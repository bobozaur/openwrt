// SPDX-License-Identifier: GPL-2.0-only
'use strict';

import { log, wiphy_info, wiphy_band } from 'wifi.common';
import { validate } from 'wifi.validate';
import * as hostapd from 'wifi.hostapd';
import * as supplicant from 'wifi.supplicant';
import * as iface from 'wifi.iface';
import * as netifd from 'wifi.netifd';
import * as rtnl from 'rtnl';
import * as fs from 'fs';
import * as libubus from 'ubus';

const wave = require('wifi.wave');

function fail(reason) {
	netifd.setup_failed(reason);
	return 1;
}

function reset_phy(phy) {
	if (!phy)
		return true;
	let ap = !global.ubus.list('hostapd') || global.ubus.call('hostapd', 'config_set', {
		phy, config: '', prev_config: `/var/run/hostapd-${phy}.conf`
	});
	let sta = !global.ubus.list('wpa_supplicant') ||
		global.ubus.call('wpa_supplicant', 'config_set', { phy, config: [] });
	return !!ap && !!sta;
}

function reset(data) {
	let sta = reset_phy(data?.sta_phy);
	let ap = reset_phy(data?.ap_phy);
	return ap && sta;
}

function iw(args) {
	if (system([ '/usr/sbin/iw', ...args ]))
		die('WAVE radio control was rejected\n');
}

function configure_radio(phy, config) {
	if (config.country_code)
		iw([ 'reg', 'set', config.country_code ]);
	if (config.txantenna != null || config.rxantenna != null) {
		let tx = config.txantenna ?? 'all';
		let rx = config.rxantenna ?? 'all';
		iw([ 'phy', phy, 'set', 'antenna', '' + tx, '' + rx ]);
	}
	if (config.txpower != null)
		iw([ 'phy', phy, 'set', 'txpower', 'limit', '' + (config.txpower * 100) ]);
	else
		iw([ 'phy', phy, 'set', 'txpower', 'auto' ]);
	for (let key in [ 'distance', 'rts' ])
		if (config[key])
			iw([ 'phy', phy, 'set', key, '' + config[key] ]);

	config.channel = config.channel == 'auto' ? 0 : +config.channel;
	if (config.channel > 0)
		config.frequency = config.band == '2g'
			? (config.channel == 14 ? 2484 : 2407 + 5 * config.channel)
			: (config.channel >= 182 ? 4000 : 5000) + 5 * config.channel;
}

