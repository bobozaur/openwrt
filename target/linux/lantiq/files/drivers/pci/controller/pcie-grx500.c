// SPDX-License-Identifier: GPL-2.0-only
/* GRX500 paired PCIe host controllers. Hardware protocol adapted from
 * TP-Link's GPL pcie-lantiq.c/.h and pcie-lantiq-phy.c:
 * Copyright (C) 2013 John Crispin
 * Copyright (C) 2009-2015 Lei Chuanhua, Lantiq
 * Copyright (C) 2016 Intel Corporation
 *
 * One owner prepares both root complexes before releasing the dual-PCI
 * WAVE device. DMA and interrupts belong to the main PCIe connection;
 * the auxiliary connection supplies the shared endpoint resources.
 */
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/pci.h>
#include <linux/platform_device.h>
#include <linux/sizes.h>
#include <linux/clk.h>
#include <linux/gpio/consumer.h>
#include <linux/mfd/syscon.h>
#include <linux/regmap.h>
#include <linux/reset.h>

#define PORTS		2
#define PHY_CFG4	0xc014
#define PHY_PIPE_PD	BIT(15)
#define RCU_ENDIAN	0x8004c
#define RCU_REFCLK	0x80120
#define APP_LTSSM	0x10
#define APP_DLL		0x14
#define APP_LINK		0x18
#define APP_AHB_SUPPRESS	0x78
#define APP_IRQ_ENABLE	0xf4
#define APP_IRQ_CLEAR	0xf8
#define APP_IRQ_CAPTURE	0xfc
#define APP_INTA		BIT(13)
#define ROOT_ID		0x00301bef
#define WAVE_ID		0x09d08086

struct grx500_pcie_port {
	void __iomem *csr, *config, *app, *phy;
	struct gpio_desc *perst;
	struct pci_host_bridge *bridge;
	struct resource memory, buses;
	spinlock_t lock;
	unsigned int index;
	int irq;
	bool radio_enabled;
	bool registered;
};

struct grx500_pcie {
	struct device *device;
	struct regmap *rcu;
	struct clk_bulk_data clocks[PORTS];
	struct clk *pll;
	struct reset_control *phy_resets;
	struct grx500_pcie_port ports[PORTS];
	bool pll_enabled;
	bool controllers_clocked;
};

static void grx500_write(void __iomem *base, unsigned int offset, u32 value)
{
	__raw_writel(value, base + offset);
	wmb();
}

static void grx500_update(void __iomem *base, unsigned int offset, u32 clear, u32 set)
{
	grx500_write(base, offset, (__raw_readl(base + offset) & ~clear) | set);
}

static bool grx500_link_up(struct grx500_pcie_port *port)
{
	return (__raw_readl(port->app + APP_LINK) & BIT(0)) &&
	       (__raw_readl(port->app + APP_DLL) & BIT(0));
}

static int grx500_config_address(struct pci_bus *bus, unsigned int devfn,
			       int where, int size, void __iomem **address)
{
	struct grx500_pcie_port *port = bus->sysdata;

	if ((size != 1 && size != 2 && size != 4) || where < 0 ||
	    where + size > PCI_CFG_SPACE_EXP_SIZE || (where & (size - 1)))
		return PCIBIOS_BAD_REGISTER_NUMBER;
	/* Both soldered endpoints are function0 on a point-to-point bus. Do not
	 * issue speculative transactions to absent devices or a down link.
	 */
	if (devfn || bus->number > 1 || (bus->number && !grx500_link_up(port)))
		return PCIBIOS_DEVICE_NOT_FOUND;
	*address = (bus->number ? port->config + (1 << 20) : port->csr) + (where & ~3);
	return PCIBIOS_SUCCESSFUL;
}

static int grx500_pci_read(struct pci_bus *bus, unsigned int devfn,
			 int where, int size, u32 *value)
{
	void __iomem *address;
	u32 data;
	int ret;

	*value = 0xffffffff;
	ret = grx500_config_address(bus, devfn, where, size, &address);
	if (ret)
		return ret;
	/* The board uses outbound_swap=0. Like the BSP, DBI and downstream
	 * config use native raw32 transactions, then PCI little-order fields.
	 */
	data = __raw_readl(address);
	/* GRX500 hardwires class0600 despite its type1 header. Match the BSP's
	 * early class fixup so generic PCI allocates the bridge window before
	 * its endpoint BAR. Otherwise the host-class bridge is skipped.
	 */
	if (!bus->number && (where & ~3) == PCI_CLASS_REVISION)
		data = (data & 0xffff) | (PCI_CLASS_BRIDGE_PCI << 16);
	data >>= (where & 3) * 8;
	*value = size == 1 ? data & 0xff : size == 2 ? data & 0xffff : data;
	return PCIBIOS_SUCCESSFUL;
}

