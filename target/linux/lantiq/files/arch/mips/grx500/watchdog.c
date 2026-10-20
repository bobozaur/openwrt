// SPDX-License-Identifier: GPL-2.0-only
/* The current-VPE GIC registers always belong to CPU0. Arm before secondary
 * startup. The watchdog core bridges the short hardware counter to the
 * userspace timeout, including procd's normal five-second keepalive interval.
 */
#include <linux/init.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/mfd/syscon.h>
#include <linux/regmap.h>
#include <linux/math64.h>
#include <linux/smp.h>
#include <linux/watchdog.h>
#include <asm/mips-cps.h>

#include "watchdog.h"

extern unsigned long grx500_cpu_hz;
static struct watchdog_device grx500_wdd;
static bool grx500_wd_ready;

bool grx500_watchdog_armed(void)
{
	return grx500_wd_ready;
}
#define WD_CONFIG 0x8090
#define WD_COUNT 0x8094
#define WD_INITIAL 0x8098
#define WD_MODE (BIT(1) | BIT(5))

enum grx500_wd_action { WD_PING, WD_STOP, WD_READ };
struct grx500_wd_call {
	enum grx500_wd_action action;
	u32 count;
};

static void grx500_wd_cpu0(void *argument)
{
	struct grx500_wd_call *call = argument;

	if (call->action == WD_READ) {
		call->count = __raw_readl(mips_gic_base + WD_COUNT);
		return;
	}
	__raw_writel(WD_MODE, mips_gic_base + WD_CONFIG);
	if (call->action == WD_PING) {
		wmb();
		__raw_writel(call->count, mips_gic_base + WD_INITIAL);
		wmb();
		__raw_writel(WD_MODE | BIT(0), mips_gic_base + WD_CONFIG);
	}
	wmb();
}

static int grx500_wd_call(enum grx500_wd_action action, u32 *count)
{
	struct grx500_wd_call call = { .action = action, .count = count ? *count : 0 };
	int ret = smp_call_function_single(0, grx500_wd_cpu0, &call, 1);

	if (!ret && count)
		*count = call.count;
	return ret;
}

static int grx500_ping(struct watchdog_device *wdd)
{
	u32 count = min_t(u64, (u64)wdd->timeout * grx500_cpu_hz, U32_MAX);

	return grx500_wd_call(WD_PING, &count);
}

static int grx500_stop(struct watchdog_device *wdd)
{
	/* Used on registration failure; nowayout prevents a userspace stop. */
	return grx500_wd_call(WD_STOP, NULL);
}

static int grx500_timeout(struct watchdog_device *wdd, unsigned int timeout)
{
	wdd->timeout = timeout;
	return grx500_ping(wdd);
}

static unsigned int grx500_timeleft(struct watchdog_device *wdd)
{
	u32 count = 0;

	if (grx500_wd_call(WD_READ, &count))
		return 0;
	return count / grx500_cpu_hz;
}

static const struct watchdog_info grx500_info = {
	.options = WDIOF_SETTIMEOUT | WDIOF_KEEPALIVEPING,
	.identity = "GRX500 GIC watchdog",
};
static const struct watchdog_ops grx500_ops = {
	.owner = THIS_MODULE,
	.start = grx500_ping,
	.stop = grx500_stop,
	.ping = grx500_ping,
	.set_timeout = grx500_timeout,
	.get_timeleft = grx500_timeleft,
};

static int __init grx500_watchdog_init(void)
{
	struct regmap *rcu;
	int ret;
	u32 route;

	/* CPU0 owns the reset timer and must remain online. */
	BUILD_BUG_ON(IS_ENABLED(CONFIG_HOTPLUG_CPU));
	if (smp_processor_id() != 0 || !mips_gic_base || !grx500_cpu_hz)
		return -ENODEV;
	if (__raw_readl(mips_gic_base + WD_CONFIG) & BIT(0))
		return -EBUSY;
	rcu = syscon_regmap_lookup_by_compatible("lantiq,grx500-rcu");
	if (IS_ERR(rcu))
		return PTR_ERR(rcu);
	ret = regmap_read(rcu, 0x50, &route);
	if (ret)
		return ret;
	ret = regmap_update_bits(rcu, 0x50, BIT(0), BIT(0));
	if (ret)
		return ret;
	/* Mask the first-stage warning IRQ; the second stage resets the SoC. */
	__raw_writel(BIT(0), mips_gic_base + 0x800c);
	grx500_wdd.info = &grx500_info;
	grx500_wdd.ops = &grx500_ops;
	grx500_wdd.min_timeout = 1;
	grx500_wdd.max_hw_heartbeat_ms = div_u64((u64)U32_MAX * 1000,
						grx500_cpu_hz);
	grx500_wdd.timeout = 30;
	watchdog_set_nowayout(&grx500_wdd, true);
	/* Arm before smp_init, including a secondary stuck before cpu_running.
	 * Registration is deferred by the core until subsys_initcall_sync, where
	 * its worker takes over before NAND scan. The worker stops at the fixed
	 * CONFIG_WATCHDOG_OPEN_TIMEOUT if PID1 never opens /dev/watchdog.
	 */
	ret = grx500_ping(&grx500_wdd);
	if (ret)
		goto restore_route;
	set_bit(WDOG_HW_RUNNING, &grx500_wdd.status);
	ret = watchdog_register_device(&grx500_wdd);
	if (ret) {
		grx500_stop(&grx500_wdd);
		clear_bit(WDOG_HW_RUNNING, &grx500_wdd.status);
	}
	if (!ret) {
		grx500_wd_ready = true;
		return 0;
	}
restore_route:
	regmap_update_bits(rcu, 0x50, BIT(0), route & BIT(0));
	return ret;
}
early_initcall(grx500_watchdog_init);
