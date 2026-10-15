// SPDX-License-Identifier: GPL-2.0-only
/* GRX500 EVA platform and firmware-owned secondary startup. */
#include <linux/clk.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/irqchip.h>
#include <linux/mfd/syscon.h>
#include <linux/of_address.h>
#include <linux/of_clk.h>
#include <linux/of_fdt.h>
#include <linux/of_platform.h>
#include <linux/regmap.h>
#include <asm/bootinfo.h>
#include <asm/irq_cpu.h>
#include <asm/mips-cps.h>
#include <asm/prom.h>
#include <asm/time.h>
#include <asm/smp-ops.h>

unsigned long grx500_cpu_hz;
#ifdef CONFIG_SMP
extern const struct plat_smp_ops grx500_smp_ops;
#endif

const char *get_system_type(void)
{
	return "Lantiq GRX500";
}

phys_addr_t mips_cm_phys_base(void)
{
	return 0x12300000;
}

void __init prom_init(void)
{
	/* The appended DT owns boot arguments after the EVA transition. */
#ifdef CONFIG_SMP
	register_smp_ops(&grx500_smp_ops);
#endif
}

void __init plat_mem_setup(void)
{
	set_io_port_base(0xa0000000);
	if (!get_fdt())
		panic("GRX500: no appended device tree");
	__dt_setup_arch(get_fdt());
}

/*
 * The SoC registers use CPU-native words. GRX500 keeps SWAP_IO_SPACE disabled
 * for its existing peripheral accessors, so regmap-mmio's big-endian path
 * would byte-swap an already native value. Share maps with every syscon consumer.
 */
static int grx500_syscon_read(void *context, unsigned int reg, unsigned int *val)
{
	void __iomem *base = (__force void __iomem *)context;

	*val = __raw_readl(base + reg);
	return 0;
}

static int grx500_syscon_write(void *context, unsigned int reg, unsigned int val)
{
	void __iomem *base = (__force void __iomem *)context;

	__raw_writel(val, base + reg);
	wmb();
	return 0;
}

static void __init grx500_syscon_init(const char *compatible)
{
	struct regmap_config config = {
		.reg_bits = 32,
		.val_bits = 32,
		.reg_stride = 4,
		.fast_io = true,
		.reg_read = grx500_syscon_read,
		.reg_write = grx500_syscon_write,
	};
	struct device_node *np;
	struct resource res;
	struct regmap *map;
	void __iomem *base;
	int ret;

	np = of_find_compatible_node(NULL, NULL, compatible);
	if (!np || of_address_to_resource(np, 0, &res))
		panic("GRX500: missing %s registers", compatible);
	base = ioremap(res.start, resource_size(&res));
	if (!base)
		panic("GRX500: cannot map %s registers", compatible);
	config.max_register = resource_size(&res) - 4;
	map = regmap_init(NULL, NULL, (__force void *)base, &config);
	if (IS_ERR(map))
		panic("GRX500: cannot create %s regmap: %ld", compatible, PTR_ERR(map));
	ret = of_syscon_register_regmap(np, map);
	if (ret)
		panic("GRX500: cannot register %s regmap: %d", compatible, ret);
	/* Keep the node reference and mapping for the registered syscon's lifetime. */
}

void __init plat_time_init(void)
{
	struct device_node *np;
	struct clk *cpu;
	u32 resolution;

	grx500_syscon_init("lantiq,grx500-cgu");
	grx500_syscon_init("lantiq,grx500-rcu");
	of_clk_init(NULL);
	np = of_get_cpu_node(0, NULL);
	cpu = of_clk_get(np, 0);
	of_node_put(np);
	if (IS_ERR(cpu))
		panic("GRX500: CPU clock unavailable");
	grx500_cpu_hz = clk_get_rate(cpu);
	clk_put(cpu);
	asm volatile("rdhwr %0, $3" : "=r" (resolution));
	if (!resolution || grx500_cpu_hz < 100000000 || grx500_cpu_hz > 1200000000)
		panic("GRX500: invalid CPU clock or counter resolution");
	mips_hpt_frequency = grx500_cpu_hz / resolution;
	write_c0_compare(read_c0_count());
	pr_info("GRX500: CPU %lu Hz, Count %u Hz\n", grx500_cpu_hz, mips_hpt_frequency);
}

void __init arch_init_irq(void)
{
	irqchip_init();
}

unsigned int get_c0_compare_int(void)
{
	return gic_get_c0_compare_int();
}

int get_c0_perfcount_int(void)
{
	return gic_get_c0_perfcount_int();
}

static int __init grx500_populate(void)
{
	return of_platform_default_populate(NULL, NULL, NULL);
}
arch_initcall(grx500_populate);