static int grx500_pci_write(struct pci_bus *bus, unsigned int devfn,
			  int where, int size, u32 value)
{
	struct grx500_pcie_port *port = bus->sysdata;
	void __iomem *address;
	unsigned long flags;
	u32 data, mask, shift = (where & 3) * 8;
	int ret;

	ret = grx500_config_address(bus, devfn, where, size, &address);
	if (ret)
		return ret;
	spin_lock_irqsave(&port->lock, flags);
	if (size == 4) {
		data = value;
	} else {
		mask = (size == 1 ? 0xffU : 0xffffU) << shift;
		data = (__raw_readl(address) & ~mask) | ((value << shift) & mask);
		/* A command-only RMW must not acknowledge unrelated W1C status. */
		if ((where & ~3) == PCI_COMMAND && where < PCI_STATUS)
			data &= 0xffff;
	}
	if ((where & ~3) == PCI_COMMAND && !port->radio_enabled) {
		data |= PCI_COMMAND_INTX_DISABLE;
		if (bus->number)
			data &= ~PCI_COMMAND_MASTER;
	}
	__raw_writel(data, address);
	wmb();
	spin_unlock_irqrestore(&port->lock, flags);
	return PCIBIOS_SUCCESSFUL;
}

static struct pci_ops grx500_pci_ops = {
	.read = grx500_pci_read,
	.write = grx500_pci_write,
};

static int grx500_map_irq(const struct pci_dev *dev, u8 slot, u8 pin)
{
	struct grx500_pcie_port *port = dev->bus->sysdata;

	/* GRX500 A21 wires the main endpoint's level INTA directly to GIC136.
	 * The legacy BSP's +8 Linux IRQ offset is not part of the GIC ID.
	 */
	if (!port->radio_enabled || !dev->bus->number || dev->devfn || slot || pin != 1)
		return 0;
	return port->irq;
}

static int grx500_lcpll(struct grx500_pcie *pcie)
{
	unsigned int i;
	int ret;

	for (i = 0; i < PORTS; i++)
		grx500_update(pcie->ports[i].phy, PHY_CFG4, 0, PHY_PIPE_PD);
	msleep(1);
	ret = clk_prepare_enable(pcie->pll);
	if (ret)
		return ret;
	pcie->pll_enabled = true;
	for (i = 0; i < PORTS; i++)
		grx500_update(pcie->ports[i].phy, PHY_CFG4, PHY_PIPE_PD, 0);
	msleep(1);
	return 0;
}

