// SPDX-License-Identifier: GPL-2.0-only
/* Linux owns physical VPE0 and VPE2. The retained IBL starts VPE2; neither
 * its sibling VPE3 nor the vendor firmware's ownership is changed here.
 * Mailbox layout and launch protocol follow the GPL GRX500 VMB/IBL source.
 */
#include <linux/init.h>
#include <linux/io.h>
#include <linux/sched/task_stack.h>
#include <linux/smp.h>
#include <asm/cacheflush.h>
#include <asm/mips-cps.h>
#include <asm/smp-ops.h>
#include <asm/time.h>

#include "watchdog.h"

#define GRX500_IBL_PHYS 0x20000000
#define GRX500_IBL_CPU2_RESPONSE 0x160
#define GRX500_IBL_CPU2_COMMAND 0x168
#define GRX500_IBL_WAIT 8
#define GRX500_IBL_START 1
#define GRX500_IBL_IRQ 21

struct grx500_ibl_command {
	u32 id, pc, sp, gp, a0, eva, mt_group, yield, priv;
	u32 tc[4][8];
	u32 tc_count;
};

static void __iomem *grx500_ibl;
static bool grx500_secondary_dispatched;

static void __init grx500_smp_setup(void)
{
	unsigned int i;

	BUILD_BUG_ON(CONFIG_MIPS_NR_CPU_NR_MAP < 3);
	BUILD_BUG_ON(sizeof(struct grx500_ibl_command) != 168);
	BUILD_BUG_ON(IS_ENABLED(CONFIG_HOTPLUG_CPU));
	/* GIC IPIs are allocated before the secondary probes its CPU identity. */
	for (i = 0; i < CONFIG_MIPS_NR_CPU_NR_MAP; i++)
		__cpu_number_map[i] = -1;
	for (i = 0; i < NR_CPUS; i++)
		__cpu_logical_map[i] = -1;
	__cpu_number_map[0] = 0;
	__cpu_number_map[2] = 1;
	__cpu_logical_map[0] = 0;
	__cpu_logical_map[1] = 2;
	cpu_set_core(&cpu_data[0], 0);
	cpu_set_core(&cpu_data[1], 1);
	cpu_set_vpe_id(&cpu_data[0], 0);
	cpu_set_vpe_id(&cpu_data[1], 0);
	set_cpu_possible(0, true);
	set_cpu_possible(1, true);
	smp_num_siblings = 1;
}

static void __init grx500_prepare_cpus(unsigned int max_cpus)
{
	grx500_ibl = ioremap(GRX500_IBL_PHYS,
			   GRX500_IBL_CPU2_COMMAND + sizeof(struct grx500_ibl_command));
	if (!grx500_ibl)
		panic("GRX500: cannot map retained CPU boot mailbox");
}

static int grx500_boot_secondary(int cpu, struct task_struct *idle)
{
	struct grx500_ibl_command command = {};
	u32 pin;
	unsigned int i;

	if (cpu != 1 || grx500_secondary_dispatched || !grx500_watchdog_armed())
		return -EINVAL;
	if (!mips_cm_present() || !mips_gic_base || mips_cm_vp_id(cpu) != 2 ||
	    (read_c0_config() & CONF_CM_CMASK) != 5 ||
	    (read_gcr_cl_coherence() & 0x33) != 0x33)
		return -ENODEV;
	if (__raw_readl(grx500_ibl + GRX500_IBL_CPU2_RESPONSE) != GRX500_IBL_WAIT)
		return -EBUSY;
	pin = read_gic_map_pin(GRX500_IBL_IRQ);
	if (!(pin & GIC_MAP_PIN_MAP_TO_PIN) || (pin & GIC_MAP_PIN_MAP_TO_NMI))
		return -ENODEV;

	command.id = GRX500_IBL_START;
	command.pc = (unsigned long)CKSEG1ADDR(__pa_symbol(&smp_bootstrap));
	command.sp = __KSTK_TOS(idle);
	command.gp = (unsigned long)task_thread_info(idle);
	/* Command words are native-endian, matching the unchanged IBL reader. */
	for (i = 0; i < sizeof(command) / sizeof(u32); i++)
		__raw_writel(((u32 *)&command)[i],
			     grx500_ibl + GRX500_IBL_CPU2_COMMAND + i * sizeof(u32));

	/* The generic GIC setup reset trigger and mask state, but retained the
	 * IBL pin mapping. Restore only this firmware-owned wake interrupt.
	 */
	write_gic_rmask(GRX500_IBL_IRQ);
	change_gic_pol(GRX500_IBL_IRQ, GIC_POL_RISING_EDGE);
	change_gic_trig(GRX500_IBL_IRQ, GIC_TRIG_EDGE);
	change_gic_dual(GRX500_IBL_IRQ, GIC_DUAL_SINGLE);
	write_gic_map_vp(GRX500_IBL_IRQ, BIT(2));
	write_gic_wedge(GRX500_IBL_IRQ);
	write_gic_smask(GRX500_IBL_IRQ);
	wmb();
	grx500_secondary_dispatched = true;
	write_gic_wedge(GIC_WEDGE_RW | GRX500_IBL_IRQ);
	pr_info("GRX500: dispatched Linux CPU1 to physical VPE2; VPE3 retained\n");
	return 0;
}

static void grx500_init_secondary(void)
{
	if ((read_c0_ebase() & 0x3ff) != 2 ||
	    (read_c0_config() & CONF_CM_CMASK) != 5 ||
	    (read_gcr_cl_coherence() & 0x33) != 0x33)
		panic("GRX500: secondary CPU identity or coherence differs");
	cpu_set_core(&current_cpu_data, 1);
	cpu_set_vpe_id(&current_cpu_data, 0);
	change_c0_status(ST0_IM, STATUSF_IP2 | STATUSF_IP3 | STATUSF_IP4 |
			 STATUSF_IP5 | STATUSF_IP6 | STATUSF_IP7);
}

static void grx500_smp_finish(void)
{
	write_c0_compare(read_c0_count() + (8 * mips_hpt_frequency / HZ));
	local_irq_enable();
}

const struct plat_smp_ops grx500_smp_ops = {
	.send_ipi_single = mips_smp_send_ipi_single,
	.send_ipi_mask = mips_smp_send_ipi_mask,
	.init_secondary = grx500_init_secondary,
	.smp_finish = grx500_smp_finish,
	.boot_secondary = grx500_boot_secondary,
	.smp_setup = grx500_smp_setup,
	.prepare_cpus = grx500_prepare_cpus,
};
