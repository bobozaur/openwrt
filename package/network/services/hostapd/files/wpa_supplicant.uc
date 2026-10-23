let libubus = require("ubus");
import * as uloop from "uloop";
import { open, readfile } from "fs";
import { wdev_create, wdev_set_mesh_params, wdev_remove, is_equal, wdev_set_up, vlist_new, phy_open, macaddr_keep, macaddr_sync_defer, macaddr_sync_entry, mld_prev_match } from "common";

let ubus = libubus.connect();

function ex_handler(e)
{
	e = split(`${e}\n${e.stacktrace[0].context}`, '\n');
	for (let line in e)
		wpas.printf(line);
	return libubus.STATUS_UNKNOWN_ERROR;
}
libubus.guard(ex_handler);

wpas.data.mld = {};
wpas.data.config = {};
wpas.data.iface_phy = {};
wpas.data.iface_ubus = {};
wpas.data.dpp_hooks = {};
wpas.data.config_generation = 0;
wpas.data.enrollment = {};
wpas.data.enrollment_results = {};

function enrollment_result(owner, phase)
{
	let binding = owner.config.uci;
	if (!binding)
		return;
	let key = binding.section;
	let previous = wpas.data.enrollment_results[key];
	previous?.expiry?.cancel();
	// This is a bounded result of an enrollment operation, not a connection
	// state cache. Keep it through the config reload caused by persistence;
	// current connection state is always queried from the native interface.
	let result = { phase, device: binding.device };
	wpas.data.enrollment_results[key] = result;
	result.expiry = uloop.timer(120000, () => {
		if (wpas.data.enrollment_results[key] == result)
			delete wpas.data.enrollment_results[key];
	});
}

function enrollment_forget(ifname, status)
{
	let entry = wpas.data.enrollment[ifname];
	if (!entry)
		return;
	entry.expiry?.cancel();
	entry.retry?.cancel();
	delete wpas.data.enrollment[ifname];
	if (entry.owner.active_wps_run == entry.run &&
	    entry.owner.active_wps_iface == entry.iface) {
		delete entry.owner.active_wps_run;
		delete entry.owner.active_wps_iface;
	}
	if (status) {
		enrollment_result(entry.owner, status);
		wpas.printf(`WPS persistence on ${ifname}: ${status}`);
		ubus.event('wps_persistence', { ifname, status });
	}
}

function enrollment_owner(ifname, iface)
{
	let phy = wpas.data.config[wpas.data.iface_phy[ifname]];
	let owner = phy?.data[ifname];
	if (!owner || owner.config.mode != 'sta' || !owner.config.uci ||
	    wpas.interfaces[ifname] != iface)
		return null;
	let uci = owner.config.uci;
	if (type(uci.section) != 'string' || !length(uci.section) ||
	    type(uci.device) != 'string' || !length(uci.device))
		return null;
	// The transaction owns one complete persistent radio/iface snapshot.
	// Preserve section identity for status even when a multi-radio or partial
	// configuration cannot safely enroll into that transaction.
	let devices = uci.expected?.device;
	let persistent = type(uci.expected) == 'object' &&
		type(uci.expected_device) == 'object' &&
		uci.expected['.type'] == 'wifi-iface' &&
		uci.expected_device['.type'] == 'wifi-device' && uci.expected.mode == 'sta' &&
		((type(devices) == 'string' && devices == uci.device) ||
		 (type(devices) == 'array' && length(devices) == 1 &&
		  type(devices[0]) == 'string' && devices[0] == uci.device));
	return { phy, owner, persistent };
}

function enrollment_values(cred, status)
{
	if (!cred.ssid || index(cred.ssid, '\x00') >= 0 ||
	    index(cred.key, '\x00') >= 0 || cred.ieee80211w < 0 || cred.ieee80211w > 2)
		return null;
	let key_mgmt = split(cred.key_mgmt ?? '', ' ');
	let proto = split(cred.proto ?? '', ' ');
	let pairwise = split(cred.pairwise ?? '', ' ');
	// WPS AES profiles may be expanded by the native driver to several optional
	// ciphers. UCI can represent the cipher actually negotiated, even when it
	// cannot express that entire optional set in one encryption value.
	let cipher = {
		'TKIP': 'tkip', 'CCMP': 'ccmp', 'GCMP': 'gcmp',
		'CCMP-256': 'ccmp256', 'GCMP-256': 'gcmp256',
	}[status.pairwise_cipher];
	let encryption;
	if (length(key_mgmt) == 1 && key_mgmt[0] == 'NONE') {
		encryption = 'none';
	} else {
		if (!length(key_mgmt) || length(filter(key_mgmt, (v) => v != 'WPA-PSK' && v != 'SAE')) ||
		    !cipher || index(pairwise, status.pairwise_cipher) < 0)
			return null;
		if (index(key_mgmt, 'SAE') >= 0) {
			if (cipher == 'tkip')
				return null;
			encryption = index(key_mgmt, 'WPA-PSK') >= 0 ? 'sae-mixed' : 'sae';
		} else if (index(proto, 'RSN') >= 0) {
			encryption = index(proto, 'WPA') >= 0 ? 'psk-mixed' : 'psk2';
		} else if (index(proto, 'WPA') >= 0) {
			encryption = 'psk';
		} else {
			return null;
		}
		encryption += '+' + cipher;
	}
	return { ssid: cred.ssid, encryption, key: encryption == 'none' ? null : cred.key,
		ieee80211w: sprintf('%d', cred.ieee80211w), sae_password: null };
}

function enrollment_persist(ifname, iface)
{
	let entry = wpas.data.enrollment[ifname];
	if (!entry)
		return;
	if (entry.expiry.remaining() <= 0) {
		enrollment_forget(ifname, entry.publication_unconfirmed ? 'unconfirmed' : 'expired');
		return;
	}
	let binding = enrollment_owner(ifname, iface);
	let status = iface.status();
	if (!binding?.persistent || entry.iface != iface || entry.owner != binding.owner ||
	    entry.generation != binding.phy.generation || status.wps_run != entry.run) {
		enrollment_forget(ifname, entry.publication_unconfirmed ? 'unconfirmed' : 'cancelled');
		return;
	}
	if (status.state != 'COMPLETED' || !status.wps_success || !status.wps_selected ||
	    status.network_wps_run != entry.run)
		return;
	let cred = entry.candidates[sprintf('%d', status.network_id)];
	if (!cred)
		return;
	let values = enrollment_values(cred, status);
	if (!values) {
		enrollment_forget(ifname, 'unrepresentable credential');
		return;
	}
	let request = { ...entry.owner.config.uci, values };
	delete entry.confirmation;
	let result = ubus.call('uci', 'wireless_enroll', request);
	let error = ubus.error(true);
	if (wpas.data.enrollment[ifname] != entry)
		return;
	if (result?.status == 'saved' || result?.status == 'conflict') {
		enrollment_forget(ifname, result.status);
		return;
	}
	if (!result?.status && (error == null || index([
	    libubus.STATUS_TIMEOUT, libubus.STATUS_CONNECTION_FAILED,
	    libubus.STATUS_UNKNOWN_ERROR ], error) >= 0)) {
		// A committed publication can lose its reply before config.change
		// replaces this owner. Retain only an already-desired confirmation:
		// its two transaction alternatives must both equal the desired state.
		let expected = { ...request.expected };
		for (let name, value in values)
			if (value === null)
				delete expected[name];
			else
				expected[name] = value;
		entry.publication_unconfirmed = true;
		entry.confirmation = { ...request, expected };
	}
	// Pending edits/Apply retain their owner. Retry only while this exact
	// selected connection and configuration incarnation remain eligible.
	enrollment_result(entry.owner, 'waiting');
	entry.retry?.cancel();
	entry.retry = uloop.timer(2000, () => enrollment_persist(ifname, iface));
}