static int grx500_root_setup(struct grx500_pcie *pcie, struct grx500_pcie_port *port)
{
	u32 id = __raw_readl(port->csr + PCI_VENDOR_ID);

	dev_info(pcie->device, "RC%u DBI ID=%08x\n", port->index, id);
	if (id != ROOT_ID)
		return -ENODEV;
	grx500_write(port->app, APP_IRQ_ENABLE, 0);
	grx500_write(port->app, APP_IRQ_CLEAR, 0xffffffff);
	grx500_write(port->app, APP_LTSSM, 0);
	grx500_write(port->app, APP_AHB_SUPPRESS, 1);
	grx500_write(port->csr, PCI_COMMAND,
		   port->radio_enabled ? 0 : PCI_COMMAND_INTX_DISABLE);
	/* Start disabled; generic PCI will size this for the actual endpoint.
	 * Preloading the whole48MiB aperture makes modern PCI retain that size
	 * and round it to64MiB for the32MiB BAR, which cannot fit the aperture.
	 */
	grx500_write(port->csr, PCI_MEMORY_BASE, 0x0000fff0);
	grx500_write(port->csr, PCI_PREF_MEMORY_BASE, 0x0000fff0);
	grx500_write(port->csr, PCI_PREF_BASE_UPPER32, 0);
	grx500_write(port->csr, PCI_PREF_LIMIT_UPPER32, 0);
	grx500_write(port->csr, PCI_IO_BASE, 0x00f0);
	grx500_write(port->csr, PCI_PRIMARY_BUS, 0x00010100);
	/* MSI, PME, AER and ASPM stay disabled during enumeration. */
	grx500_update(port->csr, 0x50, BIT(16) | GENMASK(22, 20), 0);
	grx500_update(port->csr, 0x44, BIT(8), BIT(3));
	grx500_update(port->csr, 0x74, GENMASK(11, 6) | 7, BIT(15));
	grx500_write(port->csr, 0x78, BIT(4) | BIT(11)); /* MPS/MRRS=128 */
	grx500_update(port->csr, 0x7c, GENMASK(14, 12), 3 << 12);
	grx500_update(port->csr, 0x80, BIT(4) | BIT(3) | 3, BIT(6) | BIT(28));
	grx500_update(port->csr, 0xa0, 0xf | BIT(5), 2); /* Gen2 target */
	grx500_write(port->csr, 0x108, 0xffffffff); /* Uncorrectable error mask */
	grx500_write(port->csr, 0x114, 0xffffffff); /* Correctable error mask */
	grx500_update(port->csr, 0x148, 0xf, 1);
	grx500_update(port->csr, 0x150, BIT(15), 0);
	grx500_update(port->csr, 0x80c, 0xff, BIT(17) | 127);
	grx500_update(port->csr, 0x70c, GENMASK(29, 8),
		    (127 << 8) | (127 << 16) | (7 << 24) | (5 << 27));
	grx500_update(port->csr, 0x710, 0, BIT(5));
	grx500_update(port->csr, 0x714, BIT(24) | BIT(25), 0);
	grx500_update(port->csr, 0x71c, BIT(15), BIT(31) | BIT(30));
	grx500_update(port->csr, 0x720, 0, BIT(0) | BIT(1));
	grx500_update(port->csr, 0x750, GENMASK(23, 21), 4 << 21);
	return 0;
}

static int grx500_prepare_links(struct grx500_pcie *pcie)
{
	u32 value;
	unsigned int i;
	int ret;

	/* GPIO descriptors assert both PERST lines before clock/PHY setup. */
	msleep(100);
	ret = clk_bulk_prepare_enable(PORTS, pcie->clocks);
	if (ret)
		return ret;
	pcie->controllers_clocked = true;
	for (i = 0; i < PORTS; i++) {
		grx500_write(pcie->ports[i].app, APP_LTSSM, 0);
		grx500_write(pcie->ports[i].app, APP_IRQ_ENABLE, 0);
		grx500_write(pcie->ports[i].app, APP_IRQ_CLEAR, 0xffffffff);
	}
	ret = grx500_lcpll(pcie);
	if (ret)
		return ret;
	ret = reset_control_assert(pcie->phy_resets);
	if (ret)
		return ret;
	udelay(1);
	ret = reset_control_deassert(pcie->phy_resets);
	if (ret)
		return ret;
	udelay(20);
	regmap_update_bits(pcie->rcu, RCU_ENDIAN, BIT(4) | BIT(5) | BIT(8) | BIT(9),
			   BIT(5) | BIT(9));
	regmap_update_bits(pcie->rcu, RCU_REFCLK, BIT(22) | BIT(23), 0);
	for (i = 0; i < PORTS; i++) {
		ret = grx500_root_setup(pcie, &pcie->ports[i]);
		if (ret)
			return ret;
	}
	msleep(200);
	/* Both RCs are ready before either side of the dual-PCI card is released. */
	for (i = 0; i < PORTS; i++)
		gpiod_set_value_cansleep(pcie->ports[i].perst, 0);
	for (i = 0; i < PORTS; i++)
		grx500_write(pcie->ports[i].app, APP_LTSSM, BIT(0));
	for (i = 0; i < PORTS; i++) {
		struct grx500_pcie_port *port = &pcie->ports[i];

		ret = read_poll_timeout(__raw_readl, value, value & BIT(0),
					100, 300000, false, port->app + APP_LINK);
		if (!ret)
			ret = read_poll_timeout(__raw_readl, value, value & BIT(0),
						10, 10000, false, port->app + APP_DLL);
		dev_info(pcie->device, "RC%u link=%08x DLL=%08x LCTLSTS=%08x\n", i,
			 __raw_readl(port->app + APP_LINK), __raw_readl(port->app + APP_DLL),
			 __raw_readl(port->csr + 0x80));
		if (ret || !(__raw_readl(port->csr + 0x80) & BIT(29)))
			return ret ? ret : -ENOLINK;
	}
	/* Conventional reset recovery before the first endpoint config read. */
	msleep(100);
	for (i = 0; i < PORTS; i++) {
		struct grx500_pcie_port *port = &pcie->ports[i];
		u32 id = __raw_readl(port->config + (1 << 20));

		dev_info(pcie->device, "RC%u endpoint ID=%08x\n", i, id);
		if (id != WAVE_ID)
			return -ENODEV;
		grx500_write(port->config + (1 << 20), PCI_COMMAND, PCI_COMMAND_INTX_DISABLE);
		if (__raw_readl(port->config + (1 << 20) + PCI_COMMAND) & PCI_COMMAND_MASTER)
			return -EIO;
	}
	return 0;
}