function setup(data) {
	let radio = wave.find_radio(data.config);
	// Firmware initialization publishes the AP wiphy before its master and
	// station wiphy. Hotplug retry can race a handler's teardown; wait within
	// this normal setup handler, then retain netifd's bounded retry budget.
	for (let attempt = 0; !radio && attempt < 40; attempt++) {
		sleep(250);
		radio = wave.find_radio(data.config);
	}
	if (!radio) {
		log('WAVE_RADIO_NOT_READY');
		return 1;
	}

	let config = data.config;
	validate('device', config);
	if (config.frag)
		return fail('WAVE_FRAG_UNSUPPORTED');
	if (config.radio != null && config.radio >= 0)
		return fail('WAVE_MULTIRADIO_INDEX_UNSUPPORTED');
	if (config.band != '2g' && config.band != '5g')
		return fail('WAVE_BAND_UNSUPPORTED');

	let modes = {}, station_count = 0;
	for (let _, v in data.interfaces) {
		if (v.config.mode != 'ap' && v.config.mode != 'sta')
			return fail('WAVE_MODE_UNSUPPORTED');
		let multi_ap = int(v.config.multi_ap ?? 0);
		if (multi_ap < 0 || multi_ap > (v.config.mode == 'ap' ? 3 : 1))
			return fail('WAVE_MULTI_AP_ROLE_UNSUPPORTED');
		if (v.config.mode == 'ap') {
			// Backhaul peers declare four-address use during association, before
			// STA_ADD. Ordinary late WDS and shared VLAN key contexts are separate.
			if (v.config.dynamic_vlan || v.config.per_sta_vif || length(v.vlans))
				return fail('WAVE_DYNAMIC_VLAN_UNSUPPORTED');
			if (v.config.wds && !(multi_ap & 1))
				return fail('WAVE_LATE_WDS_UNSUPPORTED');
			if (multi_ap & 1)
				v.config.wds = true;
		} else if (multi_ap || v.config.multi_ap_backhaul_sta) {
			// Create the static four-address interface before associating.
			v.config.wds = true;
		}
		modes[v.config.mode] = true;
		station_count += v.config.mode == 'sta';
	}
	let mixed = modes.ap && modes.sta;
	if (mixed && station_count != 1)
		return fail('WAVE_SINGLE_UPLINK_REQUIRED');
	let mode = modes.ap ? 'ap' : 'sta';
	data.phy = radio[mode].phy;
	if (!wiphy_band(wiphy_info(data.phy), config.band))
		return fail('WAVE_BAND_UNSUPPORTED');
	data.phy_suffix = '';
	data.vif_phy_suffix = '';
	config.macaddr_base ??= wave.board_mac(config.band);
	if (!config.macaddr_base)
		return fail('WAVE_FACTORY_MAC_UNAVAILABLE');

	if (!reset(data.data) ||
	    (data.data?.ap_phy != radio.ap.phy && !reset_phy(radio.ap.phy)) ||
	    (data.data?.sta_phy != radio.sta.phy && !reset_phy(radio.sta.phy)))
		return fail('WAVE_TEARDOWN_FAILED');
	netifd.set_data({ wave: true, phy: data.phy, ap_phy: radio.ap.phy, sta_phy: radio.sta.phy });
	configure_radio(radio.ap.phy, config);
	let ap_data = { ...data, phy: radio.ap.phy, config: { ...config },
		station_phy: mixed ? radio.sta.phy : null, channel_follow: !!mixed };
	let sta_data = { ...data, phy: radio.sta.phy, config: { ...config },
		ap_phy: mixed ? radio.ap.phy : null };
	let master_mac = trim(fs.readfile(`/sys/class/net/${radio.master}/address`) ?? '');
	if (!master_mac)
		return fail('WAVE_MASTER_MAC_UNAVAILABLE');

	let idx = { ap: 0, sta: 0 }, assigned = {};
	let supplicant_data = [];
	for (let name, v in data.interfaces) {
		let mode = v.config.mode;
		let phy = radio[mode].phy;
		let prefix = config.ifname_prefix ?? (phy + '-');
		let mode_idx = idx[mode]++;
		let primary = mode_idx == 0;
		v.config.ifname ??= `${prefix}${mode}${mode_idx}`;
		if (v.config.encryption == 'owe' && v.config.owe_transition)
			v.config.owe_transition_ifname = `${prefix}${mode}${idx[mode]++}`;
		validate('iface', v.config);
		for (let _, sta in v.stas)
			validate('station', sta.config);
		iface.prepare(v.config, phy, config.num_global_macaddr, config.macaddr_base);
		// Reuse the factory address on the protected firmware master. Make
		// this explicit so hostapd's allocator does not reserve it twice.
		if (primary && mode == 'ap')
			delete v.config.default_macaddr;
		if (!v.config.default_macaddr) {
			let address = lc(v.config.macaddr);
			if (assigned[address])
				return fail('WAVE_DUPLICATE_VIF_MAC');
			assigned[address] = true;
		}
		if (mode == 'sta' && !v.config.default_macaddr &&
		    lc(v.config.macaddr) == lc(master_mac))
			return fail('WAVE_DUPLICATE_MASTER_MAC');
		if (primary && mode == 'ap') {
			let link = { dev: radio.master, change: 1, flags: 0 };
			if (rtnl.request(rtnl.const.RTM_SETLINK, 0, link) == false)
				return fail('WAVE_MASTER_DOWN_FAILED');
			if (rtnl.request(rtnl.const.RTM_SETLINK, 0, {
				dev: radio.master, ifname: v.config.ifname, address: v.config.macaddr
			}) == false)
				return fail('WAVE_MASTER_CONFIG_FAILED');
			master_mac = lc(v.config.macaddr);
		}
		// The Linux bridge owns all bridged data forwarding for WAVE, including
		// same-BSS traffic when multicast conversion is disabled.
		netifd.set_vif(name, v.config.ifname, { bridge_forwarding: mode == 'ap' });
		if (mode == 'sta' && type(supplicant.generate(supplicant_data, sta_data, v)) != 'object')
			return 1;
	}

	if (modes.sta) {
		if (!supplicant.setup(supplicant_data, sta_data))
			return fail('WAVE_SUPPLICANT_FAILED');
		// The AP wiphy owns the persistent firmware master. Reserve its
		// address in the station allocator, which only sees the STA wiphy.
		global.ubus.call('wpa_supplicant', 'phy_set_macaddr_list', {
			phy: sta_data.phy, radio: config.radio, macaddr: [ master_mac ],
		});
		if (libubus.error())
			return fail('WAVE_MAC_RESERVATION_FAILED');
	}
	if (modes.ap && !hostapd.setup(ap_data))
		return fail('WAVE_HOSTAPD_FAILED');
	if (modes.sta) {
		if (!supplicant.start(sta_data))
			return fail('WAVE_SUPPLICANT_FAILED');
	}
	netifd.set_up();
	return 0;
}

function teardown(data) {
	return reset(data.data) ? 0 : 1;
};

return { setup, teardown };