function enrollment_reconcile(old_if, new_if, generation)
{
	if (!old_if)
		return;
	let ifname = old_if.config.iface;
	let entry = wpas.data.enrollment[ifname];
	let request = entry?.confirmation;
	if (!request || entry.owner != old_if || entry.generation != generation ||
	    wpas.interfaces[ifname] != entry.iface ||
	    old_if.cancelled_wps_run == entry.run || entry.iface.status()?.wps_run != entry.run)
		return;
	if (entry.expiry.remaining() <= 0) {
		enrollment_forget(ifname, 'unconfirmed');
		return;
	}
	if (new_if) {
		let binding = new_if.config.uci;
		if (new_if.config.mode != 'sta' || binding?.section != request.section ||
		    binding?.device != request.device ||
		    !is_equal(binding.expected, request.expected) ||
		    !is_equal(binding.expected_device, request.expected_device))
			return;
	}
	// Reconcile the old operation before retiring its resource, including a
	// stop-first reload. No candidate, writer or new expiry crosses generations.
	// This request can only re-sync already-published values; runtime equality
	// is never a durable saved acknowledgment and unsaved disk cannot be changed.
	entry.retry?.cancel();
	delete entry.confirmation;
	let result = ubus.call('uci', 'wireless_enroll', request);
	if (wpas.data.enrollment[ifname] != entry)
		return;
	enrollment_forget(ifname, result?.status == 'saved' ? 'saved' :
		result?.status == 'conflict' ? 'conflict' : 'unconfirmed');
}

// Retain only native operation control across an identical STA incarnation.
// Persistence candidates, retries and deadlines keep their original owner.
function enrollment_retain_run(old_if, new_if, generation)
{
	let ifname = old_if.config.iface;
	let iface = wpas.interfaces[ifname];
	let entry = wpas.data.enrollment[ifname];
	let owned = entry && entry.iface == iface && entry.owner == old_if &&
		entry.generation == generation;
	let run = owned ? entry.run : old_if.active_wps_run;
	if (!old_if.running || old_if.config.mode != 'sta' || !run ||
	    iface?.status().wps_run != run || old_if.cancelled_wps_run == run ||
	    (entry && !owned) || (!owned && old_if.active_wps_iface != iface))
		return;
	if (entry)
		enrollment_forget(ifname);
	delete old_if.active_wps_run;
	delete old_if.active_wps_iface;
	new_if.active_wps_run = run;
	new_if.active_wps_iface = iface;
}

function enrollment_candidate(ifname, iface, cred)
{
	if (!cred.processed || !cred.wps_run || cred.network_id == null)
		return;
	let binding = enrollment_owner(ifname, iface);
	if (!binding?.persistent || cred.wps_run != iface.status().wps_run ||
	    binding.owner.cancelled_wps_run == cred.wps_run)
		return;
	let entry = wpas.data.enrollment[ifname];
	if (entry && (entry.iface != iface || entry.owner != binding.owner ||
	             entry.generation != binding.phy.generation || entry.run != cred.wps_run)) {
		enrollment_forget(ifname);
		entry = null;
	}
	if (!entry) {
		entry = wpas.data.enrollment[ifname] = {
			iface, owner: binding.owner, generation: binding.phy.generation,
			run: cred.wps_run, candidates: {},
		};
		entry.expiry = uloop.timer(120000, () => enrollment_forget(ifname,
			entry.publication_unconfirmed ? 'unconfirmed' : 'expired'));
	}
	enrollment_result(entry.owner, 'connecting');
	entry.candidates[sprintf('%d', cred.network_id)] = cred;
}

// Internal radio serialization keeps the native interface, accepted profiles
// and exact enrollment owner. Netdev down releases the driver's station role.
function iface_pause(owner)
{
	let ifname = owner.config.iface;
	let iface = wpas.interfaces[ifname];
	if (!owner.running || !iface)
		return true;
	if (owner.paused == iface)
		return true;
	owner.paused = iface;
	if (wdev_set_up(ifname, false)) {
		delete owner.paused;
		return false;
	}
	return true;
}

function iface_stop(iface)
{
	let ifname = iface.config.iface;

	if (!iface.running)
		return;

	enrollment_forget(ifname, wpas.data.enrollment[ifname]?.publication_unconfirmed ?
		'unconfirmed' : 'cancelled');
	delete wpas.data.iface_phy[ifname];
	wpas.remove_iface(ifname);
	wdev_remove(ifname);
	delete iface.paused;
	iface.running = false;
}

function phy_macaddr_get(phydev, name, prev, data)
{
	let phy = wpas.data.config[phydev.name];
	let ret = phydev.macaddr_get(ubus, "wpa_supplicant", name, {
		num_global: phy?.num_global_macaddr,
		macaddr_base: phy?.macaddr_base,
		ifname: name,
		static: !!data.macaddr,
		...data,
	});

	if (ret.macaddr)
		return ret.macaddr;

	prev = ret.transport ? (data.macaddr ?? prev) : null;
	wpas.printf(`No MAC address from netifd for ${name}: ${ret.error}${prev ? ", using " + prev : ""}`);

	return prev;
}

function iface_start(phydev, iface)
{
	let phy = phydev.name;

	if (iface.running && wpas.interfaces[iface.config.iface]) {
		if (!iface.paused)
			return true;
		let native = wpas.interfaces[iface.config.iface];
		if (iface.paused != native || native.status().state != 'INTERFACE_DISABLED' ||
		    wdev_set_up(iface.config.iface, true))
			return false;
		delete iface.paused;
		return true;
	}
	iface.running = false;

	let ifname = iface.config.iface;
	let macaddr = phy_macaddr_get(phydev, ifname, iface.macaddr, {
		group: phydev.name,
		macaddr: iface.config.macaddr,
	});
	if (!macaddr)
		return false;

	iface.macaddr = macaddr;
	let wdev_config = { ...iface.config, macaddr };

	wdev_remove(ifname);
	let ret = phydev.wdev_add(ifname, wdev_config);
	if (ret) {
		wpas.printf(`Failed to create device ${ifname}: ${ret}`);
		delete wpas.data.iface_phy[ifname];
		return false;
	}

	ret = wdev_set_up(ifname, true);
	if (ret) {
		wpas.printf(`Failed to bring up device ${ifname}: ${ret}`);
		delete wpas.data.iface_phy[ifname];
		wdev_remove(ifname);
		return false;
	}

	wpas.data.iface_phy[ifname] = phy;
	if (wpas.add_iface(iface.config) < 0 || !wpas.interfaces[ifname]) {
		wpas.printf(`Failed to start supplicant interface ${ifname}`);
		delete wpas.data.iface_phy[ifname];
		wdev_remove(ifname);
		return false;
	}
	let transition = wpas.data.config[phydev.name]?.transition;
	if (transition?.phase == 'resuming' && transition.frequency > 0)
		wpas.interfaces[ifname].config('freq_list', [ transition.frequency ]);
	iface.running = true;
	return true;
}

// a change of these lines needs no new netdev
const network_update_keys = [
	"ssid", "psk", "sae_password", "key_mgmt", "ieee80211w", "sae_pwe",
	"proto", "pairwise", "group",
];
// the options of a station MLD that only produce those lines
const mld_network_keys = [
	"ssid", "key", "encryption", "ieee80211w", "sae_pwe", "sae_ext_key",
];
const config_line_re = /^(\t?)([a-z0-9_]+)=/;