static void grx500_stop_links(struct grx500_pcie *pcie)
{
	unsigned int i;

	if (pcie->controllers_clocked)
		for (i = 0; i < PORTS; i++)
			grx500_write(pcie->ports[i].app, APP_IRQ_ENABLE, 0);
	for (i = 0; i < PORTS; i++)
		gpiod_set_value_cansleep(pcie->ports[i].perst, 1);
	if (pcie->controllers_clocked) {
		for (i = 0; i < PORTS; i++)
			grx500_write(pcie->ports[i].app, APP_LTSSM, 0);
		clk_bulk_disable_unprepare(PORTS, pcie->clocks);
		pcie->controllers_clocked = false;
	}
	if (pcie->pll_enabled) {
		clk_disable_unprepare(pcie->pll);
		pcie->pll_enabled = false;
	}
}

static void grx500_cleanup(void *data)
{
	struct grx500_pcie *pcie = data;
	unsigned int i;

	for (i = 0; i < PORTS; i++) {
		struct grx500_pcie_port *port = &pcie->ports[i];

		if (!port->bridge)
			continue;
		if (port->registered) {
			pci_lock_rescan_remove();
			pci_stop_root_bus(port->bridge->bus);
			grx500_write(port->app, APP_IRQ_ENABLE, 0);
			pci_remove_root_bus(port->bridge->bus);
			pci_unlock_rescan_remove();
		}
		pci_free_host_bridge(port->bridge);
		port->bridge = NULL;
	}
	grx500_stop_links(pcie);
}

