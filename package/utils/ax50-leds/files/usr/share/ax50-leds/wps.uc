// SPDX-License-Identifier: GPL-2.0-only

// Native hostapd owns session start/stop. Durations here bound only the display
// of a completed result; age_ms prevents reconnects from extending that display.
export function display_state(snapshot)
{
	let chosen = null;
	let durations = { success: 300000, failure: 60000, timeout: 60000, overlap: 30000 };

	for (let bss in snapshot?.bss ?? []) {
		let duration = durations[bss.event];
		let valid = duration && type(bss.age_ms) == 'int' && bss.age_ms >= 0 && bss.age_ms < duration;

		// Another BSS may still be accepting an enrollee after this one ends.
		if (bss.active && !valid)
			return { mode: 'progress' };
		if (valid && (!chosen || bss.age_ms < chosen.age))
			chosen = { mode: bss.event, age: bss.age_ms, next: duration - bss.age_ms };
	}

	return chosen ?? { mode: 'normal' };
}

export function runtime_allowed(state, config)
{
	return state.trusted && state.enabled && !state.power_off && state.diagnostic == 'done' && state.policy == 'normal' &&
		config.valid && !config.off && !config.custom;
}