function config_equal_except(config_a, config_b, keys)
{
	let rest_a = { ...config_a };
	let rest_b = { ...config_b };

	for (let key in keys) {
		delete rest_a[key];
		delete rest_b[key];
	}

	return is_equal(rest_a, rest_b);
}

// every radio of a station MLD writes its own freq_list into the shared file
function config_line_keep(line)
{
	let m = match(line, config_line_re);
	if (!m)
		return true;
	if (!m[1])
		return m[2] != "freq_list";

	return index(network_update_keys, m[2]) < 0;
}

function config_data_strip(config_data)
{
	return filter(split(config_data ?? "", "\n"), config_line_keep);
}

function iface_network_apply(ifname, old_data, new_data, freq_list)
{
	let iface = wpas.interfaces[ifname];
	if (!iface ||
	    !is_equal(config_data_strip(old_data), config_data_strip(new_data)) ||
	    !iface.network_update())
		return false;

	wpas.printf(`Update network of interface ${ifname} in place`);
	if (length(freq_list) > 0)
		iface.config('freq_list', freq_list);

	return true;
}

function iface_cb(new_if, old_if, generation)
{
	enrollment_reconcile(old_if, new_if, generation);
	// A new configuration incarnation retires a paused old resource directly;
	// never bring the old profile up briefly while applying its replacement.
	if (old_if?.paused) {
		iface_stop(old_if);
		if (new_if)
			new_if.macaddr = old_if.macaddr;
		return;
	}
	if (old_if && new_if)
		new_if.macaddr = old_if.macaddr;

	if (old_if && new_if && is_equal(old_if.config, new_if.config)) {
		enrollment_retain_run(old_if, new_if, generation);
		new_if.running = old_if.running;
		new_if.macaddr = old_if.macaddr;
		return;
	}

	if (old_if?.running && new_if && new_if.config.mode == "sta" &&
	    config_equal_except(old_if.config, new_if.config, [ "config_data" ]) &&
	    iface_network_apply(old_if.config.iface, old_if.config.config_data,
				new_if.config.config_data)) {
		new_if.running = true;
		return;
	}

	if (new_if && old_if)
		wpas.printf(`Update configuration for interface ${old_if.config.iface}`);
	else if (old_if)
		wpas.printf(`Remove interface ${old_if.config.iface}`);

	if (old_if)
		iface_stop(old_if);
}

function prepare_config(config, radio)
{
	config.config_data = readfile(config.config);

	return { config };
}

function phy_dev_open(phy_name)
{
	let phy = wpas.data.config[phy_name];
	if (!phy) {
		warn(`Missing phy config for ${phy_name}\n`);
		return;
	}

	return phy_open(phy.name, phy.radio);
}

function start_pending(phy_name)
{
	let phy = wpas.data.config[phy_name];
	if (!phy || !phy.data || phy.transition?.phase == 'paused')
		return;

	let phydev = phy_dev_open(phy_name);
	if (!phydev) {
		wpas.printf(`Could not open phy ${phy_name}`);
		return;
	}

	for (let ifname in phy.data)
		iface_start(phydev, phy.data[ifname]);
}

function phy_name(phy, radio)
{
	if (!phy)
		return null;

	if (radio != null && radio >= 0)
		phy += "." + radio;

	return phy;
}

function mld_remove(data)
{
	if (!data.radio_mask_up)
		return;

	let name = data.name;
	wpas.printf(`Remove MLD interface ${name}`);
	wpas.remove_iface(name);
	wdev_remove(name);
	data.radio_mask_up = 0;
}

function mld_first_phy(data)
{
	let mask = data.radio_mask_present;

	for (let i = 0; mask; i++, mask >>= 1)
		if (mask & 1)
			return i;
}

// the first configured radio stays the same when a radio goes down
function mld_macaddr_radio(data)
{
	let mask = data.radio_mask;

	for (let i = 0; mask; i++, mask >>= 1)
		if ((mask & 1) && wpas.data.config[data.phy + '.' + i])
			return i;
}

function mld_radio_index(data, freq)
{
	let phys = data.phy_config;
	for (let i = 0; i < length(phys); i++)
		if (phys[i] && index(phys[i].freq_list, freq) >= 0)
			return i;
}

function mld_add(data, phy_list)
{
	let name = data.name;
	phy_list ??= {};

	wpas.printf(`Add MLD interface ${name}`);

	let radio = mld_first_phy(data);
	if (radio == null)
		return;

	let phy_name = data.phy + '.' + (mld_macaddr_radio(data) ?? radio);
	let phydev = phy_list[phy_name];
	if (!phydev) {
		phydev = phy_dev_open(phy_name);
		if (!phydev)
			return;

		phy_list[phy_name] = phydev;
	}

	let macaddr = phy_macaddr_get(phydev, name, data.macaddr, {
		group: "mld",
		macaddr: data.config.macaddr,
		any_radio: true,
	});
	if (!macaddr)
		return;

	data.macaddr = macaddr;
	let wdev_config = { ...data.config, radio_mask: data.radio_mask, macaddr };
	let ret = phydev.wdev_add(name, wdev_config);
	if (ret) {
		wpas.printf(`Failed to create device ${name}: ${ret}`);
		return;
	}

	let first_config = data.phy_config[radio];

	ret = wdev_set_up(name, true);
	if (ret) {
		wpas.printf(`Failed to bring up device ${name}: ${ret}`);
		wdev_remove(name);
		return;
	}

	data.config_data = readfile(first_config?.config);
	wpas.add_iface(first_config);

	let iface = wpas.interfaces[name];
	if (!iface) {
		wpas.printf(`Interface ${name} not found after adding\n`);
		wpas.remove_iface(name);
		wdev_remove(name);
		return;
	}

	if (length(data.freq_list) > 0)
		iface.config('freq_list', data.freq_list);

	data.radio_mask_up = data.radio_mask_present;
}

function mld_remove_links(data)
{
	// TODO
	mld_remove(data);
}

function mld_add_links(data)
{
	// TODO: incremental update
	mld_remove(data);
	mld_add(data);
}

function mld_reload(data, phy_list)
{
	let first_config = data.phy_config[mld_first_phy(data)];
	let config_data = readfile(first_config?.config);

	if (iface_network_apply(data.name, data.config_data, config_data,
				data.freq_list)) {
		data.config_data = config_data;
		return;
	}

	mld_remove(data);
	mld_add(data, phy_list);
}

function mld_macaddr_list(new_mld, added, prev_mld)
{
	let list = macaddr_keep(keys(new_mld));
	let news = {};
	let prevs = {};

	for (let name, config in added)
		if (!config.macaddr)
			news[name] = config;
	for (let name, data in prev_mld)
		if (data.macaddr && !data.config.macaddr)
			prevs[name] = data.config;

	for (let name, from in mld_prev_match(news, prevs)) {
		new_mld[name].macaddr = prev_mld[from].macaddr;
		if (from != name)
			list[name] = macaddr_sync_entry(new_mld[name].phy, -1, prev_mld[from].macaddr, {
				any_radio: true,
			});
	}

	return list;
}

