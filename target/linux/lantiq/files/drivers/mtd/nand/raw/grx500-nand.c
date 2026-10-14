// SPDX-License-Identifier: GPL-2.0-only
/* GRX500 EBU NAND controller. Register protocol follows the Lantiq BSP
 * xway_nand.c (John Crispin, Mohammad Firdaus B Alias Thani). The NAND core
 * owns ECC and partition protection; AX50 uses the factory BCH4/512 layout.
 */
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/mtd/partitions.h>
#include <linux/mtd/rawnand.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/sched.h>
#include <linux/sizes.h>

#define EBU_ADDSEL1	0x24
#define EBU_BUSCON1	0x64
#define EBU_NAND_CON	0xb0
#define EBU_NAND_WAIT	0xb4
#define NAND_READY	BIT(0)
#define BUS_COMPLETE	BIT(3)
#define CON_ENABLE	BIT(0)
#define CON_ALE		BIT(18)
#define CON_CE		BIT(20)
#define ADDR_DATA	BIT(4)
#define ADDR_COMMAND	(BIT(4) | BIT(3))
#define ADDR_ADDRESS	(BIT(4) | BIT(2))

struct grx500_nand {
	struct nand_controller controller;
	struct nand_chip chip;
	void __iomem *io;
	void __iomem *ebu;
	u32 saved_addsel, saved_buscon, saved_con;
};

static void grx500_con(struct grx500_nand *host, u32 clear, u32 set)
{
	u32 value = __raw_readl(host->ebu + EBU_NAND_CON);

	__raw_writel((value & ~clear) | set, host->ebu + EBU_NAND_CON);
	wmb();
}

static int grx500_bus_complete(struct grx500_nand *host)
{
	u32 value;

	/* Avoid a clock read on every byte when the bus has already completed. */
	if (__raw_readl(host->ebu + EBU_NAND_WAIT) & BUS_COMPLETE)
		return 0;
	return read_poll_timeout_atomic(__raw_readl, value,
					value & BUS_COMPLETE, 0, 1000, false,
					host->ebu + EBU_NAND_WAIT);
}

static bool grx500_read_command(u8 command)
{
	switch (command) {
	case NAND_CMD_RESET:
	case NAND_CMD_READID:
	case NAND_CMD_PARAM:
	case NAND_CMD_READ0:
	case NAND_CMD_READSTART:
	case NAND_CMD_STATUS:
	case NAND_CMD_RNDOUT:
	case NAND_CMD_RNDOUTSTART:
	case NAND_CMD_GET_FEATURES:
		return true;
	default:
		return false;
	}
}

static bool grx500_write_command(u8 command)
{
	switch (command) {
	case NAND_CMD_SEQIN:
	case NAND_CMD_RNDIN:
	case NAND_CMD_PAGEPROG:
	case NAND_CMD_ERASE1:
	case NAND_CMD_ERASE2:
		return true;
	default:
		return false;
	}
}

static int grx500_exec_instr(struct grx500_nand *host,
			   const struct nand_op_instr *instr)
{
	u32 value;
	unsigned int i;
	u8 *buffer;
	const u8 *output;
	int ret = 0;

	switch (instr->type) {
	case NAND_OP_CMD_INSTR:
		grx500_con(host, CON_ALE, 0);
		writeb(instr->ctx.cmd.opcode, host->io + ADDR_COMMAND);
		return grx500_bus_complete(host);
	case NAND_OP_ADDR_INSTR:
		grx500_con(host, 0, CON_ALE);
		for (i = 0; i < instr->ctx.addr.naddrs; i++) {
			writeb(instr->ctx.addr.addrs[i], host->io + ADDR_ADDRESS);
			ret = grx500_bus_complete(host);
			if (ret)
				break;
		}
		grx500_con(host, CON_ALE, 0);
		return ret;
	case NAND_OP_DATA_IN_INSTR:
		grx500_con(host, CON_ALE, 0);
		buffer = instr->ctx.data.buf.in;
		for (i = 0; i < instr->ctx.data.len; i++) {
			buffer[i] = readb(host->io + ADDR_DATA);
			ret = grx500_bus_complete(host);
			if (ret)
				return ret;
		}
		return 0;
	case NAND_OP_DATA_OUT_INSTR:
		grx500_con(host, CON_ALE, 0);
		output = instr->ctx.data.buf.out;
		for (i = 0; i < instr->ctx.data.len; i++) {
			writeb(output[i], host->io + ADDR_DATA);
			ret = grx500_bus_complete(host);
			if (ret)
				return ret;
		}
		return 0;
	case NAND_OP_WAITRDY_INSTR:
		return read_poll_timeout(__raw_readl, value, value & NAND_READY,
					 1, (u64)instr->ctx.waitrdy.timeout_ms * 1000,
					 false, host->ebu + EBU_NAND_WAIT);
	default:
		return -EOPNOTSUPP;
	}
}

