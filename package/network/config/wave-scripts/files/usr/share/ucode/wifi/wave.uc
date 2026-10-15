// SPDX-License-Identifier: GPL-2.0-only
'use strict';

import * as fs from 'fs';

// The path suffix is a hardware radio ID, independent of wiphy enumeration.
function phy_info(phy) {
	let dir = `/sys/class/ieee80211/${phy}`;
	let id = trim(fs.readfile(`${dir}/wave_radio_id`) ?? '');
	let role = trim(fs.readfile(`${dir}/wave_role`) ?? '');
	if (!match(id, /^[0-9]+$/) || (role != 'ap' && role != 'sta'))
		return null;

	let path = fs.realpath(`${dir}/device`);
	if (!path)
		return null;
	path = replace(path, /^\/sys\/devices\//, '');
	if (match(path, /^platform\/.*\/pci/))
		path = replace(path, /^platform\//, '');

	return {
		phy, role,
		path: `${path}+wave${id}`,
		master_ifindex: int(fs.readfile(`${dir}/wave_master_ifindex`)),
	};
};

function find_radio(config) {
	let selected = config.phy ? phy_info(config.phy) : null;
	let paths = config.path ?? selected?.path;
	paths = type(paths) == 'array' ? paths : [ paths ];
	let radio = {};
	for (let phy in fs.lsdir('/sys/class/ieee80211')) {
		let info = phy_info(phy);
		if (!info || index(paths, info.path) < 0)
			continue;
		if (radio[info.role])
			return null;
		radio[info.role] = info;
	}
	if (!radio.ap || !radio.sta || !radio.ap.master_ifindex ||
	    radio.ap.master_ifindex != radio.sta.master_ifindex)
		return null;
	for (let name in fs.lsdir('/sys/class/net'))
		if (int(fs.readfile(`/sys/class/net/${name}/ifindex`)) == radio.ap.master_ifindex)
			radio.master = name;
	return radio.master ? radio : null;
};

// Factory radio addresses use the OEM lower-24-bit offsets, preserving OUI.
// Explicit UCI MAC and random-address options remain owned by wifi.iface.
function board_mac(band) {
	let board = json(fs.readfile('/etc/board.json'));
	let mac = board?.system?.label_macaddr;
	if (!match(mac ?? '', /^[0-9a-fA-F]{2}(:[0-9a-fA-F]{2}){5}$/))
		return null;
	let bytes = split(mac, ':');
	let suffix = (hex(join('', slice(bytes, 3))) + (band == '2g' ? -1 : -2)) & 0xffffff;
	return lc(join(':', slice(bytes, 0, 3))) + sprintf(':%02x:%02x:%02x', suffix >> 16, (suffix >> 8) & 255, suffix & 255);
};

// Recognize the saved physical identity even before both role wiphys appear.
function matches(config) {
	if (config.phy && phy_info(config.phy))
		return true;
	let paths = config.path;
	paths = type(paths) == 'array' ? paths : [ paths ];
	return length(filter(paths, (path) => match(path ?? '', /\+wave[0-9]+$/))) > 0;
};

return { phy_info, find_radio, board_mac, matches };