function mld_set_config(config)
{
	let prev_mld = { ...wpas.data.mld };
	let new_mld = {};
	let added = {};

	wpas.printf(`Set MLD config: ${keys(config)}`);

	for (let name, data in config) {
		let prev = prev_mld[name];
		if (prev && is_equal(prev.config, data)) {
			new_mld[name] = prev;
			delete prev_mld[name];
			continue;
		}

		if (prev && config_equal_except(prev.config, data, mld_network_keys)) {
			wpas.printf(`Update network of MLD interface ${name}`);
			prev.config = data;
			prev.reload = true;
			new_mld[name] = prev;
			delete prev_mld[name];
			continue;
		}

		let radio_mask = 0;
		for (let r in data.radios)
			if (r != null)
				radio_mask |= 1 << r;

		new_mld[name] = {
			name,
			config: data,
			phy: data.phy,
			phy_config: [],
			macaddr: prev?.macaddr,
			radio_mask,
			radio_mask_up: 0,
			radio_mask_present: 0,
		};
		added[name] = data;
	}

	for (let name, data in prev_mld)
		mld_remove(data);

	wpas.data.mld = new_mld;
	macaddr_sync_defer(ubus, "wpa_supplicant", "mld", mld_macaddr_list(new_mld, added, prev_mld));
}

function mld_set_iface_config(name, data, radio, config)
{
	wpas.printf(`Set MLD interface ${name} radio ${radio} config: ${keys(config)}`);

	data.phy_config[radio] = config;
	if (config)
		data.radio_mask_present |= 1 << radio;
	else
		data.radio_mask_present &= ~(1 << radio);

	let freq_list;
	for (let config in data.phy_config) {
		if (!config || !config.freq_list)
			continue;
		if (!freq_list)
			freq_list = [ ...config.freq_list ];
		else
			push(freq_list, ...config.freq_list);
	}

	data.freq_list = freq_list;
}

function mld_update_iface(name, data) {
	if (!data.radio_mask_up)
		return;

	if (!data.radio_mask_present) {
		mld_remove(data);
		return;
	}

	let mask = data.radio_mask_up & ~data.radio_mask_present;
	if (!mask)
		return;

	mld_remove_links(data);
}

function mld_update_phy(phy, ifaces) {
	for (let name, data in wpas.data.mld) {
		if (data.phy != phy.name)
			continue;

		mld_set_iface_config(name, data, phy.radio, ifaces[name]);
		mld_update_iface(name, data);
	}
}

function mld_start() {
	if (wpas.data.mld_pending)
		return;

	wpas.printf(`Start pending MLD interfaces\n`);

	let phy_list = {};
	for (let name, data in wpas.data.mld) {
		wpas.printf(`MLD interface ${name} present=${data.radio_mask_present} up=${data.radio_mask_up}`);
		let reload = data.reload;
		delete data.reload;

		let add_mask = data.radio_mask_present & ~data.radio_mask_up;
		if (!add_mask && reload && data.radio_mask_up)
			mld_reload(data, phy_list);
		if (!add_mask)
			continue;

		if (!data.radio_mask_up)
			mld_add(data, phy_list);
		else
			mld_add_links(data);
	}
}

function mld_bss_allowed(data, bss) {
	if (!data.freq_list)
		return true;

	return index(data.freq_list, bss.freq) >= 0;
}

function phy_transition_finish(phy, resume, force)
{
	let transition = phy.transition;
	if (!transition)
		return true;
	resume ??= true;
	transition.request?.reply(null, libubus.STATUS_UNKNOWN_ERROR);
	delete transition.request;
	if (resume && !force) {
		for (let ifname in transition.freq_list) {
			let iface = wpas.interfaces[ifname];
			if (iface && phy.data[ifname]?.paused == iface &&
			    iface.status().state != 'INTERFACE_DISABLED') {
				// A queued DOWN notification is ignored if the netdev is
				// already UP. Keep the existing lease until DOWN is observed.
				transition.finishing = true;
				return true;
			}
		}
	}
	transition.lease?.cancel();
	delete phy.transition;
	let success = true;
	for (let ifname, freqs in transition.freq_list) {
		let iface = wpas.interfaces[ifname];
		if (!iface)
			continue;
		iface.config('freq_list', freqs);
		let owner = phy.data[ifname];
		if (!resume || owner?.paused != iface)
			continue;
		if (wdev_set_up(ifname, true)) {
			wpas.printf(`Failed to resume station ${ifname}`);
			success = false;
		} else {
			delete owner.paused;
		}
	}
	return success;
}

function phy_transition_ready(phy)
{
	let transition = phy.transition;
	if (!transition || transition.phase != 'paused')
		return;
	if (transition.finishing) {
		phy_transition_finish(phy);
		return;
	}
	if (!transition.request)
		return;
	for (let ifname in transition.freq_list) {
		let iface = wpas.interfaces[ifname];
		if (phy.data[ifname]?.paused &&
		    (phy.data[ifname].paused != iface ||
		     iface.status().state != 'INTERFACE_DISABLED'))
			return;
	}
	transition.request.reply({ generation: phy.generation, token: transition.token });
	delete transition.request;
}

function phy_transition_lease(name, phy, milliseconds)
{
	let transition = phy.transition;
	if (transition.lease)
		transition.lease.cancel();
	transition.lease = uloop.timer(milliseconds, () => {
		if (phy.transition != transition)
			return;
		wpas.printf(`AP/STA transition on ${name} expired; restoring station operation`);
		phy_transition_finish(phy, true, true);
		start_pending(name);
	});
}

function set_config(config_name, phy_name, radio, num_global_macaddr, macaddr_base, config_list, ap_phy)
{
	let phy = wpas.data.config[config_name];

	if (radio < 0)
		radio = null;

	if (!phy) {
		phy = vlist_new(iface_cb, false);
		phy.name = phy_name;
		wpas.data.config[config_name] = phy;
	}

	phy.radio = radio;
	phy_transition_finish(phy, false);
	let previous_generation = phy.generation;
	phy.generation = ++wpas.data.config_generation;
	phy.ap_phy = ap_phy;
	phy.num_global_macaddr = num_global_macaddr;
	phy.macaddr_base = macaddr_base;

	let values = [];
	let mlo_ifaces = {};
	for (let config in config_list)
		if (config.mlo)
			mlo_ifaces[config.iface] = config;
		else
			push(values, [ config.iface, prepare_config(config) ]);

	mld_update_phy(phy, mlo_ifaces);
	phy.update(values, previous_generation);

	macaddr_sync_defer(ubus, "wpa_supplicant", config_name, macaddr_keep(keys(phy.data)));
}

function config_clear()
{
	for (let name, phy in wpas.data.config)
		set_config(name, phy.name, phy.radio, phy.num_global_macaddr, phy.macaddr_base, []);
}

function iface_status_fill_radio_link(mld, radio, msg, link)
{
	let config = mld.phy_config[radio];
	if (!config)
		return;

	let freq_list = config.freq_list;
	if (!freq_list)
		return;

	if (!link || index(freq_list, link.frequency) < 0)
		return;

	msg.frequency = link.frequency;
	msg.sec_chan_offset = link.sec_chan_offset;
}

function iface_status_fill_radio(mld, radio, msg, status)
{
	if (status.links)
		for (let link in status.links)
			iface_status_fill_radio_link(mld, radio, msg, link);
	else
		iface_status_fill_radio_link(mld, radio, msg, status);
}

function dpp_find_iface(ifname)
{
	return wpas.interfaces[ifname];
}