static int grx500_exec_op(struct nand_chip *chip,
			const struct nand_operation *op, bool check_only)
{
	struct grx500_nand *host = nand_get_controller_data(chip);
	unsigned int i;
	int ret = 0;

	if (op->cs != 0)
		return -EOPNOTSUPP;
	/* Validate the whole operation before touching chip select or any bus
	 * register. check_only data reads may legitimately have a NULL buffer.
	 */
	for (i = 0; i < op->ninstrs; i++) {
		const struct nand_op_instr *instr = &op->instrs[i];

		switch (instr->type) {
		case NAND_OP_CMD_INSTR:
			if (!grx500_read_command(instr->ctx.cmd.opcode) &&
			    !grx500_write_command(instr->ctx.cmd.opcode))
				return -EOPNOTSUPP;
			break;
		case NAND_OP_ADDR_INSTR:
			if (!instr->ctx.addr.naddrs || !instr->ctx.addr.addrs)
				return -EINVAL;
			break;
		case NAND_OP_DATA_IN_INSTR:
			if (!check_only && instr->ctx.data.len && !instr->ctx.data.buf.in)
				return -EINVAL;
			break;
		case NAND_OP_DATA_OUT_INSTR:
			if (!check_only && instr->ctx.data.len && !instr->ctx.data.buf.out)
				return -EINVAL;
			break;
		case NAND_OP_WAITRDY_INSTR:
			if (!instr->ctx.waitrdy.timeout_ms)
				return -EINVAL;
			break;
		default:
			return -EOPNOTSUPP;
		}
	}
	if (check_only)
		return 0;

	/* NAND core serializes operations; this controller owns its EBU chip select. */
	grx500_con(host, CON_ALE, CON_ENABLE | CON_CE);
	for (i = 0; i < op->ninstrs; i++) {
		ret = grx500_exec_instr(host, &op->instrs[i]);
		if (ret)
			break;
		if (op->instrs[i].delay_ns)
			ndelay(op->instrs[i].delay_ns);
	}
	grx500_con(host, CON_ALE | CON_CE | CON_ENABLE, 0);
	/* Bad-block scans can otherwise occupy CPU0 for many seconds with
	 * PREEMPT_NONE, starving the bounded early watchdog handoff worker.
	 */
	cond_resched();
	return ret;
}

static int grx500_oob_ecc(struct mtd_info *mtd, int section,
			struct mtd_oob_region *region)
{
	if (section)
		return -ERANGE;
	region->offset = 36;
	region->length = 28;
	return 0;
}

static int grx500_oob_free(struct mtd_info *mtd, int section,
			 struct mtd_oob_region *region)
{
	if (section)
		return -ERANGE;
	region->offset = 2;
	region->length = 34;
	return 0;
}

static const struct mtd_ooblayout_ops grx500_oob_ops = {
	.ecc = grx500_oob_ecc,
	.free = grx500_oob_free,
};

static int grx500_attach_chip(struct nand_chip *chip)
{
	struct mtd_info *mtd = nand_to_mtd(chip);

	if (mtd->writesize != SZ_2K ||
	    mtd->oobsize != 64 || mtd->erasesize != SZ_128K ||
	    chip->options & NAND_BUSWIDTH_16 || chip->bbt_options ||
	    chip->ecc.engine_type != NAND_ECC_ENGINE_TYPE_SOFT ||
	    chip->ecc.algo != NAND_ECC_ALGO_BCH ||
	    chip->ecc.size != 512 || chip->ecc.strength != 4)
		return -ENODEV;
	mtd_set_ooblayout(mtd, &grx500_oob_ops);
	return 0;
}

static const struct nand_controller_ops grx500_nand_ops = {
	.exec_op = grx500_exec_op,
	.attach_chip = grx500_attach_chip,
};

