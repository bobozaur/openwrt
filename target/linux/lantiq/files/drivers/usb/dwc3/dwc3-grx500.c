// SPDX-License-Identifier: GPL-2.0-only
/* GRX500 preconditions for the upstream DWC3/xHCI drivers.
 * Reset sequence and register definitions follow the GPL Lantiq/Intel BSP.
 * The child exclusively owns the live controller after it is populated.
 */
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/clk.h>
#include <linux/mfd/syscon.h>
#include <linux/regmap.h>
#include <linux/reset.h>

#define RCU_USB0_CFG	0x80018
#define RCU_USB1_CFG	0x80034
#define AXI_DEVICE_BE	BIT(9)
#define AXI_HOST_BE	BIT(10)
#define GCTL		0xc110
#define GSNPSID		0xc120
#define GUSB2PHYCFG	0xc200
#define GUSB3PIPECTL	0xc2c0
#define DCTL		0xc704
#define CORE_RESET	BIT(11)
#define PHY_RESET	BIT(31)
#define DEVICE_RESET	BIT(30)
#define DIS_RXDET_P3	BIT(28)

struct grx500_dwc3 {
	struct clk *clock;
	struct reset_control *reset;
	struct regmap *rcu;
	struct gpio_desc *vbus;
	bool enabled;
};

static void grx500_dwc3_disable(struct grx500_dwc3 *usb)
{
	if (!usb->enabled)
		return;
	gpiod_set_value_cansleep(usb->vbus, 0);
	reset_control_assert(usb->reset);
	clk_disable_unprepare(usb->clock);
	usb->enabled = false;
}

static int grx500_dwc3_prepare(struct platform_device *pdev,
			      struct grx500_dwc3 *usb, u32 port)
{
	struct resource *res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "core");
	void __iomem *core;
	u32 value;
	int ret;

	if (!res || resource_size(res) < DCTL + 4)
		return -EINVAL;
	/* Do not reserve the child's register window. This temporary mapping is
	 * dropped before DWC3 binds and requests its own resources.
	 */
	core = ioremap(res->start, resource_size(res));
	if (!core)
		return -ENOMEM;
	ret = clk_prepare_enable(usb->clock);
	if (ret)
		goto out;
	usb->enabled = true;
	msleep(100);
	/* BSP AXI host endian selection keeps standard little-endian MMIO and
	 * descriptors usable on this big-endian CPU. No xHCI software swapping.
	 */
	regmap_update_bits(usb->rcu, port ? RCU_USB1_CFG : RCU_USB0_CFG,
			   AXI_DEVICE_BE | AXI_HOST_BE, AXI_HOST_BE);
	ret = reset_control_deassert(usb->reset);
	if (ret)
		goto out;
	msleep(100);
	value = readl(core + GSNPSID);
	if ((value & 0xffff0000) != 0x55330000) {
		dev_err(&pdev->dev, "invalid DWC3 identification: %08x\n", value);
		ret = -ENODEV;
		goto out;
	}
	writel((readl(core + DCTL) & ~BIT(31)) | DEVICE_RESET, core + DCTL);
	ret = readl_poll_timeout(core + DCTL, value, !(value & DEVICE_RESET), 10, 500000);
	if (ret)
		goto out;
	writel(readl(core + GCTL) | CORE_RESET, core + GCTL);
	writel(readl(core + GUSB3PIPECTL) | PHY_RESET | DIS_RXDET_P3, core + GUSB3PIPECTL);
	writel(readl(core + GUSB2PHYCFG) | PHY_RESET, core + GUSB2PHYCFG);
	msleep(100);
	writel(readl(core + GUSB3PIPECTL) & ~PHY_RESET, core + GUSB3PIPECTL);
	writel(readl(core + GUSB2PHYCFG) & ~PHY_RESET, core + GUSB2PHYCFG);
	msleep(100);
	writel(readl(core + GCTL) & ~CORE_RESET, core + GCTL);
	readl(core + GCTL);
	gpiod_set_value_cansleep(usb->vbus, 1);
	dev_info(&pdev->dev, "GRX500 USB%u prepared, DWC3 %08x\n", port,
		 readl(core + GSNPSID));
out:
	iounmap(core);
	return ret;
}

static int grx500_dwc3_probe(struct platform_device *pdev)
{
	struct grx500_dwc3 *usb;
	struct platform_device *child;
	struct device_node *node;
	u32 port;
	int ret;

	ret = of_property_read_u32(pdev->dev.of_node, "lantiq,port", &port);
	if (ret || port > 1)
		return -EINVAL;
	usb = devm_kzalloc(&pdev->dev, sizeof(*usb), GFP_KERNEL);
	if (!usb)
		return -ENOMEM;
	usb->clock = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(usb->clock))
		return dev_err_probe(&pdev->dev, PTR_ERR(usb->clock), "clock unavailable\n");
	usb->reset = devm_reset_control_get_exclusive(&pdev->dev, NULL);
	if (IS_ERR(usb->reset))
		return dev_err_probe(&pdev->dev, PTR_ERR(usb->reset), "reset unavailable\n");
	usb->rcu = syscon_regmap_lookup_by_phandle(pdev->dev.of_node, "lantiq,rcu");
	if (IS_ERR(usb->rcu))
		return PTR_ERR(usb->rcu);
	usb->vbus = devm_gpiod_get(&pdev->dev, "vbus", GPIOD_OUT_LOW);
	if (IS_ERR(usb->vbus))
		return dev_err_probe(&pdev->dev, PTR_ERR(usb->vbus), "VBUS GPIO unavailable\n");
	platform_set_drvdata(pdev, usb);
	ret = grx500_dwc3_prepare(pdev, usb, port);
	if (ret)
		goto disable;
	ret = of_platform_populate(pdev->dev.of_node, NULL, NULL, &pdev->dev);
	if (!ret) {
		/* Population can succeed even if a child device could not register.
		 * Check registration only: DWC3 itself may legitimately defer probe.
		 */
		node = of_get_compatible_child(pdev->dev.of_node, "snps,dwc3");
		child = node ? of_find_device_by_node(node) : NULL;
		of_node_put(node);
		if (child) {
			put_device(&child->dev);
			return 0;
		}
		ret = -ENODEV;
	}
	of_platform_depopulate(&pdev->dev);
disable:
	grx500_dwc3_disable(usb);
	return dev_err_probe(&pdev->dev, ret, "USB preparation failed\n");
}

static void grx500_dwc3_remove(struct platform_device *pdev)
{
	of_platform_depopulate(&pdev->dev);
	grx500_dwc3_disable(platform_get_drvdata(pdev));
}

static const struct of_device_id grx500_dwc3_match[] = {
	{ .compatible = "lantiq,grx500-dwc3" },
	{ }
};
MODULE_DEVICE_TABLE(of, grx500_dwc3_match);

static struct platform_driver grx500_dwc3_driver = {
	.probe = grx500_dwc3_probe,
	.remove = grx500_dwc3_remove,
	.driver = {
		.name = "grx500-dwc3",
		.of_match_table = grx500_dwc3_match,
	},
};
module_platform_driver(grx500_dwc3_driver);
MODULE_LICENSE("GPL");
