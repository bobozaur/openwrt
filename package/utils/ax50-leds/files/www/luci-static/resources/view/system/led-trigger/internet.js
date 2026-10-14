'use strict';
'require baseclass';

return baseclass.extend({
	trigger: _('Internet reachability'),
	description: _('Use on both Internet LEDs: blue when a configured uplink reaches a probe target, orange when all fail, and off while status is unknown. Configure probe targets and uplinks in /etc/config/pingcheck.'),
	kernel: false,
	addFormOptions: function(s) {}
});
