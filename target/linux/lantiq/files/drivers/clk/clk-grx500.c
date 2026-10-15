// SPDX-License-Identifier: GPL-2.0-only
/* GRX500 CGU: native-endian registers with atomic clock gate set/clear. */
#include <linux/clk-provider.h>
#include <linux/delay.h>
#include <linux/mfd/syscon.h>
#include <linux/of.h>
#include <linux/regmap.h>
#include <linux/slab.h>
#include <dt-bindings/clock/lantiq,grx500-cgu.h>

struct grx500_clk {
	struct clk_hw hw;
	struct regmap *map;
	u32 status, mask;
};

#define to_grx500_clk(_hw) container_of(_hw, struct grx500_clk, hw)

static int grx500_gate_enable(struct clk_hw *hw)
{
	struct grx500_clk *clk = to_grx500_clk(hw);
	u32 value;
	int ret;

	ret = regmap_write(clk->map, clk->status + 4, clk->mask);
	if (ret)
		return ret;
	return regmap_read_poll_timeout_atomic(clk->map, clk->status, value,
					      value & clk->mask, 1, 100000);
}

static void grx500_gate_disable(struct clk_hw *hw)
{
	struct grx500_clk *clk = to_grx500_clk(hw);

	regmap_write(clk->map, clk->status + 8, clk->mask);
}

static int grx500_gate_is_enabled(struct clk_hw *hw)
{
	struct grx500_clk *clk = to_grx500_clk(hw);
	u32 value;

	if (regmap_read(clk->map, clk->status, &value))
		return 0;
	return !!(value & clk->mask);
}

static const struct clk_ops grx500_gate_ops = {
	.enable = grx500_gate_enable,
	.disable = grx500_gate_disable,
	.is_enabled = grx500_gate_is_enabled,
};

static int grx500_pcie_pll_prepare(struct clk_hw *hw)
{
	struct regmap *map = to_grx500_clk(hw)->map;
	u32 value;
	int ret, i;

	/* Both consumers must hold their PHY PIPEs in powerdown during this
	 * shared 100 MHz spread-spectrum PLL initialization.
	 */
	regmap_write(map, 0x98, 0x10003004);
	regmap_write(map, 0x98, 0x10103004);
	regmap_write(map, 0x94, 0x00000190);
	regmap_write(map, 0x94, 0x00000191);
	ret = regmap_read_poll_timeout(map, 0x94, value, value & BIT(1), 1000, 100000);
	if (ret)
		return ret;
	for (i = 0; i < 8; i++)
		regmap_write(map, 0xa4 + i * 4, i < 4 ? 0xff60 : 0xa0);
	regmap_write(map, 0x9c, 0xff1c);
	regmap_write(map, 0x9c, 0xff1f);
	regmap_write(map, 0x9c, 0xff1c);
	regmap_write(map, 0x9c, 0xff1d);
	msleep(1);
	return 0;
}

static unsigned long grx500_pcie_pll_rate(struct clk_hw *hw, unsigned long parent)
{
	return 100000000;
}

static const struct clk_ops grx500_pcie_pll_ops = {
	.prepare = grx500_pcie_pll_prepare,
	.recalc_rate = grx500_pcie_pll_rate,
};

static void __init grx500_cgu_init(struct device_node *np)
{
	static const u8 dividers[] = { 1, 2, 3, 4, 5, 6, 8, 10,
				     12, 16, 20, 24, 32, 40, 48, 64 };
	static const struct {
		const char *name;
		u32 status, bit;
	} gates[] = {
		{ "grx500-usb0", 0x120, 6 },
		{ "grx500-usb1", 0x120, 27 },
		{ "grx500-pcie0", 0x130, 1 },
		{ "grx500-pcie1", 0x130, 17 },
		{ "grx500-pcie-pll", 0, 0 },
	};
	struct clk_hw_onecell_data *data;
	struct regmap *map;
	u32 cfg;
	unsigned long pll;
	int i, ret;

	map = syscon_node_to_regmap(np);
	if (IS_ERR(map))
		panic("GRX500: cannot map CGU");
	data = kzalloc(struct_size(data, hws, GRX500_CLK_COUNT), GFP_KERNEL);
	if (!data)
		panic("GRX500: cannot allocate clocks");
	data->num = GRX500_CLK_COUNT;
	regmap_read(map, 0x08, &cfg);
	pll = cfg & BIT(29) ? 2000000000UL : 2400000000UL;
	if (cfg & BIT(29))
		regmap_read(map, 0x38, &cfg);
	data->hws[GRX500_CLK_CPU] = clk_hw_register_fixed_rate(NULL, "grx500-cpu",
		NULL, 0, pll / dividers[(cfg >> 12) & 15]);
	data->hws[GRX500_CLK_FPI] = clk_hw_register_fixed_rate(NULL, "grx500-fpi",
		NULL, 0, 200000000);
	for (i = 0; i < ARRAY_SIZE(gates); i++) {
		struct clk_init_data init = {
			.name = gates[i].name,
			.ops = gates[i].status ? &grx500_gate_ops : &grx500_pcie_pll_ops,
		};
		struct grx500_clk *clk = kzalloc(sizeof(*clk), GFP_KERNEL);

		if (!clk)
			panic("GRX500: cannot allocate clock");
		clk->map = map;
		clk->status = gates[i].status;
		clk->mask = BIT(gates[i].bit);
		clk->hw.init = &init;
		ret = clk_hw_register(NULL, &clk->hw);
		if (ret)
			panic("GRX500: cannot register clock: %d", ret);
		data->hws[GRX500_CLK_USB0 + i] = &clk->hw;
	}
	for (i = 0; i < GRX500_CLK_COUNT; i++)
		if (IS_ERR(data->hws[i]))
			panic("GRX500: invalid clock %d", i);
	if (of_clk_add_hw_provider(np, of_clk_hw_onecell_get, data))
		panic("GRX500: cannot register CGU provider");
}
CLK_OF_DECLARE(grx500_cgu, "lantiq,grx500-cgu", grx500_cgu_init);