function dpp_channel_handle_request(channel, req)
{
	let data = req.args ?? {};
	let iface;

	switch (req.type) {
	case "start":
		if (!data.ifname)
			return libubus.STATUS_INVALID_ARGUMENT;
		if (!wpas.interfaces[data.ifname])
			return libubus.STATUS_NOT_FOUND;
		let old_hook = wpas.data.dpp_hooks[data.ifname];
		if (old_hook && old_hook.channel != channel)
			old_hook.channel.disconnect();
		wpas.data.dpp_hooks[data.ifname] = {
			channel: channel,
			timeout_count: 0,
		};
		return 0;

	case "stop":
		if (!data.ifname)
			return libubus.STATUS_INVALID_ARGUMENT;
		let hook = wpas.data.dpp_hooks[data.ifname];
		if (hook && hook.channel == channel)
			delete wpas.data.dpp_hooks[data.ifname];
		return 0;

	case "tx_action":
		iface = dpp_find_iface(data.ifname);
		if (!iface)
			return libubus.STATUS_NOT_FOUND;
		if (!iface.dpp_send_action(data.dst, data.freq ?? 0, data.frame))
			return libubus.STATUS_UNKNOWN_ERROR;
		return 0;

	case "tx_gas_req":
		iface = dpp_find_iface(data.ifname);
		if (!iface)
			return libubus.STATUS_NOT_FOUND;
		if (!iface.dpp_send_gas_req(data.dst, data.freq ?? 0, data.data, data.dialog_token ?? 0))
			return libubus.STATUS_UNKNOWN_ERROR;
		return 0;

	case "tx_gas_comeback_req":
		iface = dpp_find_iface(data.ifname);
		if (!iface)
			return libubus.STATUS_NOT_FOUND;
		if (!iface.dpp_send_gas_comeback_req(data.dst, data.freq ?? 0, data.dialog_token ?? 0))
			return libubus.STATUS_UNKNOWN_ERROR;
		return 0;

	case "dpp_bootstrap_gen":
		iface = dpp_find_iface(data.ifname);
		if (!iface)
			return libubus.STATUS_NOT_FOUND;
		let gen_cmd = "DPP_BOOTSTRAP_GEN type=qrcode";
		if (data.key)
			gen_cmd += " key=" + data.key;
		if (data.curve)
			gen_cmd += " curve=" + data.curve;
		let gen_result = iface.ctrl(gen_cmd);
		if (!gen_result || gen_result == "FAIL")
			return libubus.STATUS_UNKNOWN_ERROR;
		return { id: +gen_result };

	case "dpp_bootstrap_remove":
		iface = dpp_find_iface(data.ifname);
		if (!iface)
			return libubus.STATUS_NOT_FOUND;
		let remove_result = iface.ctrl("DPP_BOOTSTRAP_REMOVE " + (data.id ?? "*"));
		return (remove_result == "OK") ? 0 : libubus.STATUS_UNKNOWN_ERROR;

	case "dpp_chirp":
		iface = dpp_find_iface(data.ifname);
		if (!iface)
			return libubus.STATUS_NOT_FOUND;
		let chirp_cmd = "DPP_CHIRP own=" + data.id;
		if (data.iter)
			chirp_cmd += " iter=" + data.iter;
		if (data.listen_freq)
			chirp_cmd += " listen=" + data.listen_freq;
		let chirp_result = iface.ctrl(chirp_cmd);
		return (chirp_result == "OK") ? 0 : libubus.STATUS_UNKNOWN_ERROR;

	case "dpp_stop_chirp":
		iface = dpp_find_iface(data.ifname);
		if (!iface)
			return libubus.STATUS_NOT_FOUND;
		iface.ctrl("DPP_STOP_CHIRP");
		return 0;

	case "wps_set_m7":
		iface = dpp_find_iface(data.ifname);
		if (!iface)
			return libubus.STATUS_NOT_FOUND;
		if (!iface.wps_set_m7(data.data))
			return libubus.STATUS_UNKNOWN_ERROR;
		return 0;

	default:
		return libubus.STATUS_METHOD_NOT_FOUND;
	}
}

function dpp_channel_handle_disconnect(channel)
{
	for (let ifname, hook in wpas.data.dpp_hooks) {
		if (hook.channel == channel)
			delete wpas.data.dpp_hooks[ifname];
	}
}

function dpp_rx_via_channel(ifname, method, data, no_reply)
{
	let hook = wpas.data.dpp_hooks[ifname];
	if (!hook)
		return null;

	let req = {
		method: method,
		data: data,
	};

	if (no_reply) {
		req.return = "ignore";
		hook.channel.request(req);
		return null;
	}

	let response = hook.channel.request(req);
	if (hook.channel.error(true) == libubus.STATUS_TIMEOUT) {
		hook.timeout_count++;
		if (hook.timeout_count >= 3) {
			wpas.printf(`DPP channel timeout for ${ifname}, disconnecting`);
			hook.channel.disconnect();
			delete wpas.data.dpp_hooks[ifname];
		}
		return null;
	}

	hook.timeout_count = 0;
	return response;
}

