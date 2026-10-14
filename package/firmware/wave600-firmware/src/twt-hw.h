/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef WAVE_TWT_HW_H
#define WAVE_TWT_HW_H

#define WAVE_TWT_BANDS 2U
#define WAVE_TWT_GROUPS 32U
#define WAVE_TWT_STATIONS 256U
#define WAVE_TWT_PHASE_MAX 0x7ffffU

enum wave_twt_hw_result {
	WAVE_TWT_HW_OK = 0,
	WAVE_TWT_HW_INVALID = -1,
	WAVE_TWT_HW_TIMEOUT = -2,
	WAVE_TWT_HW_UNCERTAIN = -3,
};

/* Phase values use the native calendar's approximately 32-us units. The
 * caller owns negotiation, TSF conversion, serialization and group lifetime.
 * A timeout leaves hardware state uncertain: it must fence that owner rather
 * than clear/reuse its software record or publish a successful agreement.
 */
int wave_twt_hw_program(unsigned band, unsigned group, unsigned start_tsf,
			unsigned low_phase, unsigned high_phase);
/* Member addition first acquires the station's native-limit overlay. A
 * failed/uncertain addition retains it until the lifecycle disables the
 * station after removing every possibly published membership.
 */
int wave_twt_hw_membership(unsigned band, unsigned group, unsigned sid,
			   unsigned present);
int wave_twt_hw_deactivate(unsigned band, unsigned group);
/* Disable only for the last member; success also restores current native
 * limits. A failure must retain the lifecycle record for recovery.
 */
int wave_twt_hw_station(unsigned band, unsigned sid, unsigned enabled,
			unsigned unannounced);
/* Recover after a timeout only once all three command ports have completed.
 * A still-busy port keeps the caller fenced; no command is overwritten.
 */
int wave_twt_hw_quiesce(unsigned band);
int wave_twt_hw_tsf(unsigned band, unsigned *low, unsigned *high);

/* Use only with ingress stopped and callbacks prevented from publishing
 * membership. The caller must retain pending allocation/TX ownership and
 * keep calendar records until every deactivation has completed. After an
 * uncertain command, quiesce first and remove the owned memberships.
 */
int wave_twt_hw_reset(unsigned band);

#endif
