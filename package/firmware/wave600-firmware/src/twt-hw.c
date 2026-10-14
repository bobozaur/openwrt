/* SPDX-License-Identifier: GPL-2.0-only */
#include "twt-hw.h"
#include "twt-limits.h"

#define TWT_TEXT __attribute__((section(".twt.text")))
#define TWT_SELECTOR_BASE 0xa0938000U
#define TWT_BAND_SHIFT 22U
#define TWT_GROUP_UPDATE 0x350U
#define TWT_GROUP_START 0x354U
#define TWT_GROUP_LOW 0x358U
#define TWT_MEMBER_UPDATE 0x35cU
#define TWT_GROUP_VALID 0x360U
#define TWT_GROUP_DEACTIVATE 0x364U
#define TWT_DONE (1U << 31)

/* The exact A0 UMAC primitive reads the native PAC TSF low word. Preserve the
 * reference calendar's 55/5 tick deadlines and unsigned wrap arithmetic.
 */
extern unsigned native_tsf32(void);
extern void native_critical_enter(unsigned *saved);
extern void native_critical_leave(unsigned saved);

_Static_assert(sizeof(unsigned) == 4, "native MMIO words must be 32 bits");
_Static_assert(sizeof(void *) == 4, "native firmware addresses must be 32 bits");

static inline volatile unsigned *twt_register(unsigned band, unsigned offset)
{
	return (volatile unsigned *)(TWT_SELECTOR_BASE +
				    (band << TWT_BAND_SHIFT) + offset);
}

static TWT_TEXT int twt_wait(volatile unsigned *reg, unsigned timeout)
{
	unsigned start = native_tsf32();

	while ((unsigned)(native_tsf32() - start) < timeout) {
		if (*reg & TWT_DONE)
			return WAVE_TWT_HW_OK;
	}
	return WAVE_TWT_HW_TIMEOUT;
}

TWT_TEXT int wave_twt_hw_program(unsigned band, unsigned group,
				 unsigned start_tsf, unsigned low_phase,
				 unsigned high_phase)
{
	volatile unsigned *update;

	if (band >= WAVE_TWT_BANDS || group >= WAVE_TWT_GROUPS ||
	    low_phase > WAVE_TWT_PHASE_MAX || high_phase > WAVE_TWT_PHASE_MAX)
		return WAVE_TWT_HW_INVALID;

	/* The command word is last: writing it starts the calendar update. */
	*twt_register(band, TWT_GROUP_LOW) = low_phase;
	*twt_register(band, TWT_GROUP_START) = start_tsf;
	update = twt_register(band, TWT_GROUP_UPDATE);
	*update = high_phase | (group << 24);
	return twt_wait(update, 55);
}

TWT_TEXT int wave_twt_hw_membership(unsigned band, unsigned group,
				    unsigned sid, unsigned present)
{
	volatile unsigned *update;
	int result;

	if (band >= WAVE_TWT_BANDS || group >= WAVE_TWT_GROUPS ||
	    sid >= WAVE_TWT_STATIONS || present > 1)
		return WAVE_TWT_HW_INVALID;
	if (present) {
		/* A member command can take effect even if its completion times
		 * out. Publish the limit first and retain it through recovery.
		 */
		result = wave_twt_limits_set(sid, 1);
		if (result)
			return result;
	}

	update = twt_register(band, TWT_MEMBER_UPDATE);
	*update = group | (sid << 8) | (present << 16);
	return twt_wait(update, 5);
}

TWT_TEXT int wave_twt_hw_deactivate(unsigned band, unsigned group)
{
	volatile unsigned *update;

	if (band >= WAVE_TWT_BANDS || group >= WAVE_TWT_GROUPS)
		return WAVE_TWT_HW_INVALID;

	update = twt_register(band, TWT_GROUP_DEACTIVATE);
	*update = group;
	return twt_wait(update, 55);
}

TWT_TEXT int wave_twt_hw_station(unsigned band, unsigned sid,
				 unsigned enabled, unsigned unannounced)
{
	unsigned saved;

	if (band >= WAVE_TWT_BANDS || sid >= WAVE_TWT_STATIONS ||
	    enabled > 1 || unannounced > 1)
		return WAVE_TWT_HW_INVALID;

	/* UMAC owns bitmap master1. Keep each data/command pair atomic with
	 * respect to its other native users. B selects announced type and D
	 * availability; both fields have station rather than TID resolution.
	 */
	native_critical_enter(&saved);
	if (enabled) {
		*twt_register(band, 0x10) = (!unannounced) << 18;
		*twt_register(band, 0x0c) = sid | (0xbU << 20);
	}
	*twt_register(band, 0x10) = enabled << 18;
	*twt_register(band, 0x0c) = sid | (0xdU << 20);
	native_critical_leave(saved);
	return enabled ? WAVE_TWT_HW_OK : wave_twt_limits_set(sid, 0);
}

TWT_TEXT int wave_twt_hw_reset(unsigned band)
{
	unsigned group, active;
	int result;

	if (band >= WAVE_TWT_BANDS)
		return WAVE_TWT_HW_INVALID;

	active = *twt_register(band, TWT_GROUP_VALID);
	for (group = 0; group < WAVE_TWT_GROUPS; group++) {
		if (!(active & (1U << group)))
			continue;
		result = wave_twt_hw_deactivate(band, group);
		if (result != WAVE_TWT_HW_OK)
			return result;
	}
	if (*twt_register(band, TWT_GROUP_VALID))
		return WAVE_TWT_HW_UNCERTAIN;
	return WAVE_TWT_HW_OK;
}

TWT_TEXT int wave_twt_hw_quiesce(unsigned band)
{
	int result;

	if (band >= WAVE_TWT_BANDS)
		return WAVE_TWT_HW_INVALID;
	result = twt_wait(twt_register(band, TWT_GROUP_UPDATE), 55);
	if (result)
		return result;
	result = twt_wait(twt_register(band, TWT_MEMBER_UPDATE), 5);
	if (result)
		return result;
	return twt_wait(twt_register(band, TWT_GROUP_DEACTIVATE), 55);
}

TWT_TEXT int wave_twt_hw_tsf(unsigned band, unsigned *low, unsigned *high)
{
	volatile unsigned *port;
	unsigned hi, lo, attempt;

	if (band >= WAVE_TWT_BANDS)
		return WAVE_TWT_HW_INVALID;
	port = (volatile unsigned *)(0xa0900760U + (band << TWT_BAND_SHIFT));
	/* Native 64-bit sampling uses high/low/high. One retry handles the
	 * low-word rollover; repeated changes leave the sample unqualified.
	 */
	for (attempt = 0; attempt < 2; attempt++) {
		hi = port[1];
		lo = port[0];
		if (hi == port[1]) {
			*low = lo;
			*high = hi;
			return WAVE_TWT_HW_OK;
		}
	}
	return WAVE_TWT_HW_UNCERTAIN;
}