let main_obj = {
	phy_transition: {
		args: { phy: '', owner: '', token: '', replaces: '', action: '', frequency: 0, lease_ms: 0 },
		call: function(req) {
			let phy = wpas.data.config[req.args.phy];
			if (!phy || !req.args.token || phy.ap_phy != req.args.owner)
				return libubus.STATUS_NOT_FOUND;
			let transition = phy.transition;
			if (req.args.action == 'pause') {
				if (transition && transition.token != req.args.token &&
				    transition.token != req.args.replaces)
					return libubus.STATUS_PERMISSION_DENIED;
				if (transition?.token == req.args.token) {
					if (transition.request || transition.finishing)
						return libubus.STATUS_PERMISSION_DENIED;
					return { generation: phy.generation, token: transition.token };
				}
				let previous_freqs = transition?.freq_list;
				// Transfer only the same native paused resources, without UP.
				phy_transition_finish(phy, false);
				transition = phy.transition = {
					token: req.args.token, owner: req.args.owner,
					generation: phy.generation, phase: 'paused', freq_list: {},
				};
				phy_transition_lease(req.args.phy, phy, 30000);
				for (let ifname, entry in phy.data) {
					let iface = wpas.interfaces[ifname];
					let previous = previous_freqs && index(keys(previous_freqs), ifname) >= 0;
					transition.freq_list[ifname] = previous ? previous_freqs[ifname] :
						(iface ? iface.config('freq_list') : null);
					if (!iface_pause(entry)) {
						phy_transition_finish(phy);
						start_pending(req.args.phy);
						return libubus.STATUS_UNKNOWN_ERROR;
					}
				}
				req.defer();
				transition.request = req;
				phy_transition_ready(phy);
				return;
			}
			if (!transition && req.args.action == 'cancel') {
				// A previous UP failure retains its exact native owner. The
				// ordinary start path can restore that resource without re-add.
				for (let ifname, entry in phy.data)
					if (entry.paused) {
						start_pending(req.args.phy);
						break;
					}
				for (let ifname, entry in phy.data)
					if (entry.paused)
						return libubus.STATUS_UNKNOWN_ERROR;
				return 0;
			}
			if (!transition || transition.token != req.args.token ||
			    transition.generation != phy.generation)
				return libubus.STATUS_NOT_FOUND;
			if (req.args.action == 'cancel') {
				let restored = phy_transition_finish(phy);
				start_pending(req.args.phy);
				return restored ? 0 : libubus.STATUS_UNKNOWN_ERROR;
			}
			if (req.args.action == 'renew') {
				if (req.args.lease_ms <= 0)
					return libubus.STATUS_INVALID_ARGUMENT;
				phy_transition_lease(req.args.phy, phy, req.args.lease_ms);
				return 0;
			}
			if (req.args.action != 'resume' || req.args.frequency < 0)
				return libubus.STATUS_INVALID_ARGUMENT;
			if (transition.request || transition.finishing)
				return libubus.STATUS_PERMISSION_DENIED;
			transition.phase = 'resuming';
			transition.frequency = req.args.frequency;
			if (!transition.frequency) {
				if (!phy_transition_finish(phy))
					return libubus.STATUS_UNKNOWN_ERROR;
			} else {
				phy_transition_lease(req.args.phy, phy, 35000);
				for (let ifname, entry in phy.data) {
					let iface = wpas.interfaces[ifname];
					if (!iface || entry.paused != iface)
						continue;
					if (!iface.config('freq_list', [ transition.frequency ]) ||
					    wdev_set_up(ifname, true))
						return libubus.STATUS_UNKNOWN_ERROR;
					delete entry.paused;
				}
			}
			start_pending(req.args.phy);
			for (let ifname in phy.data)
				if (!wpas.interfaces[ifname])
					return libubus.STATUS_UNKNOWN_ERROR;
			return 0;
		}
	},
	phy_set_state: {
		args: {
			phy: "",
			radio: 0,
			stop: true,
		},
		call: function(req) {
			let name = phy_name(req.args.phy, req.args.radio);
			if (!name || req.args.stop == null)
				return libubus.STATUS_INVALID_ARGUMENT;

			let phy = wpas.data.config[name];
			if (!phy)
				return libubus.STATUS_NOT_FOUND;
			if (phy.transition)
				return libubus.STATUS_PERMISSION_DENIED;

			let radio_mask = phy.radio != null ? (1 << phy.radio) : 0;
			if (req.args.stop) {
				for (let ifname in phy.data)
					iface_stop(phy.data[ifname]);
				for (let name, data in wpas.data.mld) {
					if (data.phy != phy.name)
						continue;
					data.radio_mask_present &= ~radio_mask;
					if (data.radio_mask_up & radio_mask)
						mld_update_iface(name, data);
				}
			} else {
				start_pending(name);

				let found;
				for (let name, data in wpas.data.mld) {
					if (data.phy != phy.name)
						continue;
					if (!(data.radio_mask & radio_mask))
						continue;
					data.radio_mask_present |= radio_mask;
					found = true;
				}
				if (found)
					mld_start();
			}

			return 0;
		}
	},
	phy_status: {
		args: {
			phy: "",
			radio: 0,
		},
		call: function(req) {
			let phy = phy_name(req.args.phy, req.args.radio);
			if (!phy)
				return libubus.STATUS_INVALID_ARGUMENT;

			phy = wpas.data.config[phy];
			if (!phy)
				return libubus.STATUS_NOT_FOUND;

			let ifnames = keys(phy.data);
			let radio_mask = phy.radio != null ? (1 << phy.radio) : 0;
			for (let name, data in wpas.data.mld) {
				if (data.phy != phy.name)
					continue;
				if (data.radio_mask_up & radio_mask)
					push(ifnames, name);
			}

			for (let ifname in ifnames) {
				try {
					let iface = wpas.interfaces[ifname];
					if (!iface)
						continue;

					let status = iface.status();
					if (!status)
						continue;

					if (status.state == "INTERFACE_DISABLED")
						continue;

					if (phy.data[ifname]) {
						status.ifname = ifname;
						return status;
					}

					let ret = {
						ifname,
						state: status.state,
					};

					let mld = wpas.data.mld[ifname];
					iface_status_fill_radio(mld, phy.radio, ret, status);
					return ret;
				} catch (e) {
					ex_handler(e);
					continue;
				}
			}

			return libubus.STATUS_NOT_FOUND;
		}
	},
	iface_status: {
		args: {
			name: ""
		},
		call: function(req) {
			let iface = wpas.interfaces[req.args.name];
			if (!iface)
				return libubus.STATUS_NOT_FOUND;

			return iface.status();
		},
	},
	mld_set: {
		args: {
			config: {}
		},
		call: function(req) {
			if (!req.args.config)
				return libubus.STATUS_INVALID_ARGUMENT;

			wpas.data.mld_pending = true;
			mld_set_config(req.args.config);
			return 0;
		}
	},
	mld_start: {
		args: {},
		call: function(req) {
			wpas.data.mld_pending = false;
			mld_start();
			return 0;
		}
	},
	config_reset: {
		args: {},
		call: function(req) {
			config_clear();
			mld_set_config({});
			return 0;
		}
	},
	config_set: {
		args: {
			phy: "",
			radio: 0,
			num_global_macaddr: 0,
			macaddr_base: "",
			ap_phy: '',
			config: [],
			defer: true,
		},
		call: function(req) {
			let phy = phy_name(req.args.phy, req.args.radio);
			if (!phy)
				return libubus.STATUS_INVALID_ARGUMENT;

			wpas.printf(`Set new config for phy ${phy}`);
			if (req.args.config)
				set_config(phy, req.args.phy, req.args.radio, req.args.num_global_macaddr, req.args.macaddr_base, req.args.config, req.args.ap_phy);

			if (!req.args.defer) {
				let owner = wpas.data.config[phy];
				let generation = owner?.generation;
				uloop.timer(100, () => {
					if (wpas.data.config[phy] == owner && owner?.generation == generation)
						start_pending(phy);
				});
			}

			return {
				pid: wpas.getpid()
			};
		}
	},
	config_add: {
		args: {
			driver: "",
			iface: "",
			bridge: "",
			hostapd_ctrl: "",
			ctrl: "",
			config: "",
		},
		call: function(req) {
			if (!req.args.iface || !req.args.config)
				return libubus.STATUS_INVALID_ARGUMENT;

			if (wpas.add_iface(req.args) < 0)
				return libubus.STATUS_INVALID_ARGUMENT;

			return {
				pid: wpas.getpid()
			};
		}
	},
	config_remove: {
		args: {
			iface: ""
		},
		call: function(req) {
			if (!req.args.iface)
				return libubus.STATUS_INVALID_ARGUMENT;

			wpas.remove_iface(req.args.iface);
			return 0;
		}
	},
	bss_info: {
		args: {
			iface: "",
		},
		call: function(req) {
			let ifname = req.args.iface;
			if (!ifname)
				return libubus.STATUS_INVALID_ARGUMENT;

			let iface = wpas.interfaces[ifname];
			if (!iface)
				return libubus.STATUS_NOT_FOUND;

			let status = iface.ctrl("STATUS");
			if (!status)
				return libubus.STATUS_NOT_FOUND;

			let ret = {};
			status = split(status, "\n");
			for (let line in status) {
				line = split(line, "=", 2);
				ret[line[0]] = line[1];
			}

			return ret;
		}
	},
	status: {
		args: {},
		call: function(req) {
			let interfaces = {};

			for (let phy_name, phy in wpas.data.config) {
				if (!phy || !phy.data)
					continue;

				for (let ifname, iface_data in phy.data) {
					let config = iface_data.config;

					let entry = {
						wiphy: phy.name,
						macaddr: config.macaddr ?? iface_data.macaddr,
						running: !!iface_data.running,
						pending: !iface_data.running,
					};

					if (phy.radio != null && phy.radio >= 0)
						entry.radio = phy.radio;

					interfaces[config.iface] = entry;
				}
			}

			for (let name, mld in wpas.data.mld) {
				let entry = {
					wiphy: mld.phy,
					links: {},
				};

				let macaddr = mld.config?.macaddr ?? mld.macaddr;
				if (macaddr)
					entry.macaddr = macaddr;

				let mask = mld.radio_mask;
				for (let radio = 0; mask; radio++, mask >>= 1) {
					if (!(mask & 1))
						continue;

					entry.links[radio] = {
						radio,
						running: !!(mld.radio_mask_up & (1 << radio)),
						pending: !!(mld.radio_mask_present & (1 << radio)) &&
						         !(mld.radio_mask_up & (1 << radio)),
					};
				}

				interfaces[mld.name] = entry;
			}

			return { interfaces };
		}
	},
	dpp_channel: {
		args: {},
		call: function(req) {
			let channel;
			let on_request = (chan_req) => dpp_channel_handle_request(channel, chan_req);
			let on_disconnect = () => dpp_channel_handle_disconnect(channel);

			channel = req.new_channel(on_request, on_disconnect, 1);
			if (!channel)
				return libubus.STATUS_UNKNOWN_ERROR;

			return 0;
		}
	},
};

