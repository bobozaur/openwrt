/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef WAVE_TWT_LIMITS_H
#define WAVE_TWT_LIMITS_H

/* UMAC RX owner only. Enable before publishing any membership; disable
 * after removing the last membership and clearing station availability.
 * Repeated enables preserve the current native shadow. An uncertain member
 * command retains the clamp until the existing recovery owner removes it.
 */
int wave_twt_limits_set(unsigned sid, unsigned active);

#endif
