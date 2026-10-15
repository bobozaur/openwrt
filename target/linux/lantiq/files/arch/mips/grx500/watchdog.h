/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef __GRX500_WATCHDOG_H
#define __GRX500_WATCHDOG_H

#include <linux/types.h>

/* Secondary launch is safe only while the boot watchdog is armed. */
bool grx500_watchdog_armed(void);

#endif