wpas.data.ubus = ubus;
wpas.data.obj = ubus.publish("wpa_supplicant", main_obj);

wpas.data.hostapd_watch = ubus.subscriber(() => 0, () => {
	// A vanished AP owner cannot retain a station pause or frequency pin.
	let lost = [];
	for (let name, phy in wpas.data.config)
		if (phy.transition)
			push(lost, { name, phy, transition: phy.transition });
	uloop.timer(0, () => {
		for (let entry in lost) {
			if (entry.phy.transition != entry.transition)
				continue;
			phy_transition_finish(entry.phy);
			start_pending(entry.name);
		}
	});
}, [ 'hostapd' ]);
wpas.data.hostapd_watch.subscribe('hostapd');
wpas.udebug_set("wpa_supplicant", wpas.data.ubus);

function iface_event(type, name, data) {
	let ubus = wpas.data.ubus;

	data ??= {};
	data.name = name;
	let req = wpas.data.obj.notify(`iface.${type}`, data, null, null, null, -1);
	if (req)
		req.abort();

	ubus.call("service", "event", { type: `wpa_supplicant.${name}.${type}`, data: {} });
}

function iface_hostapd_notify(ifname, iface, state)
{
	let status = iface.status();
	let ubus = wpas.data.ubus;
	let msg = { station_state: state };

	let mld = wpas.data.mld[ifname];
	switch (state) {
	case "DISCONNECTED":
	case "AUTHENTICATING":
	case "SCANNING":
		msg.up = false;
		break;
	case "INTERFACE_DISABLED":
	case "INACTIVE":
		msg.up = true;
		break;
	case "COMPLETED":
		msg.up = true;
		if (!mld) {
			msg.frequency = status.frequency;
			msg.sec_chan_offset = status.sec_chan_offset;
			if (status.chandef)
				msg.chandef = status.chandef;
		}
		break;
	default:
		return;
	}

	if (!mld) {
		msg.phy = wpas.data.iface_phy[ifname];
		if (!msg.phy) {
			wpas.printf(`no PHY for ifname ${ifname}`);
			return;
		}
		let phy = wpas.data.config[msg.phy];
		msg.phy = phy?.ap_phy ?? msg.phy;
		let transition = phy?.transition;
		if (transition)
			msg.transition_token = transition.token;
		ubus.call("hostapd", "apsta_state", msg);
		if (state == 'COMPLETED' && transition && phy.transition == transition &&
		    transition.phase == 'resuming')
			phy_transition_finish(phy);
		return;
	}

	let radio_mask = mld.radio_mask;
	for (let i = 0; radio_mask; i++, radio_mask >>= 1) {
		if (!(radio_mask & 1)) {
			wpas.printf(`skip radio ${i}`);
			continue;
		}

		let radio_msg = {
			...msg,
			phy: mld.phy,
			radio: i,
		};

		if (state == "COMPLETED") {
			iface_status_fill_radio(mld, i, radio_msg, status);

			// A station MLD spans every radio it was configured for, but only
			// drives the channel of the radios it holds a link on. Tell the AP
			// side which radios are left without one, so they can fall back to
			// their own channel configuration.
			if (!radio_msg.frequency)
				radio_msg.no_link = true;
		}

		ubus.call("hostapd", "apsta_state", radio_msg);
	}
}

function iface_channel_switch(ifname, iface, info)
{
	let msg = {
		up: true,
		csa: true,
		csa_count: info.csa_count ? info.csa_count - 1 : 0,
		frequency: info.frequency,
		sec_chan_offset: info.sec_chan_offset,
	};
	if (info.chandef)
		msg.chandef = info.chandef;

	let mld = wpas.data.mld[ifname];
	if (mld) {
		msg.phy = mld.phy;
		msg.radio = mld_radio_index(mld, info.frequency);
		if (msg.radio == null) {
			wpas.printf(`PHY ${mld.phy} radio for frequency ${info.frequency} not found`);
			return;
		}
	} else {
		msg.phy = wpas.data.iface_phy[ifname];
		if (!msg.phy) {
			wpas.printf(`no PHY for ifname ${ifname}`);
			return;
		}
		let phy = wpas.data.config[msg.phy];
		msg.phy = phy?.ap_phy ?? msg.phy;
		if (phy?.transition)
			msg.transition_token = phy.transition.token;
	}

	ubus.call("hostapd", "apsta_state", msg);
}

function iface_ubus_remove(ifname)
{
	let obj = wpas.data.iface_ubus[ifname];
	if (!obj)
		return;

	obj.remove();
	delete wpas.data.iface_ubus[ifname];
}

function iface_ubus_notify(ifname, event)
{
	event = { ifname, event };

	dpp_rx_via_channel(ifname, "ctrl-event", event, true);
	let obj = wpas.data.iface_ubus[ifname];
	if (!obj)
		return;

	obj.notify('ctrl-event', event, null, null, null, -1);
}