static int grx500_pcie_probe(struct platform_device *pdev)
{
	static const char *const names[PORTS][4] = {
		{ "csr0", "config0", "app0", "phy0" },
		{ "csr1", "config1", "app1", "phy1" },
	};
	static const char *const perst_names[PORTS] = { "main-reset", "aux-reset" };
	struct device *device = &pdev->dev;
	struct grx500_pcie *pcie;
	unsigned int i;
	int ret;

	pcie = devm_kzalloc(device, sizeof(*pcie), GFP_KERNEL);
	if (!pcie)
		return -ENOMEM;
	pcie->device = device;
	pcie->rcu = syscon_regmap_lookup_by_phandle(device->of_node, "lantiq,rcu");
	if (IS_ERR(pcie->rcu))
		return PTR_ERR(pcie->rcu);
	pcie->clocks[0].id = "pcie0";
	pcie->clocks[1].id = "pcie1";
	ret = devm_clk_bulk_get(device, PORTS, pcie->clocks);
	if (ret)
		return dev_err_probe(device, ret, "controller clocks unavailable\n");
	pcie->pll = devm_clk_get(device, "pll");
	if (IS_ERR(pcie->pll))
		return dev_err_probe(device, PTR_ERR(pcie->pll), "PLL unavailable\n");
	pcie->phy_resets = devm_reset_control_array_get_exclusive(device);
	if (IS_ERR(pcie->phy_resets))
		return dev_err_probe(device, PTR_ERR(pcie->phy_resets), "PHY resets unavailable\n");
	for (i = 0; i < PORTS; i++) {
		struct grx500_pcie_port *port = &pcie->ports[i];

		port->csr = devm_platform_ioremap_resource_byname(pdev, names[i][0]);
		port->config = devm_platform_ioremap_resource_byname(pdev, names[i][1]);
		port->app = devm_platform_ioremap_resource_byname(pdev, names[i][2]);
		port->phy = devm_platform_ioremap_resource_byname(pdev, names[i][3]);
		if (IS_ERR(port->csr) || IS_ERR(port->config) || IS_ERR(port->app) || IS_ERR(port->phy))
			return -ENODEV;
		port->perst = devm_gpiod_get(device, perst_names[i], GPIOD_OUT_HIGH);
		if (IS_ERR(port->perst))
			return dev_err_probe(device, PTR_ERR(port->perst), "PERST unavailable\n");
		spin_lock_init(&port->lock);
		port->index = i;
		port->radio_enabled = i == 0;
		if (port->radio_enabled) {
			port->irq = platform_get_irq(pdev, 0);
			if (port->irq <= 0)
				return port->irq ? port->irq : -EINVAL;
		}
		port->memory = (struct resource) {
			.name = i ? "GRX500 PCIe1 memory" : "GRX500 PCIe0 memory",
			.start = i ? 0xb4000000 : 0xb8000000,
			.end = i ? 0xb6ffffff : 0xbaffffff,
			.flags = IORESOURCE_MEM,
		};
		port->buses = (struct resource) {
			.name = "GRX500 point-to-point bus", .start = 0, .end = 1,
			.flags = IORESOURCE_BUS,
		};
	}
	ret = grx500_prepare_links(pcie);
	if (ret) {
		dev_err(device, "PCIe preparation failed: %d\n", ret);
		goto cleanup;
	}
	for (i = 0; i < PORTS; i++) {
		struct grx500_pcie_port *port = &pcie->ports[i];
		struct pci_host_bridge *bridge = pci_alloc_host_bridge(0);

		if (!bridge) {
			ret = -ENOMEM;
			goto cleanup;
		}
		port->bridge = bridge;
		bridge->dev.parent = device;
		bridge->sysdata = port;
		bridge->ops = &grx500_pci_ops;
		bridge->busnr = 0;
		bridge->domain_nr = i;
		bridge->swizzle_irq = pci_common_swizzle;
		bridge->map_irq = grx500_map_irq;
		pci_add_resource(&bridge->windows, &port->memory);
		pci_add_resource(&bridge->windows, &port->buses);
		ret = devm_request_pci_bus_resources(device, &bridge->windows);
		if (ret)
			goto cleanup;
		if (port->radio_enabled) {
			/* A21 needs one initial APP clear/enable. The native GIC level
			 * handler and endpoint source acknowledge subsequent interrupts.
			 * Endpoint INTx stays disabled until its driver owns the IRQ.
			 */
			grx500_write(port->app, APP_IRQ_CLEAR, APP_INTA);
			grx500_write(port->app, APP_IRQ_ENABLE, APP_INTA);
		}
		ret = pci_host_probe(bridge);
		if (ret)
			goto cleanup;
		port->registered = true;
	}
	/* Runs before devres releases PCI windows, MMIO or the containing state. */
	ret = devm_add_action_or_reset(device, grx500_cleanup, pcie);
	if (ret)
		return ret;

	platform_set_drvdata(pdev, pcie);
	dev_info(device, "WAVE endpoints enumerated; main DMA/INTx %s, auxiliary fenced\n",
		 pcie->ports[0].radio_enabled ? "driver controlled" : "disabled");
	return 0;

cleanup:
	grx500_cleanup(pcie);
	return ret;
}

static void grx500_pcie_shutdown(struct platform_device *pdev)
{
	/* Child PCI shutdown callbacks have fenced their DMA. Keep the buses,
	 * mappings and bridge objects alive while holding both endpoints reset.
	 */
	grx500_stop_links(platform_get_drvdata(pdev));
	dev_info(&pdev->dev, "PCIe endpoints held in reset for shutdown\n");
}

static const struct of_device_id grx500_pcie_match[] = {
	{ .compatible = "lantiq,grx500-pcie" },
	{ }
};
MODULE_DEVICE_TABLE(of, grx500_pcie_match);

static struct platform_driver grx500_pcie_driver = {
	.probe = grx500_pcie_probe,
	.shutdown = grx500_pcie_shutdown,
	.driver = {
		.name = "grx500-pcie",
		.of_match_table = grx500_pcie_match,
	},
};
module_platform_driver(grx500_pcie_driver);
MODULE_LICENSE("GPL");
