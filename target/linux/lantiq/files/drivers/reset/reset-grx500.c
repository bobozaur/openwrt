// SPDX-License-Identifier: GPL-2.0-only
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/reset-controller.h>

struct grx500_reset {
	struct reset_controller_dev rcdev;
	struct regmap *map;
};

static int grx500_reset_update(struct reset_controller_dev *rcdev,
			       unsigned long id, bool assert)
{
	struct grx500_reset *reset = container_of(rcdev, struct grx500_reset, rcdev);
	u32 bit = BIT(id % 32);

	return regmap_update_bits(reset->map, id < 32 ? 0x10 : 0x48,
				  bit, assert ? bit : 0);
}

static int grx500_reset_assert(struct reset_controller_dev *rcdev, unsigned long id)
{
	return grx500_reset_update(rcdev, id, true);
}

static int grx500_reset_deassert(struct reset_controller_dev *rcdev, unsigned long id)
{
	return grx500_reset_update(rcdev, id, false);
}

static int grx500_reset_status(struct reset_controller_dev *rcdev, unsigned long id)
{
	struct grx500_reset *reset = container_of(rcdev, struct grx500_reset, rcdev);
	u32 value;
	int ret = regmap_read(reset->map, id < 32 ? 0x10 : 0x48, &value);

	return ret ? ret : !!(value & BIT(id % 32));
}

static const struct reset_control_ops grx500_reset_ops = {
	.assert = grx500_reset_assert,
	.deassert = grx500_reset_deassert,
	.status = grx500_reset_status,
};

static int grx500_reset_probe(struct platform_device *pdev)
{
	struct grx500_reset *reset;

	reset = devm_kzalloc(&pdev->dev, sizeof(*reset), GFP_KERNEL);
	if (!reset)
		return -ENOMEM;
	reset->map = syscon_node_to_regmap(pdev->dev.of_node);
	if (IS_ERR(reset->map))
		return PTR_ERR(reset->map);
	reset->rcdev.owner = THIS_MODULE;
	reset->rcdev.ops = &grx500_reset_ops;
	reset->rcdev.of_node = pdev->dev.of_node;
	reset->rcdev.nr_resets = 64;
	return devm_reset_controller_register(&pdev->dev, &reset->rcdev);
}

static const struct of_device_id grx500_reset_match[] = {
	{ .compatible = "lantiq,grx500-rcu" },
	{ }
};
MODULE_DEVICE_TABLE(of, grx500_reset_match);

static struct platform_driver grx500_reset_driver = {
	.probe = grx500_reset_probe,
	.driver = {
		.name = "grx500-reset",
		.of_match_table = grx500_reset_match,
	},
};
builtin_platform_driver(grx500_reset_driver);