function iface_ubus_add(ifname)
{
	let ubus = wpas.data.ubus;

	iface_ubus_remove(ifname);

	let obj = ubus.publish(`wpa_supplicant.${ifname}`, {
		reload: {
			args: {},
			call: (req) => {
				let iface = wpas.interfaces[ifname];
				if (!iface)
					return libubus.STATUS_NOT_FOUND;

				iface.ctrl("RECONFIGURE");

				// the file of a station MLD holds the frequencies of one radio only
				let mld = wpas.data.mld[ifname];
				if (length(mld?.freq_list) > 0)
					iface.config('freq_list', mld.freq_list);
				return 0;
			},
		},
		wps_start: {
			args: {
				multi_ap: true,
				section: '',
				// uHTTPd supplies the authenticated transport session. Its ACL
				// check owns authorization; this method owns section identity.
				ubus_rpc_session: '',
			},
			call: (req) => {
				let iface = wpas.interfaces[ifname];
				if (!iface)
					return libubus.STATUS_NOT_FOUND;

				let binding = enrollment_owner(ifname, iface);
				if (req.args.section != null && req.args.section != binding?.owner.config.uci.section)
					return libubus.STATUS_NOT_FOUND;
				if (req.args.section != null && !binding?.persistent)
					return libubus.STATUS_NOT_SUPPORTED;
				enrollment_forget(ifname);
				if (binding) {
					delete binding.owner.active_wps_run;
					delete binding.owner.active_wps_iface;
					// Starting a replacement can synchronously retire the old run.
					binding.owner.starting_wps_run = iface.status().wps_run;
				}
				let multi_ap = req.args.multi_ap ?? binding?.owner.config.wps_multi_ap ?? false;
				let result = iface.ctrl(`WPS_PBC multi_ap=${+multi_ap}`);
				if (binding) {
					delete binding.owner.starting_wps_run;
					let run = iface.status().wps_run;
					if (result && match(result, /^OK\n?$/) &&
					    binding.owner.cancelled_wps_run != run) {
						binding.owner.active_wps_run = run;
						binding.owner.active_wps_iface = iface;
						enrollment_result(binding.owner, 'active');
					}
				}
				return result && match(result, /^OK\n?$/) ? 0 : libubus.STATUS_INVALID_ARGUMENT;
			},
		},
		wps_status: {
			args: { section: '', ubus_rpc_session: '' },
			call: (req) => {
				let iface = wpas.interfaces[ifname];
				if (!iface)
					return libubus.STATUS_NOT_FOUND;
				let binding = enrollment_owner(ifname, iface);
				if (req.args.section != null && req.args.section != binding?.owner.config.uci.section)
					return libubus.STATUS_NOT_FOUND;
				let result = wpas.data.enrollment_results[binding?.owner.config.uci.section];
				if (result?.device != binding?.owner.config.uci.device)
					result = null;
				return { state: iface.status().state,
					phase: result?.phase ?? 'idle',
					persistent: !!binding?.persistent };
			},
		},
		wps_cancel: {
			args: { section: '', ubus_rpc_session: '' },
			call: (req) => {
				let iface = wpas.interfaces[ifname];
				if (!iface)
					return libubus.STATUS_NOT_FOUND;

				let binding = enrollment_owner(ifname, iface);
				if (req.args.section != null && req.args.section != binding?.owner.config.uci.section)
					return libubus.STATUS_NOT_FOUND;
				let state = iface.status();
				// Persistence reloads the saved profile into a new native network,
				// so its WPS run tags no longer identify the established connection.
				// Cancel may withdraw a selected candidate waiting to be saved, but
				// must not disconnect any completed non-WPS profile or replace a
				// terminal saved result on a late/repeated request.
				if (state.state == 'COMPLETED' && state.wps_selected) {
					if (wpas.data.enrollment[ifname]) {
						if (binding)
							binding.owner.cancelled_wps_run = state.wps_run;
						enrollment_forget(ifname, 'cancelled');
					}
					return 0;
				}
				enrollment_forget(ifname);
				if (binding) {
					binding.owner.cancelled_wps_run = state.wps_run;
					delete binding.owner.active_wps_run;
					delete binding.owner.active_wps_iface;
					enrollment_result(binding.owner, 'cancelled');
				}
				let result = iface.ctrl("WPS_CANCEL");
				return result && match(result, /^OK\n?$/) ? 0 : libubus.STATUS_INVALID_ARGUMENT;
			},
		},
		control: {
			args: {
				command: ""
			},
			call: (req) => {
				let iface = wpas.interfaces[ifname];
				if (!iface)
					return libubus.STATUS_NOT_FOUND;

				return {
					result: iface.ctrl(req.args.command)
				};
			},
		},
	});
	wpas.data.iface_ubus[ifname] = obj;
}

return {
	shutdown: function() {
		config_clear();
		wpas.udebug_set(null);
		wpas.data.ubus.disconnect();
	},
	bss_allowed: function(ifname, bss) {
		let phy = wpas.data.config[wpas.data.iface_phy[ifname]];
		let transition = phy?.transition;
		if (transition?.phase == 'paused')
			return false;
		if (transition?.phase == 'resuming' && transition.frequency > 0 &&
		    bss.freq != transition.frequency)
			return false;
		let mld = wpas.data.mld[ifname];
		if (!mld)
			return true;

		return mld_bss_allowed(mld, bss);
	},
	iface_add: function(name, obj) {
		iface_ubus_add(name);
		iface_event("add", name);
	},
	iface_remove: function(name, obj) {
		enrollment_forget(name, wpas.data.enrollment[name]?.publication_unconfirmed ?
			'unconfirmed' : 'cancelled');
		iface_event("remove", name);
		iface_ubus_remove(name);
		delete wpas.data.dpp_hooks[name];
	},
	ctrl_event: function(name, iface, ev, run) {
		iface_ubus_notify(name, ev);
		if (match(ev, /^WPS-(CANCEL|FAIL|TIMEOUT|OVERLAP-DETECTED)( |$)/)) {
			let binding = enrollment_owner(name, iface);
			let entry = wpas.data.enrollment[name];
			if (!binding || !run || run != iface.status().wps_run)
				return;
			let owned = entry && entry.iface == iface && entry.owner == binding.owner &&
				entry.generation == binding.phy.generation && entry.run == run;
			if (entry && !owned)
				return;
			let starting = binding.owner.starting_wps_run;
			let active = binding.owner.active_wps_iface == iface &&
				binding.owner.active_wps_run == run;
			if (!owned && !active && !(starting != null && starting != run))
				return;
			binding.owner.cancelled_wps_run = run;
			delete binding.owner.active_wps_run;
			delete binding.owner.active_wps_iface;
			if (owned)
				enrollment_forget(name, 'cancelled');
			else
				enrollment_result(binding.owner, 'cancelled');
		}

		// The set of links of an associated MLD can change without a state
		// transition, which would leave the AP side following a link that no
		// longer exists, or ignoring one that just appeared.
		if (index(ev, "CTRL-EVENT-LINK-RECONFIG") != 0)
			return;

		try {
			iface_hostapd_notify(name, iface, "COMPLETED");
		} catch (e) {
			wpas.printf(`Error handling link reconfiguration: ${e}`);
		}
	},
	state: function(ifname, iface, state) {
		let event_data = iface.status();
		event_data.name = ifname;
		iface_event("state", ifname, event_data);
		try {
			let transition_phy = wpas.data.config[wpas.data.iface_phy[ifname]];
			if (state == 'INTERFACE_DISABLED' && transition_phy)
				phy_transition_ready(transition_phy);
			enrollment_persist(ifname, iface);
			iface_hostapd_notify(ifname, iface, state);

			if (state != "COMPLETED")
				return;

			let phy = wpas.data.iface_phy[ifname];
			if (!phy)
				return;

			let phy_data = wpas.data.config[phy];
			if (!phy_data)
				return;

			let iface_data = phy_data.data[ifname];
			if (!iface_data)
				return;

			let wdev_config = iface_data.config;
			if (!wdev_config || wdev_config.mode != "mesh")
				return;

			wdev_set_mesh_params(ifname, wdev_config);
		} catch (e) {
			ex_handler(e);
		}
	},
	event: function(ifname, iface, ev, info) {
		if (ev == "CH_SWITCH_STARTED" || ev == "LINK_CH_SWITCH_STARTED")
			iface_channel_switch(ifname, iface, info);
	},
	wps_credentials: function(ifname, iface, cred) {
		enrollment_candidate(ifname, iface, cred);
		cred.ifname = ifname;
		ubus.event("wps_credentials", cred);
	},
	dpp_rx_action: function(iface, src, frame_type, freq, frame) {
		let response = dpp_rx_via_channel(iface, "rx_action", {
			ifname: iface, src, frame_type, freq, frame,
		});
		if (response && response.handled)
			return true;
		return false;
	},
	dpp_rx_gas: function(iface, src, freq, gas_frame) {
		let response = dpp_rx_via_channel(iface, "rx_gas", {
			ifname: iface, src, freq, gas_frame,
		});
		if (response && response.handled)
			return true;
		return false;
	},
	wps_m8_rx: function(ifname, iface, data) {
		let response = dpp_rx_via_channel(ifname, "wps_m8_rx", {
			ifname, data,
		});
		if (response && response.handled)
			return true;
		return false;
	},
};