static void grx500_restore_ebu(void *data)
{
	struct grx500_nand *host = data;

	__raw_writel(host->saved_con, host->ebu + EBU_NAND_CON);
	__raw_writel(host->saved_buscon, host->ebu + EBU_BUSCON1);
	__raw_writel(host->saved_addsel, host->ebu + EBU_ADDSEL1);
	wmb();
}

static int grx500_nand_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct grx500_nand *host;
	struct nand_chip *chip;
	struct mtd_info *mtd;
	struct resource *res;
	int ret;

	static const char * const parsers[] = { "ofpart", NULL };
	host = devm_kzalloc(dev, sizeof(*host), GFP_KERNEL);
	if (!host)
		return -ENOMEM;
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res || res->start != 0x17c00000 || resource_size(res) < SZ_1M)
		return -EINVAL;
	host->io = devm_ioremap_resource(dev, res);
	if (IS_ERR(host->io))
		return PTR_ERR(host->io);
	res = platform_get_resource(pdev, IORESOURCE_MEM, 1);
	if (!res || res->start != 0x16f00000 || resource_size(res) < 0x100)
		return -EINVAL;
	host->ebu = devm_ioremap_resource(dev, res);
	if (IS_ERR(host->ebu))
		return PTR_ERR(host->ebu);

	host->saved_addsel = __raw_readl(host->ebu + EBU_ADDSEL1);
	host->saved_buscon = __raw_readl(host->ebu + EBU_BUSCON1);
	host->saved_con = __raw_readl(host->ebu + EBU_NAND_CON);
	ret = devm_add_action_or_reset(dev, grx500_restore_ebu, host);
	if (ret)
		return ret;
	/* Physical aperture plus GRX500 mask 5; never derive this from the EVA
	 * ioremap pointer. Keep the BSP's CS1 bus setup and NAND signal polarity.
	 */
	__raw_writel(0x17c00051, host->ebu + EBU_ADDSEL1);
	__raw_writel(BIT(22) | (3 << 12) | (2 << 8) | (2 << 6) |
		     BIT(4) | BIT(2) | 1, host->ebu + EBU_BUSCON1);
	__raw_writel(BIT(10) | BIT(8) | BIT(7) | BIT(6) | BIT(5) |
		     BIT(4) | BIT(1), host->ebu + EBU_NAND_CON);
	wmb();

	chip = &host->chip;
	nand_controller_init(&host->controller);
	host->controller.ops = &grx500_nand_ops;
	chip->controller = &host->controller;
	nand_set_controller_data(chip, host);
	nand_set_flash_node(chip, dev->of_node);
	chip->options = NAND_NO_SUBPAGE_WRITE | NAND_KEEP_TIMINGS;
	chip->ecc.engine_type = NAND_ECC_ENGINE_TYPE_SOFT;
	chip->ecc.algo = NAND_ECC_ALGO_BCH;
	chip->ecc.size = 512;
	chip->ecc.strength = 4;
	mtd = nand_to_mtd(chip);
	mtd->dev.parent = dev;
	mtd->name = "grx500-nand";
	platform_set_drvdata(pdev, host);
	ret = nand_scan(chip, 1);
	if (ret)
		return dev_err_probe(dev, ret, "NAND scan failed\n");
	if (chip->ecc.bytes != 7 ||
	    chip->ecc.total != 28) {
		nand_cleanup(chip);
		return -EINVAL;
	}
	ret = mtd_device_parse_register(mtd, parsers, NULL, NULL, 0);
	if (ret)
		nand_cleanup(chip);
	else
		dev_info(dev, "NAND BCH4/512, OOB ECC 36..63\n");
	return ret;
}

static void grx500_nand_remove(struct platform_device *pdev)
{
	struct grx500_nand *host = platform_get_drvdata(pdev);
	int ret = mtd_device_unregister(nand_to_mtd(&host->chip));

	WARN_ON(ret);
	nand_cleanup(&host->chip);
}

static const struct of_device_id grx500_nand_match[] = {
	{ .compatible = "lantiq,grx500-nand" },
	{},
};

static struct platform_driver grx500_nand_driver = {
	.probe = grx500_nand_probe,
	.remove = grx500_nand_remove,
	.driver = {
		.name = "grx500-nand",
		.of_match_table = grx500_nand_match,
	},
};
builtin_platform_driver(grx500_nand_driver);
