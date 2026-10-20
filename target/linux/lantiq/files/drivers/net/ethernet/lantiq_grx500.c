// SPDX-License-Identifier: GPL-2.0-or-later
/* GRX500 DMA2 LAN and DMA1 WAN paths, based on the register protocol in TP-Link's
 * U-Boot grx500_sw.c (Wolfgang Denk/Lantiq) and xrx500_phy_fw.c (John Crispin).
 * One owner initializes both switches and the five shared GPHYs; a DSA child
 * owns LAN port policy after the DMA conduits and MDIO buses are registered.
 */
#include <linux/cpumask.h>
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/etherdevice.h>
#include <linux/ethtool.h>
#include <linux/firmware.h>
#include <linux/io.h>
#include <linux/interrupt.h>
#include <linux/iopoll.h>
#include <linux/mii.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/phy.h>
#include <linux/rtnetlink.h>
#include <linux/sizes.h>
#include <linux/of_mdio.h>
#include <linux/of_net.h>
#include <linux/of_platform.h>
#include <linux/platform_data/grx500.h>
#include <linux/reset.h>
#include <linux/workqueue.h>
#include "lantiq_grx500_pce.h"

#define RX_COUNT	32
#define TX_COUNT	8
#define BUF_SIZE	2048
#define MAX_FRAME	1536
#define PMAC_SIZE	8
#define P2P_CHANNEL	1
#define PAE_SESSIONS	4096
#define PAE_RETURN_PORT	7
#define PAE_RETURN_MARK	0xa
#define R_INTERNAL_VID	4095
#define R_ACTIVE_PORTS	(BIT(0) | GENMASK(5, 2) | BIT(15))
#define FW_SIZE		SZ_64K
#define FW_ALIGN	SZ_16K
#define FW_ALLOC	(FW_SIZE + FW_ALIGN)
#define GSW_TOP		0x3c00
#define GSW_PCE_CTRL	(0x456 * 4)
#define GSW_PMAC_CTRL2	(0xd05 * 4)
#define GSW_PMAC_CTRL4	(0xd07 * 4)
#define MDIO_CTRL	(GSW_TOP + 0x10)
#define MDIO_DATA	(GSW_TOP + 0x14)
#define MDIO_POLL	(GSW_TOP + 0x1c)
#define PHY_ADDR(i)	(GSW_TOP + 0x110 + (i) * 0x10)
#define PHY_ADDR_FCON_TX	GENMASK(8, 7)
#define PHY_ADDR_FCON_RX	GENMASK(6, 5)
#define PHY_ADDR_FCON_TX_EN BIT(7)
#define PHY_ADDR_FCON_RX_EN BIT(5)
#define MAC_CTRL0	(0x903 * 4)
#define MAC_CTRL0_FCON	GENMASK(6, 4)
#define MAC_CTRL0_FCON_RX	BIT(4)
#define MAC_CTRL0_FCON_TX	BIT(5)
#define MAC_CTRL0_FCON_OFF BIT(6)
#define SDMA_PCTRL(p)	((0xbc0 + 6 * (p)) * 4)
#define SDMA_PCTRL_FCEN	BIT(1)
#define GRX500_PAUSE_REGS_COUNT 46
#define PHY_FW_ADDR(i)	(GSW_TOP + 0x228 + (i) * 0x10)
#define PCE_TABLE_CTRL	(0x44f * 4)
#define PCE_TABLE_ADDR	(0x44e * 4)
#define PCE_TABLE_KEY(i)	((0x447 - (i)) * 4)
#define PCE_TABLE_MASK(i) (((i) ? 0x42d - (i) : 0x448) * 4)
#define PCE_TABLE_VAL(i)	(((i) < 5 ? 0x44d - (i) : 0x43c - (i)) * 4)
#define PAE_TABLE_CTRL	(0x46d * 4)
#define PCE_PORT_CTRL(i)	((0x480 + (i) * 10) * 4)
#define PCE_UCAST_MAP	(0x455 * 4)
#define PCE_MCAST_MAP	(0x454 * 4)
#define PMAC_TABLE_CTRL	(0xd46 * 4)
#define PMAC_TABLE_ADDR	(0xd45 * 4)
#define PMAC_TABLE_VAL(i) ((0xd44 - (i)) * 4)
#define TABLE_BUSY	BIT(15)
#define TABLE_WRITE	BIT(5)
#define BM_RAM_VAL(i)	((0x043 - (i)) * 4)
#define BM_RAM_ADDR	(0x044 * 4)
#define BM_RAM_CTRL	(0x045 * 4)
#define BM_CPU_PCFG	(0x080 * 4)
#define BM_CNTEN		BIT(0)
/* GSWIP3 logical ports 8..15 use BM tables 16..23. */
#define WAN_RMON_TABLE	23
/* Logical ports 0..7 use the identically numbered BM table. */
#define CPU_RMON_TABLE	0

#define DMA_ID		0x08
#define DMA_CTRL	0x10
#define DMA_CPOLL	0x14
#define DMA_CS		0x18
#define DMA_CCTRL	0x1c
#define DMA_CDBA	0x20
#define DMA_CDLEN	0x24
#define DMA_CIS		0x28
#define DMA_CIE		0x2c
#define DMA_CGBL	0x30
#define DMA_PS		0x40
#define DMA_PCTRL	0x44
#define DMA_IRNEN	0xf4
#define DMA_IRNCR	0xf8
#define DESC_OWN	BIT(31)
#define DESC_DONE	BIT(30)
#define DESC_SOP	BIT(29)
#define DESC_EOP	BIT(28)
#define DMA_ON		BIT(0)
#define DMA_RESET	BIT(1)
#define DMA_EOP		BIT(1)
#define DMA_DESCPT	BIT(3)
#define DMA_CI_ALL	GENMASK(5, 1)
#define DMA_CLASS_MASK	(GENMASK(11, 9) | GENMASK(19, 18))
#define DMA_P2P		BIT(24)
/* U-Boot's DMA2 port: BE words, two-doubleword bursts, TX weight one. */
#define DMA_PORT_CONFIG	((1 << 12) | (3 << 10) | (3 << 8) | (2 << 4) | (2 << 2))
#define DMA_POLL_CONFIG	(BIT(31) | (24 << 4))

/* Native BE descriptor layout is DW1, DW0, DW3, DW2 in memory. */
struct grx500_desc {
	__be32 dw1, dw0, status, address;
} __aligned(16);

struct grx500_dma_memory {
	struct grx500_desc rx[RX_COUNT];
	struct grx500_desc tx[TX_COUNT];
};

struct grx500_dma {
	void __iomem *base;
	/* Protect CS together with every banked register access. */
	spinlock_t lock;
	unsigned int channels;
	bool transmit;
};

struct grx500_p2p_memory {
	struct grx500_desc desc[RX_COUNT];
	u8 data[RX_COUNT][BUF_SIZE] __aligned(ARCH_DMA_MINALIGN);
};

struct grx500_tx_buffer {
	struct sk_buff *skb;
	dma_addr_t dma;
	unsigned int mapped_length;
};

struct grx500_rmon_counter {
	const char *name;
	u8 offset;
	bool bytes;
};

/* The frame totals at 0x0c/0x1f depend on the broadcast counting mode.
 * Leave that mode unchanged and expose only counters with fixed meanings.
 */
static const struct grx500_rmon_counter grx500_wan_rmon_counters[] = {
	{ "rx_unicast_packets", 0x23 },
	{ "rx_multicast_packets", 0x22 },
	{ "rx_fcs_errors", 0x21 },
	{ "rx_undersize_good_packets", 0x1d },
	{ "rx_undersize_errors", 0x1e },
	{ "rx_oversize_good_packets", 0x1b },
	{ "rx_oversize_errors", 0x1c },
	{ "rx_pause_packets", 0x20 },
	{ "rx_alignment_errors", 0x1a },
	{ "rx_64_byte_packets", 0x12 },
	{ "rx_65_127_byte_packets", 0x13 },
	{ "rx_128_255_byte_packets", 0x14 },
	{ "rx_256_511_byte_packets", 0x15 },
	{ "rx_512_1023_byte_packets", 0x16 },
	{ "rx_1024_max_byte_packets", 0x17 },
	{ "rx_tail_dropped_packets", 0x18 },
	{ "rx_filtered_packets", 0x19 },
	{ "rx_good_bytes", 0x24, true },
	{ "rx_bad_bytes", 0x26, true },
	{ "tx_acm_dropped_packets", 0x11 },
	{ "tx_unicast_packets", 0x06 },
	{ "tx_multicast_packets", 0x07 },
	{ "tx_64_byte_packets", 0x00 },
	{ "tx_65_127_byte_packets", 0x01 },
	{ "tx_128_255_byte_packets", 0x02 },
	{ "tx_256_511_byte_packets", 0x03 },
	{ "tx_512_1023_byte_packets", 0x04 },
	{ "tx_1024_max_byte_packets", 0x05 },
	{ "tx_single_collisions", 0x08 },
	{ "tx_multiple_collisions", 0x09 },
	{ "tx_late_collisions", 0x0a },
	{ "tx_excessive_collisions", 0x0b },
	{ "tx_pause_packets", 0x0d },
	{ "tx_dropped_packets", 0x10 },
	{ "tx_good_bytes", 0x0e, true },
};

/* GSW-R port 0 transmits CPU misses toward PMAC/RX DMA, not to the WAN
 * cable. Keep the mode-dependent total/broadcast counter out of this set.
 */
static const struct grx500_rmon_counter grx500_cpu_rmon_counters[] = {
	{ "cpu_port_tx_unicast_packets", 0x06 },
	{ "cpu_port_tx_multicast_packets", 0x07 },
	{ "cpu_port_tx_64_byte_packets", 0x00 },
	{ "cpu_port_tx_65_127_packets", 0x01 },
	{ "cpu_port_tx_128_255_packets", 0x02 },
	{ "cpu_port_tx_256_511_packets", 0x03 },
	{ "cpu_port_tx_512_1023_packets", 0x04 },
	{ "cpu_port_tx_1024_max_packets", 0x05 },
	{ "cpu_port_tx_acm_dropped_packets", 0x11 },
	{ "cpu_port_tx_dropped_packets", 0x10 },
	{ "cpu_port_tx_good_bytes", 0x0e, true },
};

struct grx500_rmon_stats {
	u64 counters[ARRAY_SIZE(grx500_wan_rmon_counters)];
	u64 read_failures;
};

struct grx500_cpu_rmon_stats {
	u64 counters[ARRAY_SIZE(grx500_cpu_rmon_counters)];
	u64 read_failures;
};

/* One owner for the shared GPHY instruction memory and switch-L registers.
 * The WAN PHY's firmware pointer lives in switch-L, even though packets and
 * MDIO use switch-R. Neither netdev may free or reset the other's resources.
 */
struct grx500_eth_board {
	struct device *device;
	void __iomem *gsw[2];
	struct grx500_dma rx_dma[2], tx_dma[2];
	struct net_device *netdev[2];
	struct reset_control *phy_resets;
	struct grx500_switch_pdata switch_data;
	void *firmware;
	dma_addr_t firmware_dma;
	/* One allocation, owned by both P2P endpoints until both stop. CPU code
	 * never consumes or rearms these descriptors while either endpoint runs.
	 */
	struct grx500_p2p_memory *p2p;
	dma_addr_t p2p_dma;
	bool ready;
};

struct grx500_eth {
	struct device *device;
	struct grx500_eth_board *board;
	struct net_device *netdev;
	void __iomem *gsw;
	struct grx500_dma *rx_dma, *tx_dma;
	struct grx500_dma_memory *memory;
	dma_addr_t memory_dma;
	/* Descriptors are coherent. RX slots own complete cached lines; each TX
	 * slot retains its skb and exact streaming mapping until DMA completes.
	 */
	u8 (*rx_data)[BUF_SIZE];
	dma_addr_t rx_data_dma;
	bool rx_data_mapped;
	struct grx500_tx_buffer tx_buffer[TX_COUNT];
	struct mii_bus *mdio;
	struct phy_device *phydev;
	/* Requested WAN policy; PHY locking also serializes its MAC updates. */
	bool pause_autoneg;
	bool tx_pause;
	bool rx_pause;
	bool wan;
	struct napi_struct napi;
	struct delayed_work recheck_work;
	struct delayed_work writeback_work;
	struct work_struct reset_work;
	spinlock_t lock;
	struct rtnl_link_stats64 stats;
	/* WAN owns GSW-R's BM registers; the DSA child uses GSW-L. */
	struct mutex bm_lock;
	struct grx500_rmon_stats rmon;
	struct grx500_cpu_rmon_stats cpu_rmon;
	unsigned int rx_next, tx_head, tx_tail, tx_pending;
	int rx_irq, tx_irq;
	bool running;
	/* NAPI is attached to this real netdev for native ownership/discovery,
	 * but rx_running owns its service lifetime: WAN CPU RX also carries LAN
	 * misses while WAN is administratively down. Both real netdevs are closed
	 * before either NAPI/netdev is freed.
	 */
	bool rx_running;
};

static void grx500_writel(void __iomem *base, unsigned int reg, u32 value)
{
	__raw_writel(value, base + reg);
	wmb();
}

static int grx500_dma_stop(struct grx500_dma *dma, unsigned int channel)
{
	void __iomem *base = dma->base;
	unsigned long flags;
	u32 value;
	int ret;

	spin_lock_irqsave(&dma->lock, flags);
	grx500_writel(base, DMA_CS, channel);
	grx500_writel(base, DMA_CIE, 0);
	value = __raw_readl(base + DMA_IRNEN) & ~BIT(channel);
	grx500_writel(base, DMA_IRNEN, value);
	value = __raw_readl(base + DMA_CCTRL);
	grx500_writel(base, DMA_CCTRL, value & ~DMA_ON);
	ret = read_poll_timeout_atomic(__raw_readl, value, !(value & DMA_ON),
				       1, 10000, false, base + DMA_CCTRL);
	spin_unlock_irqrestore(&dma->lock, flags);
	return ret;
}

static void grx500_dma_start(struct grx500_dma *dma, unsigned int channel)
{
	void __iomem *base = dma->base;
	unsigned long flags;
	u32 value;

	spin_lock_irqsave(&dma->lock, flags);
	grx500_writel(base, DMA_CS, channel);
	value = __raw_readl(base + DMA_CCTRL);
	if (!(value & DMA_ON))
		grx500_writel(base, DMA_CCTRL, value | DMA_ON);
	spin_unlock_irqrestore(&dma->lock, flags);
}

static int grx500_dma_init(struct grx500_dma *dma, bool flow_control)
{
	void __iomem *base = dma->base;
	unsigned int channel;
	u32 value;
	int ret;

	/* Probe is the only controller-wide reset/configuration owner. No rings
	 * have been published, and inherited channels must first acknowledge OFF.
	 */
	dma->channels = (__raw_readl(base + DMA_ID) >> 20) & 0x7f;
	if (dma->channels <= P2P_CHANNEL || dma->channels > 32)
		return -ENODEV;
	for (channel = 0; channel < dma->channels; channel++) {
		ret = grx500_dma_stop(dma, channel);
		if (ret)
			return ret;
	}
	/* Retain the known RX-controller/TX-channel reset distinction. */
	if (!dma->transmit) {
		value = __raw_readl(base + DMA_CTRL);
		grx500_writel(base, DMA_CTRL, value | BIT(0));
		ret = read_poll_timeout(__raw_readl, value, !(value & BIT(0)),
					5, 10000, false, base + DMA_CTRL);
		if (ret)
			return ret;
	} else {
		/* R native/P2P injectors receive the explicit PMAC sideband maps.
		 * L TX0 has a separate DSA queue owner and remains unchanged.
		 */
		grx500_writel(base, DMA_CTRL, 0x80040200 | (flow_control ? BIT(6) : 0));
	}
	grx500_writel(base, DMA_IRNEN, 0);
	grx500_writel(base, DMA_IRNCR, U32_MAX);
	grx500_writel(base, DMA_PS, 0);
	grx500_writel(base, DMA_PCTRL, DMA_PORT_CONFIG);
	grx500_writel(base, DMA_CPOLL, DMA_POLL_CONFIG);
	return 0;
}

static void grx500_quiesce(struct grx500_eth *eth)
{
	int rx, tx;

	/* No buffer may be freed while the DMA engines can still access it. */
	rx = grx500_dma_stop(eth->rx_dma, 0);
	tx = grx500_dma_stop(eth->tx_dma, 0);
	if (rx || tx)
		panic("GRX500 Ethernet: DMA did not stop; reset required");
}

static int grx500_mdio_read(void __iomem *gsw, unsigned int phy,
			  unsigned int reg)
{
	u32 value;
	int ret;

	ret = read_poll_timeout(__raw_readl, value, !(value & BIT(12)),
				5, 10000, false, gsw + MDIO_CTRL);
	if (ret)
		return ret;
	grx500_writel(gsw, MDIO_CTRL, BIT(12) | BIT(11) | (phy << 5) | reg);
	/* GSWIP3 may still expose the previous idle state just after submission.
	 * Wait before the first completion check, as in its MDIO API.
	 */
	ret = read_poll_timeout(__raw_readl, value, !(value & BIT(12)),
				5, 10000, true, gsw + MDIO_CTRL);
	return ret ? ret : __raw_readl(gsw + MDIO_DATA) & 0xffff;
}

static int grx500_load_phy(struct grx500_eth_board *board)
{
	dma_addr_t address;
	const struct firmware *firmware;
	unsigned int i, offset;
	int ret;
	int version = -ETIMEDOUT;

	ret = request_firmware(&firmware, "lantiq/grx500-phy11g-a21.bin", board->device);
	if (ret)
		return ret;
	if (firmware->size != FW_SIZE) {
		release_firmware(firmware);
		return -EINVAL;
	}
	board->firmware = dma_alloc_coherent(board->device, FW_ALLOC,
					   &board->firmware_dma, GFP_KERNEL);
	if (!board->firmware) {
		release_firmware(firmware);
		return -ENOMEM;
	}
	address = ALIGN(board->firmware_dma, FW_ALIGN);
	offset = address - board->firmware_dma;
	memcpy(board->firmware + offset, firmware->data, FW_SIZE);
	release_firmware(firmware);
	dma_wmb();
	ret = reset_control_assert(board->phy_resets);
	if (ret)
		return ret;
	msleep(100);
	for (i = 0; i < 5; i++) {
		/* Donor slots1..5 are zero-based here; slot5 is the WAN GPHY6F. */
		grx500_writel(board->gsw[0], GSW_TOP + 0x220 + i * 0x10, 3);
		grx500_writel(board->gsw[0], GSW_TOP + 0x224 + i * 0x10, 0x51ec);
		grx500_writel(board->gsw[0], PHY_FW_ADDR(i), address & 0xffff);
		grx500_writel(board->gsw[0], PHY_FW_ADDR(i) + 4, address >> 16);
	}
	ret = reset_control_deassert(board->phy_resets);
	if (ret)
		return ret;
	/* The same firmware reports 0x8548 at PHY4 register 0x1e in U-Boot. */
	for (i = 0; i < 20; i++) {
		msleep(100);
		version = grx500_mdio_read(board->gsw[0], 4, 0x1e);
		if (version == 0x8548)
			break;
	}
	dev_info(board->device, "GPHY firmware loaded at %pad, version=%x\n",
		 &address, version);
	if (version != 0x8548)
		return -EIO;
	for (i = 0; i < 20; i++) {
		version = grx500_mdio_read(board->gsw[1], 1, 0x1e);
		if (version == 0x8548)
			return 0;
		msleep(100);
	}
	dev_err(board->device, "WAN GPHY firmware did not start (%x)\n", version);
	return -EIO;
}

static int grx500_table_idle(void __iomem *gsw, unsigned int reg)
{
	u32 value;

	return read_poll_timeout(__raw_readl, value, !(value & TABLE_BUSY),
				1, 10000, false, gsw + reg);
}

/* GSW-R tables are written only during probe, before either netdev or the
 * DSA child is published. No runtime route entries are admitted by this path.
 */
static void grx500_pce_words_clear(void __iomem *gsw)
{
	unsigned int i;

	for (i = 0; i < 16; i++) {
		grx500_writel(gsw, PCE_TABLE_KEY(i), 0);
		grx500_writel(gsw, PCE_TABLE_VAL(i), 0);
	}
	for (i = 0; i < 4; i++)
		grx500_writel(gsw, PCE_TABLE_MASK(i), 0);
}

static int grx500_r_clear_table(void __iomem *gsw, unsigned int table,
				unsigned int entries)
{
	unsigned int i;
	int ret;

	ret = grx500_table_idle(gsw, PCE_TABLE_CTRL);
	if (ret)
		return ret;
	grx500_pce_words_clear(gsw);
	for (i = 0; i < entries; i++) {
		grx500_writel(gsw, PCE_TABLE_ADDR, i);
		grx500_writel(gsw, PCE_TABLE_CTRL, TABLE_BUSY | TABLE_WRITE | table);
		ret = grx500_table_idle(gsw, PCE_TABLE_CTRL);
		if (ret)
			return ret;
	}
	return 0;
}

static int grx500_r_sessions_clear(void __iomem *gsw)
{
	unsigned int i;
	int ret;

	ret = grx500_table_idle(gsw, PAE_TABLE_CTRL);
	if (ret)
		return ret;
	grx500_writel(gsw, PCE_TABLE_CTRL, 0);
	grx500_pce_words_clear(gsw);
	for (i = 0; i < PAE_SESSIONS; i++) {
		/* Invalid, self-terminating hash-chain heads. Session validity is
		 * KEY4[15]; verify it through the independent route-table read.
		 */
		grx500_writel(gsw, PCE_TABLE_ADDR, i);
		grx500_writel(gsw, PCE_TABLE_KEY(4), 0);
		grx500_writel(gsw, PCE_TABLE_KEY(5), i);
		grx500_writel(gsw, PAE_TABLE_CTRL, TABLE_BUSY | TABLE_WRITE);
		ret = grx500_table_idle(gsw, PAE_TABLE_CTRL);
		if (ret)
			return ret;
		grx500_writel(gsw, PAE_TABLE_CTRL, TABLE_BUSY);
		ret = grx500_table_idle(gsw, PAE_TABLE_CTRL);
		if (ret)
			return ret;
		if (__raw_readl(gsw + PCE_TABLE_KEY(4)) & BIT(15))
			return -EIO;
	}
	grx500_writel(gsw, PCE_TABLE_CTRL, 0);
	return 0;
}

static int grx500_r_pmac_setup(void __iomem *gsw)
{
	unsigned int flow, class, i;
	int ret;

	/* Channel1 consumes the LAN RX PMAC. Trust only its physical source;
	 * RX byte3 is not a TX map-enable field. Select default disabled map/
	 * class enables, subinterface0 and class0 explicitly.
	 */
	ret = grx500_table_idle(gsw, PMAC_TABLE_CTRL);
	if (ret)
		return ret;
	grx500_writel(gsw, PMAC_TABLE_ADDR, P2P_CHANNEL);
	grx500_writel(gsw, PMAC_TABLE_VAL(0), 0);
	grx500_writel(gsw, PMAC_TABLE_VAL(1), 0);
	grx500_writel(gsw, PMAC_TABLE_VAL(2), 0);
	grx500_writel(gsw, PMAC_TABLE_VAL(3), 0xffff);
	grx500_writel(gsw, PMAC_TABLE_VAL(4), 0x7d);
	grx500_writel(gsw, PMAC_TABLE_CTRL, TABLE_BUSY | TABLE_WRITE | 1);
	for (flow = 0; flow < 4; flow++) {
		for (class = 0; class < 16; class++) {
			ret = grx500_table_idle(gsw, PMAC_TABLE_CTRL);
			if (ret)
				return ret;
			grx500_writel(gsw, PMAC_TABLE_ADDR, (flow << 8) | (class << 4));
			for (i = 0; i < 5; i++)
				grx500_writel(gsw, PMAC_TABLE_VAL(i), i == 2 ? 1 : 0);
			grx500_writel(gsw, PMAC_TABLE_CTRL, TABLE_BUSY | TABLE_WRITE | 2);
		}
	}
	return grx500_table_idle(gsw, PMAC_TABLE_CTRL);
}

static int grx500_r_miss_setup(void __iomem *gsw)
{
	unsigned int rules = __raw_readl(gsw + 0x00f * 4) & 0x3ff;
	unsigned int macs = __raw_readl(gsw + 0x010 * 4) & 0x1fff;
	unsigned int port;
	u32 value;
	int ret;

	if (!rules || !macs)
		return -ENODEV;
	ret = grx500_r_clear_table(gsw, 0x0f, rules);
	if (!ret)
		ret = grx500_r_clear_table(gsw, 0x0b, macs);
	if (!ret)
		ret = grx500_r_sessions_clear(gsw);
	if (ret)
		return ret;
	/* Every ingress is transparent: existing customer/stacked VLAN bytes
	 * are payload. The private VID adds no tag at CPU or physical WAN egress.
	 */
	grx500_writel(gsw, GSW_PCE_CTRL, BIT(14) | BIT(3) | BIT(2) | BIT(1));
	grx500_writel(gsw, 0x457 * 4, BIT(9));
	grx500_pce_words_clear(gsw);
	grx500_writel(gsw, PCE_TABLE_ADDR, R_INTERNAL_VID);
	grx500_writel(gsw, PCE_TABLE_VAL(1), R_ACTIVE_PORTS);
	grx500_writel(gsw, PCE_TABLE_CTRL, TABLE_BUSY | TABLE_WRITE | 2);
	ret = grx500_table_idle(gsw, PCE_TABLE_CTRL);
	if (ret)
		return ret;
	for (port = 0; port < 16; port++) {
		/* Only internal sources carry trusted special tags. Learning and
		 * monitoring remain off, so received traffic has no physical copy.
		 */
		value = BIT(5) | 7;
		if (port != 15 && (R_ACTIVE_PORTS & BIT(port)))
			value |= BIT(11);
		grx500_writel(gsw, PCE_PORT_CTRL(port), value);
		grx500_writel(gsw, (0x483 + 10 * port) * 4, BIT(15));
		grx500_writel(gsw, (0x485 + 10 * port) * 4, 0);
		grx500_writel(gsw, (0x486 + 10 * port) * 4, R_INTERNAL_VID);
	}
	grx500_writel(gsw, 0x453 * 4, BIT(0));
	grx500_writel(gsw, PCE_MCAST_MAP, BIT(0));
	grx500_writel(gsw, PCE_UCAST_MAP, BIT(0));
	value = __raw_readl(gsw + 0xa47 * 4);
	grx500_writel(gsw, 0xa47 * 4, value & ~3);
	ret = grx500_r_pmac_setup(gsw);
	if (ret)
		return ret;
	/* Publication follows the complete CPU-only table/header baseline.
	 * The physical PHY is still held in reset and CPU rings start on open.
	 */
	for (port = 0; port < 16; port++) {
		if (!(R_ACTIVE_PORTS & BIT(port)))
			continue;
		grx500_writel(gsw, (0xa80 + 6 * port) * 4, BIT(0));
		grx500_writel(gsw, (0xbc0 + 6 * port) * 4, BIT(0));
	}
	return 0;
}

static int grx500_switch_init(struct grx500_eth_board *board, unsigned int index)
{
	void __iomem *gsw = board->gsw[index];
	u32 value;
	unsigned int i, j;
	int ret;

	/* Initialize independently of the bootloader's switch and parser state. */
	grx500_writel(gsw, 0, 1);
	ret = read_poll_timeout(__raw_readl, value, !(value & 1),
				1, 10000, false, gsw);
	if (ret)
		return ret;
	/* Publish neither switch until its owner installs the forwarding policy. */
	for (i = 0; i < 16; i++) {
		grx500_writel(gsw, (0xa80 + 6 * i) * 4, 0);
		grx500_writel(gsw, (0xbc0 + 6 * i) * 4, 0);
	}
	grx500_writel(gsw, GSW_TOP, BIT(15));
	/* Bit15 is the MDIO reset request, not an enable. */
	grx500_writel(gsw, GSW_TOP + 0x20, 0x800f);
	udelay(100);
	grx500_writel(gsw, GSW_TOP + 0x20, 0);
	for (i = 0; i < (index ? 1 : 5); i++) {
		grx500_writel(gsw, PHY_ADDR(i), 0x1801 + i);
		grx500_writel(gsw, (0x903 + 12 * i) * 4, 0x1c0);
	}
	grx500_writel(gsw, MDIO_POLL, 0x7e);
	grx500_writel(gsw, GSW_PMAC_CTRL4, 0);
	grx500_writel(gsw, GSW_PMAC_CTRL2, 4);
	if (index) {
		/* External WAN is forwarding port15 (physical MAC1, MDIO address1).
		 * Never interpret its packet bytes as a trusted internal special tag.
		 */
		for (i = 0; i < 16; i++) {
			value = __raw_readl(gsw + PCE_PORT_CTRL(i)) & ~BIT(11);
			grx500_writel(gsw, PCE_PORT_CTRL(i), value | (i == 15 ? 0 : BIT(11)));
		}
		ret = grx500_table_idle(gsw, PMAC_TABLE_CTRL);
		if (ret)
			return ret;
		grx500_writel(gsw, PMAC_TABLE_ADDR, 0);
		grx500_writel(gsw, PMAC_TABLE_VAL(4), 0x52);
		grx500_writel(gsw, PMAC_TABLE_VAL(3), BIT(15));
		grx500_writel(gsw, PMAC_TABLE_VAL(2), 0);
		grx500_writel(gsw, PMAC_TABLE_VAL(1), 0xa0);
		grx500_writel(gsw, PMAC_TABLE_VAL(0), 0);
		grx500_writel(gsw, PMAC_TABLE_CTRL, TABLE_BUSY | TABLE_WRITE | 1);
		ret = grx500_table_idle(gsw, PMAC_TABLE_CTRL);
		if (ret)
			return ret;
	}
	for (i = 1; i <= 15; i++)
		grx500_writel(gsw, (0x080 + 2 * i) * 4, 1);
	if (index) {
		/* Native RMON counting is enabled separately from forwarding.
		 * Preserve every other CPU-port setting and never clear a counter.
		 */
		value = __raw_readl(gsw + BM_CPU_PCFG);
		grx500_writel(gsw, BM_CPU_PCFG, value | BM_CNTEN);
	}
	value = __raw_readl(gsw + GSW_PCE_CTRL) & ~BIT(3);
	grx500_writel(gsw, GSW_PCE_CTRL, value);
	for (i = 0; i < ARRAY_SIZE(grx500_pce_microcode); i++) {
		ret = grx500_table_idle(gsw, PCE_TABLE_CTRL);
		if (ret)
			return ret;
		grx500_writel(gsw, PCE_TABLE_ADDR, i);
		for (j = 0; j < 4; j++)
			grx500_writel(gsw, PCE_TABLE_VAL(3 - j), grx500_pce_microcode[i][j]);
		grx500_writel(gsw, PCE_TABLE_CTRL, TABLE_BUSY | TABLE_WRITE);
		ret = grx500_table_idle(gsw, PCE_TABLE_CTRL);
		if (ret)
			return ret;
		/* Qualify the source-defined parser before enabling it. */
		grx500_writel(gsw, PCE_TABLE_CTRL, TABLE_BUSY);
		ret = grx500_table_idle(gsw, PCE_TABLE_CTRL);
		if (ret)
			return ret;
		for (j = 0; j < 4; j++)
			if ((__raw_readl(gsw + PCE_TABLE_VAL(3 - j)) & 0xffff) !=
			    grx500_pce_microcode[i][j])
				return -EIO;
	}
	grx500_writel(gsw, GSW_PCE_CTRL, value | BIT(3));
	return index ? grx500_r_miss_setup(gsw) : 0;
}

static int grx500_bus_read(struct mii_bus *bus, int address, int reg)
{
	struct grx500_eth *eth = bus->priv;

	return grx500_mdio_read(eth->gsw, address, reg);
}

static int grx500_bus_write(struct mii_bus *bus, int address, int reg, u16 data)
{
	struct grx500_eth *eth = bus->priv;
	u32 value;
	int ret;

	ret = read_poll_timeout(__raw_readl, value, !(value & BIT(12)),
				5, 10000, false, eth->gsw + MDIO_CTRL);
	if (ret)
		return ret;
	grx500_writel(eth->gsw, GSW_TOP + 0x18, data);
	grx500_writel(eth->gsw, MDIO_CTRL, BIT(12) | BIT(10) | (address << 5) | reg);
	/* Apply the same post-submit delay as the read path. */
	return read_poll_timeout(__raw_readl, value, !(value & BIT(12)),
				5, 10000, true, eth->gsw + MDIO_CTRL);
}

static void grx500_wan_set_pause(struct grx500_eth *eth, bool tx, bool rx)
{
	u32 mac = (tx ? MAC_CTRL0_FCON_TX : 0) |
		  (rx ? MAC_CTRL0_FCON_RX : 0);
	u32 phy = (tx ? PHY_ADDR_FCON_TX_EN : PHY_ADDR_FCON_TX) |
		  (rx ? PHY_ADDR_FCON_RX_EN : PHY_ADDR_FCON_RX);
	u32 value;

	lockdep_assert_held(&eth->phydev->lock);
	if (!mac)
		mac = MAC_CTRL0_FCON_OFF;
	/* WAN logical port 15 is physical MAC/PHY bank 0. GSWIP3 uses force
	 * value 1 for enable and 3 for disable; polling still owns speed/duplex.
	 */
	value = __raw_readl(eth->gsw + MAC_CTRL0);
	grx500_writel(eth->gsw, MAC_CTRL0, (value & ~MAC_CTRL0_FCON) | mac);
	value = __raw_readl(eth->gsw + PHY_ADDR(0));
	grx500_writel(eth->gsw, PHY_ADDR(0),
		      (value & ~(PHY_ADDR_FCON_TX | PHY_ADDR_FCON_RX)) | phy);
	value = __raw_readl(eth->gsw + SDMA_PCTRL(15));
	grx500_writel(eth->gsw, SDMA_PCTRL(15),
		      (value & ~SDMA_PCTRL_FCEN) | (tx ? SDMA_PCTRL_FCEN : 0));
}

static void grx500_wan_apply_pause(struct grx500_eth *eth)
{
	struct phy_device *phydev = eth->phydev;
	bool tx = false, rx = false;

	lockdep_assert_held(&phydev->lock);
	if (READ_ONCE(eth->running) && phydev->link &&
	    phydev->duplex == DUPLEX_FULL) {
		if (!eth->pause_autoneg) {
			tx = eth->tx_pause;
			rx = eth->rx_pause;
		} else if (phydev->autoneg == AUTONEG_ENABLE &&
			   phydev->autoneg_complete) {
			phy_get_pause(phydev, &tx, &rx);
		}
	}
	grx500_wan_set_pause(eth, tx, rx);
}

static void grx500_wan_adjust_link(struct net_device *netdev)
{
	grx500_wan_apply_pause(netdev_priv(netdev));
	phy_print_status(netdev->phydev);
}

static void grx500_wan_get_pauseparam(struct net_device *netdev,
				    struct ethtool_pauseparam *pause)
{
	struct grx500_eth *eth = netdev_priv(netdev);

	mutex_lock(&eth->phydev->lock);
	pause->autoneg = eth->pause_autoneg;
	pause->tx_pause = eth->tx_pause;
	pause->rx_pause = eth->rx_pause;
	mutex_unlock(&eth->phydev->lock);
}

static int grx500_wan_set_pauseparam(struct net_device *netdev,
				   struct ethtool_pauseparam *pause)
{
	struct grx500_eth *eth = netdev_priv(netdev);
	struct phy_device *phydev = eth->phydev;

	if (!phy_validate_pause(phydev, pause))
		return -EINVAL;

	mutex_lock(&phydev->lock);
	eth->pause_autoneg = !!pause->autoneg;
	eth->tx_pause = !!pause->tx_pause;
	eth->rx_pause = !!pause->rx_pause;
	mutex_unlock(&phydev->lock);

	/* Like phylink, let phylib restart negotiation when advertising changes.
	 * A manual-policy change also needs applying without a link transition.
	 */
	phy_set_asym_pause(phydev, pause->rx_pause, pause->tx_pause);
	mutex_lock(&phydev->lock);
	grx500_wan_apply_pause(eth);
	mutex_unlock(&phydev->lock);
	return 0;
}

static int grx500_wan_get_regs_len(struct net_device *netdev)
{
	return GRX500_PAUSE_REGS_COUNT * sizeof(u32);
}

static void grx500_wan_get_regs(struct net_device *netdev,
				struct ethtool_regs *regs, void *data)
{
	struct grx500_eth *eth = netdev_priv(netdev);
	u32 *values = data;
	unsigned int bank, port, mac, reg;
	void __iomem *base;

	/* Version 2: native-endian u32 words, GSW-L then GSW-R. Each bank
	 * starts with SDMA_FCTHR1..4, followed by physical ports 2..5 (L) or
	 * 15 (R). Each port contributes MAC_CTRL0, MAC_PSTAT, PHY_ADDR,
	 * MDIO_STAT, SDMA_PCTRL, SDMA_PFCTHR8, SDMA_PFCTHR9, in that order.
	 * The original 43 words are followed by the attached WAN PHY's BMCR,
	 * ADVERTISE and LPA. Each PHY word contains a 16-bit register value or
	 * a negative signed 32-bit errno. No latch-low BMSR read is performed.
	 * These are non-atomic snapshots; link/configuration changes can occur
	 * between reads. PHY reads use the existing phylib and MDIO bus locks.
	 */
	regs->version = 2;
	for (bank = 0; bank < ARRAY_SIZE(eth->board->gsw); bank++) {
		base = eth->board->gsw[bank];
		for (reg = 0xb41; reg <= 0xb44; reg++)
			*values++ = __raw_readl(base + reg * 4);
		for (port = bank ? 15 : 2; port <= (bank ? 15 : 5); port++) {
			mac = bank ? 0 : port - 1;
			*values++ = __raw_readl(base + MAC_CTRL0 + mac * 12 * 4);
			*values++ = __raw_readl(base + (0x900 + mac * 12) * 4);
			*values++ = __raw_readl(base + PHY_ADDR(mac));
			*values++ = __raw_readl(base + PHY_ADDR(mac) + 4);
			*values++ = __raw_readl(base + SDMA_PCTRL(port));
			*values++ = __raw_readl(base + (0xb80 + port * 2) * 4);
			*values++ = __raw_readl(base + (0xb81 + port * 2) * 4);
		}
	}
	mutex_lock(&eth->phydev->lock);
	*values++ = phy_read(eth->phydev, MII_BMCR);
	*values++ = phy_read(eth->phydev, MII_ADVERTISE);
	*values++ = phy_read(eth->phydev, MII_LPA);
	mutex_unlock(&eth->phydev->lock);
}

static int grx500_rmon_read32(struct grx500_eth *eth, u8 table, u8 offset,
			    u32 *value)
{
	int ret;

	lockdep_assert_held(&eth->bm_lock);
	ret = grx500_table_idle(eth->gsw, BM_RAM_CTRL);
	if (ret)
		return ret;
	grx500_writel(eth->gsw, BM_RAM_ADDR, offset);
	/* OPMOD=0 reads without resetting counters. */
	grx500_writel(eth->gsw, BM_RAM_CTRL, TABLE_BUSY | table);
	ret = grx500_table_idle(eth->gsw, BM_RAM_CTRL);
	if (ret)
		return ret;
	*value = (__raw_readl(eth->gsw + BM_RAM_VAL(0)) & 0xffff) |
		 ((__raw_readl(eth->gsw + BM_RAM_VAL(1)) & 0xffff) << 16);
	return 0;
}

static int grx500_rmon_read64(struct grx500_eth *eth, u8 table, u8 offset,
			    u64 *value)
{
	u32 high, low, high_after;
	int attempt, ret;

	/* The free-running byte counter spans two independently read entries. */
	for (attempt = 0; attempt < 3; attempt++) {
		ret = grx500_rmon_read32(eth, table, offset + 1, &high);
		if (ret)
			return ret;
		ret = grx500_rmon_read32(eth, table, offset, &low);
		if (ret)
			return ret;
		ret = grx500_rmon_read32(eth, table, offset + 1, &high_after);
		if (ret)
			return ret;
		if (high == high_after) {
			*value = ((u64)high << 32) | low;
			return 0;
		}
	}
	return -EAGAIN;
}

static void grx500_wan_get_strings(struct net_device *netdev, u32 stringset,
				   u8 *data)
{
	unsigned int i;

	if (stringset != ETH_SS_STATS)
		return;
	for (i = 0; i < ARRAY_SIZE(grx500_wan_rmon_counters); i++)
		ethtool_puts(&data, grx500_wan_rmon_counters[i].name);
	ethtool_puts(&data, "rmon_read_failures");
	ethtool_puts(&data, "rmon_last_read_ok");
	for (i = 0; i < ARRAY_SIZE(grx500_cpu_rmon_counters); i++)
		ethtool_puts(&data, grx500_cpu_rmon_counters[i].name);
	ethtool_puts(&data, "cpu_port_rmon_read_failures");
	ethtool_puts(&data, "cpu_port_rmon_last_read_ok");
	ethtool_puts(&data, "cpu_port_rmon_enabled");
}

static int grx500_wan_get_sset_count(struct net_device *netdev, int sset)
{
	if (sset != ETH_SS_STATS)
		return -EOPNOTSUPP;
	return ARRAY_SIZE(grx500_wan_rmon_counters) + 2 +
		ARRAY_SIZE(grx500_cpu_rmon_counters) + 3;
}

static void grx500_cpu_get_rmon_stats(struct grx500_eth *eth, u64 *data)
{
	struct grx500_cpu_rmon_stats *stats = &eth->cpu_rmon;
	u64 sample[ARRAY_SIZE(grx500_cpu_rmon_counters)];
	unsigned int i;
	bool enabled;
	int ret = 0;

	mutex_lock(&eth->bm_lock);
	enabled = !!(__raw_readl(eth->gsw + BM_CPU_PCFG) & BM_CNTEN);
	for (i = 0; i < ARRAY_SIZE(grx500_cpu_rmon_counters); i++) {
		const struct grx500_rmon_counter *counter = &grx500_cpu_rmon_counters[i];
		u32 value;

		if (counter->bytes) {
			ret = grx500_rmon_read64(eth, CPU_RMON_TABLE,
						counter->offset, &sample[i]);
		} else {
			ret = grx500_rmon_read32(eth, CPU_RMON_TABLE,
						counter->offset, &value);
			if (!ret)
				sample[i] = value;
		}
		if (ret)
			break;
	}
	if (ret) {
		stats->read_failures++;
		dev_warn_ratelimited(&eth->netdev->dev,
				     "CPU-port RMON read failed (%d)\n", ret);
	} else {
		memcpy(stats->counters, sample, sizeof(sample));
	}
	memcpy(data, stats->counters, sizeof(stats->counters));
	data[ARRAY_SIZE(grx500_cpu_rmon_counters)] = stats->read_failures;
	data[ARRAY_SIZE(grx500_cpu_rmon_counters) + 1] = !ret;
	data[ARRAY_SIZE(grx500_cpu_rmon_counters) + 2] = enabled;
	mutex_unlock(&eth->bm_lock);
}

static void grx500_wan_get_ethtool_stats(struct net_device *netdev,
					 struct ethtool_stats *unused, u64 *data)
{
	struct grx500_eth *eth = netdev_priv(netdev);
	struct grx500_rmon_stats *stats = &eth->rmon;
	u64 sample[ARRAY_SIZE(grx500_wan_rmon_counters)];
	unsigned int i;
	int ret = 0;

	mutex_lock(&eth->bm_lock);
	for (i = 0; i < ARRAY_SIZE(grx500_wan_rmon_counters); i++) {
		const struct grx500_rmon_counter *counter = &grx500_wan_rmon_counters[i];
		u32 value;

		if (counter->bytes) {
			ret = grx500_rmon_read64(eth, WAN_RMON_TABLE,
						counter->offset, &sample[i]);
		} else {
			ret = grx500_rmon_read32(eth, WAN_RMON_TABLE,
						counter->offset, &value);
			if (!ret)
				sample[i] = value;
		}
		if (ret)
			break;
	}
	/* ethtool has no error return here. Preserve the last complete read and
	 * expose freshness separately from the hardware counters.
	 */
	if (ret) {
		stats->read_failures++;
		dev_warn_ratelimited(&netdev->dev, "RMON read failed (%d)\n", ret);
	} else {
		memcpy(stats->counters, sample, sizeof(sample));
	}
	memcpy(data, stats->counters, sizeof(stats->counters));
	data[ARRAY_SIZE(grx500_wan_rmon_counters)] = stats->read_failures;
	data[ARRAY_SIZE(grx500_wan_rmon_counters) + 1] = !ret;
	mutex_unlock(&eth->bm_lock);
	grx500_cpu_get_rmon_stats(eth,
		data + ARRAY_SIZE(grx500_wan_rmon_counters) + 2);
}

static const struct ethtool_ops grx500_wan_ethtool_ops = {
	.get_link = ethtool_op_get_link,
	.get_link_ksettings = phy_ethtool_get_link_ksettings,
	.set_link_ksettings = phy_ethtool_set_link_ksettings,
	.nway_reset = phy_ethtool_nway_reset,
	.get_pauseparam = grx500_wan_get_pauseparam,
	.set_pauseparam = grx500_wan_set_pauseparam,
	.get_regs_len = grx500_wan_get_regs_len,
	.get_regs = grx500_wan_get_regs,
	.get_strings = grx500_wan_get_strings,
	.get_sset_count = grx500_wan_get_sset_count,
	.get_ethtool_stats = grx500_wan_get_ethtool_stats,
};

static int grx500_mdio_register(struct grx500_eth *eth)
{
	struct device_node *node = eth->netdev->dev.of_node;
	struct device_node *mdio = of_get_child_by_name(node, "mdio");
	struct mii_bus *bus;
	int ret;

	if (!mdio)
		return -ENODEV;
	bus = mdiobus_alloc();
	if (!bus) {
		of_node_put(mdio);
		return -ENOMEM;
	}
	bus->name = "GRX500 MDIO";
	snprintf(bus->id, MII_BUS_ID_SIZE, "%s-%u", dev_name(eth->device), eth->wan);
	bus->parent = eth->device;
	bus->priv = eth;
	bus->read = grx500_bus_read;
	bus->write = grx500_bus_write;
	ret = of_mdiobus_register(bus, mdio);
	of_node_put(mdio);
	if (ret) {
		mdiobus_free(bus);
		return ret;
	}
	eth->mdio = bus;
	if (eth->wan) {
		struct device_node *phy = of_parse_phandle(node, "phy-handle", 0);
		phy_interface_t mode;

		ret = of_get_phy_mode(node, &mode);
		if (ret) {
			of_node_put(phy);
			return ret;
		}
		eth->phydev = of_phy_connect(eth->netdev, phy, grx500_wan_adjust_link, 0, mode);
		of_node_put(phy);
		if (!eth->phydev)
			return -ENODEV;
		eth->pause_autoneg = true;
		eth->tx_pause = true;
		eth->rx_pause = true;
		phy_support_asym_pause(eth->phydev);
		eth->netdev->ethtool_ops = &grx500_wan_ethtool_ops;
	}
	return 0;
}

static void grx500_rearm_rx(struct grx500_eth *eth, unsigned int index)
{
	struct grx500_desc *desc = &eth->memory->rx[index];
	dma_addr_t address = eth->rx_data_dma + index * BUF_SIZE;

	dma_sync_single_range_for_device(eth->device, eth->rx_data_dma,
					 index * BUF_SIZE, BUF_SIZE, DMA_FROM_DEVICE);
	desc->dw0 = 0;
	desc->dw1 = 0;
	desc->address = cpu_to_be32(address);
	dma_wmb();
	WRITE_ONCE(desc->status, cpu_to_be32(DESC_OWN | BUF_SIZE));
}

static int grx500_dma_prepare(struct grx500_dma *dma, unsigned int channel,
			      dma_addr_t ring, unsigned int count, bool p2p)
{
	void __iomem *base = dma->base;
	unsigned long flags;
	u32 value, class;
	int ret;

	/* Only a stopped channel may change its ring. Controller configuration
	 * is shared with live channel0/P2P users and belongs exclusively to probe.
	 */
	spin_lock_irqsave(&dma->lock, flags);
	grx500_writel(base, DMA_CS, channel);
	value = __raw_readl(base + DMA_CCTRL);
	if (value & DMA_ON) {
		ret = -EBUSY;
		goto out;
	}
	class = value & DMA_CLASS_MASK;
	grx500_writel(base, DMA_CCTRL, class | DMA_RESET);
	ret = read_poll_timeout_atomic(__raw_readl, value, !(value & DMA_RESET),
				       1, 10000, false, base + DMA_CCTRL);
	if (ret)
		goto out;
	grx500_writel(base, DMA_CDBA, ring);
	grx500_writel(base, DMA_CDLEN, count);
	grx500_writel(base, DMA_CIE, 0);
	grx500_writel(base, DMA_CIS, U32_MAX);
	grx500_writel(base, DMA_IRNCR, BIT(channel));
	value = class | BIT(16) | (dma->transmit ? BIT(8) : 0);
	if (p2p) {
		/* Uniform capacity also satisfies implementations where CGBL is
		 * shared across a TX instance rather than banked per channel.
		 */
		grx500_writel(base, DMA_CGBL, BUF_SIZE);
		value |= DMA_P2P;
	}
	grx500_writel(base, DMA_CCTRL, value);
out:
	spin_unlock_irqrestore(&dma->lock, flags);
	return ret;
}

static int grx500_backpressure_map(void __iomem *gsw, unsigned int channel,
				   u32 ports, u32 queues)
{
	u32 values[] = { ports, queues & 0xffff, queues >> 16 };
	unsigned int i;
	int ret;

	ret = grx500_table_idle(gsw, PMAC_TABLE_CTRL);
	if (ret)
		return ret;
	grx500_writel(gsw, PMAC_TABLE_ADDR, channel);
	for (i = 0; i < ARRAY_SIZE(values); i++)
		grx500_writel(gsw, PMAC_TABLE_VAL(i), values[i]);
	grx500_writel(gsw, PMAC_TABLE_CTRL, TABLE_BUSY | TABLE_WRITE);
	ret = grx500_table_idle(gsw, PMAC_TABLE_CTRL);
	if (ret)
		return ret;
	grx500_writel(gsw, PMAC_TABLE_ADDR, channel);
	grx500_writel(gsw, PMAC_TABLE_CTRL, TABLE_BUSY);
	ret = grx500_table_idle(gsw, PMAC_TABLE_CTRL);
	if (ret)
		return ret;
	for (i = 0; i < ARRAY_SIZE(values); i++)
		if ((__raw_readl(gsw + PMAC_TABLE_VAL(i)) & 0xffff) != values[i])
			return -EIO;
	return 0;
}

/* Probe exclusively owns GSW-R PCE/PMAC staging until board->ready. Couple
 * backpressure to the CPU and WAN queues before either transmitter can start.
 * BM reads still share their lock with ethtool on the registered WAN netdev.
 */
static int grx500_backpressure_init(struct grx500_eth_board *board)
{
	static const unsigned int ports[] = { 0, 15 };
	struct grx500_eth *wan = netdev_priv(board->netdev[1]);
	void __iomem *gsw = board->gsw[1];
	u32 queues[ARRAY_SIZE(ports)] = {}, value;
	unsigned int p, bypass, tc, queue, count, owner, redirect;
	int ret;

	count = (__raw_readl(gsw + 0x007 * 4) >> 8) & 0x7f;
	for (p = 0; p < ARRAY_SIZE(ports); p++) {
		for (bypass = 0; bypass < 2; bypass++) {
			for (tc = 0; tc < 16; tc++) {
				ret = grx500_table_idle(gsw, PCE_TABLE_CTRL);
				if (!ret)
					ret = grx500_table_idle(gsw, PAE_TABLE_CTRL);
				if (ret)
					return ret;
				grx500_writel(gsw, PCE_TABLE_ADDR,
					      ports[p] << 4 | tc | bypass << 8);
				grx500_writel(gsw, PCE_TABLE_CTRL, TABLE_BUSY | 0x11);
				ret = grx500_table_idle(gsw, PCE_TABLE_CTRL);
				if (ret)
					return ret;
				queue = __raw_readl(gsw + PCE_TABLE_VAL(0)) & 0x3f;
				grx500_writel(gsw, PCE_TABLE_CTRL, 0);
				if (queue >= count || queue >= 32)
					return -ERANGE;
				queues[p] |= BIT(queue);
			}
		}
	}
	mutex_lock(&wan->bm_lock);
	for (p = 0; p < ARRAY_SIZE(ports); p++) {
		for (queue = 0; queue < 32; queue++) {
			if (!(queues[p] & BIT(queue)))
				continue;
			ret = grx500_rmon_read32(wan, 0xe, queue, &value);
			if (ret)
				goto out;
			owner = (value >> 8) & 0xf;
			redirect = (value & 7) | ((value >> 1) & 8);
			if (owner != ports[p] || redirect != ports[p]) {
				ret = -EINVAL;
				goto out;
			}
		}
	}
out:
	mutex_unlock(&wan->bm_lock);
	if (ret)
		return ret;
	ret = grx500_backpressure_map(gsw, 0, BIT(0), queues[1]);
	if (!ret)
		ret = grx500_backpressure_map(gsw, 1, GENMASK(5, 2),
					     queues[0] | queues[1]);
	return ret;
}

static void grx500_p2p_stop(struct grx500_eth_board *board)
{
	int rx, tx;

	/* RX stops producing before TX is stopped. Attempt both fences even on
	 * error; a timeout cannot authorize reclaiming their shared allocation.
	 */
	rx = grx500_dma_stop(&board->rx_dma[0], P2P_CHANNEL);
	tx = grx500_dma_stop(&board->tx_dma[1], P2P_CHANNEL);
	if (rx || tx)
		panic("GRX500 Ethernet: P2P endpoints did not stop; reset required");
}

static int grx500_p2p_start(struct grx500_eth_board *board)
{
	struct grx500_p2p_memory *memory = board->p2p;
	unsigned int i;
	int ret;

	grx500_p2p_stop(board);
	memset(memory, 0, sizeof(*memory));
	for (i = 0; i < RX_COUNT; i++) {
		dma_addr_t address = board->p2p_dma +
			offsetof(struct grx500_p2p_memory, data) + i * BUF_SIZE;

		memory->desc[i].address = cpu_to_be32(address);
		memory->desc[i].status = cpu_to_be32(DESC_OWN | BUF_SIZE);
	}
	ret = grx500_dma_prepare(&board->rx_dma[0], P2P_CHANNEL,
				 board->p2p_dma, RX_COUNT, false);
	if (!ret)
		ret = grx500_dma_prepare(&board->tx_dma[1], P2P_CHANNEL,
					 board->p2p_dma, RX_COUNT, true);
	if (ret) {
		grx500_p2p_stop(board);
		return ret;
	}
	dma_wmb();
	/* The consumer is ready before the producer can fill a descriptor. */
	grx500_dma_start(&board->tx_dma[1], P2P_CHANNEL);
	grx500_dma_start(&board->rx_dma[0], P2P_CHANNEL);
	return 0;
}

static unsigned int grx500_reap_tx(struct grx500_eth *eth)
{
	unsigned int completed = 0, bytes = 0;

	while (eth->tx_pending) {
		unsigned int index = eth->tx_tail;
		struct grx500_tx_buffer *buffer = &eth->tx_buffer[index];
		u32 status = be32_to_cpu(READ_ONCE(eth->memory->tx[index].status));

		if (status & DESC_OWN)
			break;
		dma_rmb();
		if (status & DESC_DONE) {
			eth->stats.tx_packets++;
			eth->stats.tx_bytes += buffer->skb->len;
		} else {
			eth->stats.tx_errors++;
		}
		bytes += buffer->skb->len;
		dma_unmap_single(eth->device, buffer->dma,
				 buffer->mapped_length, DMA_TO_DEVICE);
		dev_consume_skb_any(buffer->skb);
		buffer->skb = NULL;
		eth->tx_tail = (index + 1) % TX_COUNT;
		eth->tx_pending--;
		completed++;
	}
	netdev_completed_queue(eth->netdev, completed, bytes);
	if (eth->running && eth->tx_pending < TX_COUNT && netif_queue_stopped(eth->netdev))
		netif_wake_queue(eth->netdev);
	return completed;
}

static bool grx500_rx_ready(struct grx500_eth *eth)
{
	unsigned int index = READ_ONCE(eth->rx_next);
	u32 status = be32_to_cpu(READ_ONCE(eth->memory->rx[index].status));

	return !(status & DESC_OWN) && (status & DESC_DONE);
}

static bool grx500_tx_ready(struct grx500_eth *eth)
{
	return eth->tx_pending &&
		!(be32_to_cpu(READ_ONCE(eth->memory->tx[eth->tx_tail].status)) & DESC_OWN);
}

/* The netdev lock serializes NAPI/TX state; each controller lock protects
 * channel selection against the parent's independent P2P endpoint operations.
 * Lock order is eth->lock, then one controller lock, never the reverse.
 */
static void grx500_dma_irq_mask(struct grx500_dma *dma, u32 mask)
{
	void __iomem *base = dma->base;
	unsigned long flags;
	u32 value;

	spin_lock_irqsave(&dma->lock, flags);
	grx500_writel(base, DMA_CS, 0);
	grx500_writel(base, DMA_CIE, mask);
	value = __raw_readl(base + DMA_IRNEN) & ~BIT(0);
	grx500_writel(base, DMA_IRNEN, value | (mask ? BIT(0) : 0));
	spin_unlock_irqrestore(&dma->lock, flags);
}

static u32 grx500_dma_ack(struct grx500_dma *dma)
{
	void __iomem *base = dma->base;
	unsigned long flags;
	u32 status;

	spin_lock_irqsave(&dma->lock, flags);
	grx500_writel(base, DMA_CS, 0);
	status = __raw_readl(base + DMA_CIS);
	grx500_writel(base, DMA_CIS, status & DMA_CI_ALL);
	grx500_writel(base, DMA_IRNCR, BIT(0));
	spin_unlock_irqrestore(&dma->lock, flags);
	return status;
}

static void grx500_mask_irqs(struct grx500_eth *eth)
{
	grx500_dma_irq_mask(eth->rx_dma, 0);
	grx500_dma_irq_mask(eth->tx_dma, 0);
}

static void grx500_capture_events(struct grx500_eth *eth)
{
	u32 rx = grx500_dma_ack(eth->rx_dma);
	u32 tx = grx500_dma_ack(eth->tx_dma);

	/* Interrupts may precede descriptor writeback. A coalesced status bit can
	 * also describe work already drained, so it is only a bounded recheck
	 * hint, never an obligation to spin NAPI until another packet appears.
	 */
	if (eth->rx_running && ((rx & (DMA_EOP | DMA_DESCPT)) || (tx & DMA_EOP)))
		schedule_delayed_work(&eth->writeback_work, 1);
}

static void grx500_unmask_irqs(struct grx500_eth *eth)
{
	grx500_dma_irq_mask(eth->rx_dma, DMA_EOP | DMA_DESCPT);
	grx500_dma_irq_mask(eth->tx_dma, eth->running ? DMA_EOP : 0);
}

static irqreturn_t grx500_interrupt(int irq, void *data)
{
	struct grx500_eth *eth = data;
	unsigned long flags;

	spin_lock_irqsave(&eth->lock, flags);
	grx500_mask_irqs(eth);
	grx500_capture_events(eth);
	if (eth->rx_running && napi_schedule_prep(&eth->napi))
		__napi_schedule(&eth->napi);
	spin_unlock_irqrestore(&eth->lock, flags);
	return IRQ_HANDLED;
}

static void grx500_schedule_ready(struct grx500_eth *eth)
{
	unsigned long flags;

	spin_lock_irqsave(&eth->lock, flags);
	if (eth->rx_running && (grx500_rx_ready(eth) || grx500_tx_ready(eth)) &&
	    napi_schedule_prep(&eth->napi)) {
		grx500_mask_irqs(eth);
		__napi_schedule(&eth->napi);
	}
	spin_unlock_irqrestore(&eth->lock, flags);
}

static void grx500_writeback_work(struct work_struct *work)
{
	struct grx500_eth *eth = container_of(to_delayed_work(work), struct grx500_eth, writeback_work);

	grx500_schedule_ready(eth);
}

static int grx500_receive(struct grx500_eth *eth, int budget)
{
	int received;

	for (received = 0; received < budget; received++) {
		unsigned int index = eth->rx_next, length, offset, source;
		struct grx500_desc *desc = &eth->memory->rx[index];
		struct grx500_eth *ingress = eth;
		struct sk_buff *skb = NULL;
		u32 status = be32_to_cpu(READ_ONCE(desc->status));
		unsigned long flags;
		const u8 *frame;
		bool valid;

		if ((status & DESC_OWN) || !(status & DESC_DONE))
			break;
		dma_rmb();
		dma_sync_single_range_for_cpu(eth->device, eth->rx_data_dma,
					     index * BUF_SIZE, BUF_SIZE, DMA_FROM_DEVICE);
		length = status & 0xffff;
		offset = (status >> 23) & 7;
		frame = eth->rx_data[index] + offset;
		valid = (status & (DESC_SOP | DESC_EOP)) == (DESC_SOP | DESC_EOP) &&
			length >= ETH_HLEN + PMAC_SIZE && length <= MAX_FRAME &&
			length + offset <= BUF_SIZE;
		if (valid && eth->wan) {
			u32 dw1 = be32_to_cpu(READ_ONCE(desc->dw1));

			/* An inherited PAE return may already be translated. Never
			 * feed it through ordinary IP receive a second time.
			 */
			if (((dw1 >> 8) & 0xf) == PAE_RETURN_PORT ||
			    ((dw1 >> 4) & 0xf) == PAE_RETURN_MARK)
				valid = false;
		}
		if (valid) {
			/* Both CPU RX paths have one PMAC format. WAN physical ingress
			 * cannot supply this trusted header. LAN misses retain the tag
			 * for DSA, including GSW-L's VLAN classification result.
			 */
			source = (frame[2] >> 4) & 0xf;
			if (eth->wan && source == 15) {
				frame += PMAC_SIZE;
				length -= PMAC_SIZE;
			} else if (source >= 2 && source <= 5) {
				ingress = netdev_priv(eth->board->netdev[0]);
			} else {
				valid = false;
			}
		}
		if (valid && READ_ONCE(ingress->running)) {
			skb = napi_alloc_skb(&eth->napi, length);
			if (skb) {
				skb_put_data(skb, frame, length);
				skb->protocol = eth_type_trans(skb, ingress->netdev);
			}
		}
		/* Account by the original ingress, not the DMA transporting a miss. */
		spin_lock_irqsave(&ingress->lock, flags);
		if (skb) {
			ingress->stats.rx_packets++;
			ingress->stats.rx_bytes += length;
		} else {
			ingress->stats.rx_dropped++;
			if (!valid)
				ingress->stats.rx_errors++;
		}
		spin_unlock_irqrestore(&ingress->lock, flags);
		if (!valid)
			dev_warn_ratelimited(eth->device,
					     "invalid RX descriptor or PMAC %08x\n", status);
		grx500_rearm_rx(eth, index);
		WRITE_ONCE(eth->rx_next, (index + 1) % RX_COUNT);
		if (skb)
			napi_gro_receive(&eth->napi, skb);
	}
	return received;
}

static int grx500_poll(struct napi_struct *napi, int budget)
{
	struct grx500_eth *eth = container_of(napi, struct grx500_eth, napi);
	unsigned long flags;
	bool pending, running;
	int received;

	spin_lock_irqsave(&eth->lock, flags);
	grx500_capture_events(eth);
	grx500_reap_tx(eth);
	spin_unlock_irqrestore(&eth->lock, flags);
	/* A zero budget permits only TX completion handling, never GRO/complete. */
	if (!budget)
		return 0;
	received = grx500_receive(eth, budget);
	spin_lock_irqsave(&eth->lock, flags);
	grx500_capture_events(eth);
	grx500_reap_tx(eth);
	pending = grx500_rx_ready(eth) || grx500_tx_ready(eth);
	running = eth->rx_running;
	spin_unlock_irqrestore(&eth->lock, flags);
	if (running && (received == budget || pending))
		return budget;
	/* GRO completion can enter the TX path; never hold eth->lock here. */
	if (napi_complete_done(napi, received)) {
		spin_lock_irqsave(&eth->lock, flags);
		if (eth->rx_running) {
			grx500_unmask_irqs(eth);
			/* Close the masked-arrival window, including delayed writeback. */
			if ((grx500_rx_ready(eth) || grx500_tx_ready(eth)) &&
			    napi_schedule_prep(napi)) {
				grx500_mask_irqs(eth);
				__napi_schedule(napi);
			}
		}
		spin_unlock_irqrestore(&eth->lock, flags);
	}
	return min(received, budget - 1);
}

static void grx500_recheck_work(struct work_struct *work)
{
	struct grx500_eth *eth = container_of(to_delayed_work(work), struct grx500_eth, recheck_work);
	unsigned long flags;

	spin_lock_irqsave(&eth->lock, flags);
	if (eth->rx_running)
		schedule_delayed_work(&eth->recheck_work, HZ);
	spin_unlock_irqrestore(&eth->lock, flags);
	/* Ownership-only backstop for interrupt-before-writeback; phylink owns
	 * each LAN user's carrier, and this DMA conduit has a fixed CPU link.
	 */
	grx500_schedule_ready(eth);
}

static int grx500_rx_start(struct grx500_eth *eth)
{
	unsigned long flags;
	unsigned int i;
	int ret;

	if (eth->rx_running)
		return 0;
	if (grx500_dma_stop(eth->rx_dma, 0))
		panic("GRX500 Ethernet: CPU RX did not stop; reset required");
	memset(eth->memory->rx, 0, sizeof(eth->memory->rx));
	eth->rx_next = 0;
	for (i = 0; i < RX_COUNT; i++)
		grx500_rearm_rx(eth, i);
	ret = grx500_dma_prepare(eth->rx_dma, 0, eth->memory_dma +
			       offsetof(struct grx500_dma_memory, rx), RX_COUNT, false);
	if (ret)
		return ret;
	napi_enable(&eth->napi);
	spin_lock_irqsave(&eth->lock, flags);
	eth->rx_running = true;
	grx500_unmask_irqs(eth);
	dma_wmb();
	grx500_dma_start(eth->rx_dma, 0);
	spin_unlock_irqrestore(&eth->lock, flags);
	schedule_delayed_work(&eth->recheck_work, 0);
	return 0;
}

static void grx500_rx_stop(struct grx500_eth *eth)
{
	unsigned long flags;

	if (!eth->rx_running)
		return;
	spin_lock_irqsave(&eth->lock, flags);
	eth->rx_running = false;
	grx500_mask_irqs(eth);
	spin_unlock_irqrestore(&eth->lock, flags);
	synchronize_irq(eth->rx_irq);
	synchronize_irq(eth->tx_irq);
	cancel_delayed_work_sync(&eth->recheck_work);
	cancel_delayed_work_sync(&eth->writeback_work);
	napi_disable(&eth->napi);
	synchronize_net();
	if (grx500_dma_stop(eth->rx_dma, 0))
		panic("GRX500 Ethernet: CPU RX did not stop; reset required");
}

static int grx500_open(struct net_device *netdev)
{
	struct grx500_eth *eth = netdev_priv(netdev);
	struct grx500_eth_board *board = eth->board;
	struct grx500_eth *lan, *wan;
	unsigned long flags;
	int ret;

	ASSERT_RTNL();
	if (!READ_ONCE(board->ready))
		return -ENODEV;
	lan = netdev_priv(board->netdev[0]);
	wan = netdev_priv(board->netdev[1]);
	if (grx500_dma_stop(eth->tx_dma, 0))
		panic("GRX500 Ethernet: CPU TX did not stop; reset required");
	/* WAN RX may already be active for LAN misses. Never clear its ring. */
	spin_lock_irqsave(&eth->lock, flags);
	memset(eth->memory->tx, 0, sizeof(eth->memory->tx));
	eth->tx_head = 0;
	eth->tx_tail = 0;
	eth->tx_pending = 0;
	spin_unlock_irqrestore(&eth->lock, flags);
	ret = grx500_dma_prepare(eth->tx_dma, 0, eth->memory_dma +
			       offsetof(struct grx500_dma_memory, tx), TX_COUNT, false);
	if (ret)
		return ret;
	ret = grx500_rx_start(wan);
	if (ret)
		goto stop_rx;
	if (!eth->wan) {
		ret = grx500_rx_start(eth);
		if (!ret)
			ret = grx500_p2p_start(board);
		if (ret)
			goto stop_rx;
	}
	spin_lock_irqsave(&eth->lock, flags);
	eth->running = true;
	grx500_unmask_irqs(eth);
	spin_unlock_irqrestore(&eth->lock, flags);
	netif_start_queue(netdev);
	if (eth->phydev)
		phy_start(eth->phydev);
	else
		netif_carrier_on(netdev);
	netdev_info(netdev, "DMA%u started, CPU rings at %pad\n",
		    eth->wan ? 1 : 2, &eth->memory_dma);
	return 0;

stop_rx:
	if (!eth->wan) {
		grx500_p2p_stop(board);
		grx500_rx_stop(eth);
	}
	if (!wan->running && !lan->running)
		grx500_rx_stop(wan);
	return ret;
}

static void grx500_halt(struct net_device *netdev)
{
	struct grx500_eth *eth = netdev_priv(netdev);
	struct grx500_eth_board *board = eth->board;
	struct grx500_eth *lan = netdev_priv(board->netdev[0]);
	struct grx500_eth *wan = netdev_priv(board->netdev[1]);
	unsigned long flags;
	unsigned int i;

	ASSERT_RTNL();
	netif_tx_disable(netdev);
	spin_lock_irqsave(&eth->lock, flags);
	eth->running = false;
	grx500_dma_irq_mask(eth->tx_dma, 0);
	spin_unlock_irqrestore(&eth->lock, flags);
	if (eth->phydev)
		phy_stop(eth->phydev);
	if (grx500_dma_stop(eth->tx_dma, 0))
		panic("GRX500 Ethernet: CPU TX did not stop; reset required");
	if (!eth->wan) {
		/* Once producer and consumer are fenced, the CPU miss service
		 * can stop without stranding a DMA reference.
		 */
		grx500_p2p_stop(board);
		grx500_rx_stop(lan);
	}
	if (!lan->running && !wan->running)
		grx500_rx_stop(wan);
	/* Drain any LAN delivery which observed running before it was cleared,
	 * including delivery from WAN's still-active NAPI instance.
	 */
	synchronize_net();
	netif_carrier_off(netdev);
	spin_lock_irqsave(&eth->lock, flags);
	grx500_reap_tx(eth);
	eth->stats.tx_dropped += eth->tx_pending;
	for (i = 0; i < TX_COUNT; i++) {
		struct grx500_tx_buffer *buffer = &eth->tx_buffer[i];

		if (!buffer->skb)
			continue;
		dma_unmap_single(eth->device, buffer->dma,
				 buffer->mapped_length, DMA_TO_DEVICE);
		dev_kfree_skb_any(buffer->skb);
		buffer->skb = NULL;
	}
	eth->tx_pending = 0;
	netdev_reset_queue(netdev);
	spin_unlock_irqrestore(&eth->lock, flags);
	netdev_info(netdev, "DMA%u stopped\n", eth->wan ? 1 : 2);
}

static int grx500_stop(struct net_device *netdev)
{
	struct grx500_eth *eth = netdev_priv(netdev);

	if (READ_ONCE(eth->running))
		grx500_halt(netdev);
	cancel_work_sync(&eth->reset_work);
	return 0;
}

static void grx500_reset_work(struct work_struct *work)
{
	struct grx500_eth *eth = container_of(work, struct grx500_eth, reset_work);
	int ret;

	/* ndo_stop holds RTNL while draining this worker. Never block behind it;
	 * the ordinary TX watchdog retries if an unrelated RTNL user is busy.
	 */
	if (!rtnl_trylock())
		return;
	if (netif_running(eth->netdev) && eth->running) {
		netdev_warn(eth->netdev, "TX timeout; restarting the DMA rings\n");
		grx500_halt(eth->netdev);
		ret = grx500_open(eth->netdev);
		if (ret)
			netdev_err(eth->netdev, "DMA restart failed: %d\n", ret);
	}
	rtnl_unlock();
}

static void grx500_tx_timeout(struct net_device *netdev, unsigned int queue)
{
	struct grx500_eth *eth = netdev_priv(netdev);

	schedule_work(&eth->reset_work);
}

static netdev_tx_t grx500_transmit(struct sk_buff *skb, struct net_device *netdev)
{
	struct grx500_eth *eth = netdev_priv(netdev);
	struct grx500_desc *desc;
	struct grx500_tx_buffer *buffer;
	dma_addr_t address;
	unsigned long flags;
	unsigned int index, length, offset, mapped_length;

	spin_lock_irqsave(&eth->lock, flags);
	grx500_reap_tx(eth);
	if (!eth->running || eth->tx_pending == TX_COUNT) {
		netif_stop_queue(netdev);
		spin_unlock_irqrestore(&eth->lock, flags);
		return NETDEV_TX_BUSY;
	}
	/* No scatter/gather is advertised. Defensively handle an unexpected
	 * fragmented skb and pad short frames without exposing tail bytes.
	 * A BUSY return above leaves the stack's skb completely untouched.
	 */
	if (unlikely(skb->len > MAX_FRAME || skb_linearize(skb) ||
		     __skb_put_padto(skb, ETH_ZLEN, false)))
		goto drop;
	offset = (unsigned long)skb->data & 7;
	if (unlikely(skb_headroom(skb) < offset)) {
		if (skb_cow_head(skb, 7))
			goto drop;
		offset = (unsigned long)skb->data & 7;
	}
	length = skb->len;
	mapped_length = length + offset;
	/* DMA1/2 use an eight-byte base with a three-bit offset in DW3.
	 * Include the prefix in the mapping; unmap this exact base and length.
	 */
	address = dma_map_single(eth->device, skb->data - offset,
				 mapped_length, DMA_TO_DEVICE);
	if (dma_mapping_error(eth->device, address))
		goto drop;
	index = eth->tx_head;
	desc = &eth->memory->tx[index];
	buffer = &eth->tx_buffer[index];
	buffer->skb = skb;
	buffer->dma = address;
	buffer->mapped_length = mapped_length;
	desc->dw0 = 0;
	desc->dw1 = 0;
	desc->address = cpu_to_be32(address);
	netdev_sent_queue(netdev, length);
	dma_wmb();
	WRITE_ONCE(desc->status, cpu_to_be32(DESC_OWN | DESC_SOP | DESC_EOP |
					   (offset << 23) | length));
	dma_wmb();
	eth->tx_head = (index + 1) % TX_COUNT;
	eth->tx_pending++;
	if (eth->tx_pending == TX_COUNT)
		netif_stop_queue(netdev);
	/* As in the BSP, kick a stopped channel and leave a running fetch engine
	 * alone. Channel configuration belongs to prepare, not every packet.
	 */
	grx500_dma_start(eth->tx_dma, 0);
	netif_trans_update(netdev);
	spin_unlock_irqrestore(&eth->lock, flags);
	return NETDEV_TX_OK;

drop:
	eth->stats.tx_dropped++;
	spin_unlock_irqrestore(&eth->lock, flags);
	dev_kfree_skb_any(skb);
	return NETDEV_TX_OK;
}

static void grx500_get_stats(struct net_device *netdev, struct rtnl_link_stats64 *stats)
{
	struct grx500_eth *eth = netdev_priv(netdev);
	unsigned long flags;

	spin_lock_irqsave(&eth->lock, flags);
	*stats = eth->stats;
	spin_unlock_irqrestore(&eth->lock, flags);
}

static const struct net_device_ops grx500_netdev_ops = {
	.ndo_open = grx500_open,
	.ndo_stop = grx500_stop,
	.ndo_start_xmit = grx500_transmit,
	.ndo_tx_timeout = grx500_tx_timeout,
	.ndo_get_stats64 = grx500_get_stats,
	.ndo_set_mac_address = eth_mac_addr,
	.ndo_validate_addr = eth_validate_addr,
};

static void grx500_free_dma(struct grx500_eth *eth)
{
	if (eth->memory)
		grx500_quiesce(eth);
	if (eth->rx_data_mapped)
		dma_unmap_single(eth->device, eth->rx_data_dma,
				 RX_COUNT * BUF_SIZE, DMA_FROM_DEVICE);
	kfree(eth->rx_data);
	if (eth->memory)
		dma_free_coherent(eth->device, sizeof(*eth->memory), eth->memory, eth->memory_dma);
}

static int grx500_register_port(struct platform_device *pdev,
			      struct grx500_eth_board *board, unsigned int port)
{
	const struct cpumask *affinity;
	struct device_node *node;
	const char *label;
	u32 index;
	struct device *device = &pdev->dev;
	struct net_device *netdev;
	struct grx500_eth *eth;
	int ret;

	netdev = alloc_etherdev(sizeof(*eth));
	if (!netdev)
		return -ENOMEM;
	SET_NETDEV_DEV(netdev, device);
	for_each_available_child_of_node(device->of_node, node) {
		if (of_node_name_eq(node, "ethernet") &&
		    !of_property_read_u32(node, "reg", &index) && index == port)
			break;
	}
	if (!node) {
		ret = -ENODEV;
		goto free_netdev;
	}
	netdev->dev.of_node = node;
	if (!of_property_read_string(node, "label", &label))
		strscpy(netdev->name, label, IFNAMSIZ);
	eth = netdev_priv(netdev);
	eth->device = device;
	eth->board = board;
	eth->netdev = netdev;
	eth->wan = port == 1;
	spin_lock_init(&eth->lock);
	mutex_init(&eth->bm_lock);
	INIT_DELAYED_WORK(&eth->recheck_work, grx500_recheck_work);
	INIT_DELAYED_WORK(&eth->writeback_work, grx500_writeback_work);
	INIT_WORK(&eth->reset_work, grx500_reset_work);
	eth->gsw = board->gsw[port];
	eth->rx_dma = &board->rx_dma[port];
	eth->tx_dma = &board->tx_dma[port];
	eth->memory = dma_alloc_coherent(device, sizeof(*eth->memory), &eth->memory_dma, GFP_KERNEL);
	if (!eth->memory) {
		ret = -ENOMEM;
		goto free_netdev;
	}
	BUILD_BUG_ON(BUF_SIZE % ARCH_DMA_MINALIGN);
	eth->rx_data = kmalloc_array(RX_COUNT, BUF_SIZE, GFP_KERNEL);
	if (!eth->rx_data) {
		ret = -ENOMEM;
		goto free_dma;
	}
	eth->rx_data_dma = dma_map_single(device, eth->rx_data,
					 RX_COUNT * BUF_SIZE, DMA_FROM_DEVICE);
	if (dma_mapping_error(device, eth->rx_data_dma)) {
		ret = -EIO;
		goto free_dma;
	}
	eth->rx_data_mapped = true;
	eth->rx_irq = platform_get_irq_byname(pdev, port ? "wan-rx" : "rx");
	eth->tx_irq = platform_get_irq_byname(pdev, port ? "wan-tx" : "tx");
	if (eth->rx_irq < 0 || eth->tx_irq < 0) {
		ret = eth->rx_irq < 0 ? eth->rx_irq : eth->tx_irq;
		goto free_dma;
	}
	grx500_mask_irqs(eth);
	netif_napi_add(netdev, &eth->napi, grx500_poll);
	ret = request_irq(eth->rx_irq, grx500_interrupt, 0, port ? "grx500-dma1-rx" : "grx500-dma2-rx", eth);
	if (ret)
		goto del_napi;
	ret = request_irq(eth->tx_irq, grx500_interrupt, 0, port ? "grx500-dma1-tx" : "grx500-dma2-tx", eth);
	if (ret)
		goto free_rx_irq;
	/* Each DMA pair shares one lock and NAPI instance. Keep its RX/TX IRQs
	 * together, while distributing the two Ethernet paths across CPUs.
	 */
	affinity = cpumask_of(cpumask_local_spread(port, dev_to_node(device)));
	irq_set_affinity_and_hint(eth->rx_irq, affinity);
	irq_set_affinity_and_hint(eth->tx_irq, affinity);
	netif_napi_set_irq(&eth->napi, eth->rx_irq);

	netdev->netdev_ops = &grx500_netdev_ops;
	netdev->watchdog_timeo = 5 * HZ;
	netdev->min_mtu = ETH_MIN_MTU;
	netdev->max_mtu = ETH_DATA_LEN + (port ? 0 : 8);
	if (of_get_ethdev_address(node, netdev))
		eth_hw_addr_random(netdev);
	netif_carrier_off(netdev);
	ret = grx500_mdio_register(eth);
	if (ret)
		goto disconnect_phy;
	ret = register_netdev(netdev);
	if (ret)
		goto disconnect_phy;
	board->netdev[port] = netdev;
	netdev_info(netdev, "DMA%u IRQs RX=%d TX=%d\n",
		    port ? 1 : 2, eth->rx_irq, eth->tx_irq);
	return 0;

disconnect_phy:
	if (eth->phydev)
		phy_disconnect(eth->phydev);
	if (eth->mdio) {
		mdiobus_unregister(eth->mdio);
		mdiobus_free(eth->mdio);
	}
	irq_update_affinity_hint(eth->tx_irq, NULL);
	free_irq(eth->tx_irq, eth);
free_rx_irq:
	irq_update_affinity_hint(eth->rx_irq, NULL);
	free_irq(eth->rx_irq, eth);
del_napi:
	netif_napi_del(&eth->napi);
free_dma:
	grx500_free_dma(eth);
free_netdev:
	of_node_put(netdev->dev.of_node);
	free_netdev(netdev);
	return ret;
}

static void grx500_unregister_port(struct net_device *netdev)
{
	struct grx500_eth *eth = netdev_priv(netdev);

	unregister_netdev(netdev);
	if (eth->phydev)
		phy_disconnect(eth->phydev);
	if (eth->mdio) {
		mdiobus_unregister(eth->mdio);
		mdiobus_free(eth->mdio);
	}
	irq_update_affinity_hint(eth->rx_irq, NULL);
	irq_update_affinity_hint(eth->tx_irq, NULL);
	free_irq(eth->rx_irq, eth);
	free_irq(eth->tx_irq, eth);
	netif_napi_del(&eth->napi);
	grx500_free_dma(eth);
	of_node_put(netdev->dev.of_node);
	free_netdev(netdev);
}

static void grx500_board_release(struct grx500_eth_board *board)
{
	int port;

	WRITE_ONCE(board->ready, false);
	of_platform_depopulate(board->device);
	/* WAN's receive ring may serve an administratively-up LAN conduit.
	 * Close both netdevs before freeing either netdev/ring/NAPI instance.
	 */
	rtnl_lock();
	for (port = 0; port < 2; port++)
		if (board->netdev[port] && netif_running(board->netdev[port]))
			dev_close(board->netdev[port]);
	rtnl_unlock();
	if (board->p2p) {
		grx500_p2p_stop(board);
		dma_free_coherent(board->device, sizeof(*board->p2p),
				  board->p2p, board->p2p_dma);
	}
	for (port = 1; port >= 0; port--)
		if (board->netdev[port]) {
			grx500_unregister_port(board->netdev[port]);
			board->netdev[port] = NULL;
		}
	/* Both DMA pairs and phylib are now stopped. All five PHYs must stop
	 * fetching instructions before their single shared allocation is freed.
	 */
	if (reset_control_assert(board->phy_resets))
		panic("GRX500: cannot fence GPHY firmware accesses");
	grx500_writel(board->gsw[0], GSW_TOP, 0);
	grx500_writel(board->gsw[1], GSW_TOP, 0);
	if (board->firmware) {
		msleep(100);
		dma_free_coherent(board->device, FW_ALLOC, board->firmware,
				  board->firmware_dma);
	}
}

static int grx500_eth_probe(struct platform_device *pdev)
{
	struct device *device = &pdev->dev;
	struct grx500_eth_board *board;
	unsigned int port;
	int ret;

	ret = dma_set_mask_and_coherent(device, DMA_BIT_MASK(32));
	if (ret)
		return ret;
	board = devm_kzalloc(device, sizeof(*board), GFP_KERNEL);
	if (!board)
		return -ENOMEM;
	board->device = device;
	board->phy_resets = devm_reset_control_array_get_exclusive(device);
	if (IS_ERR(board->phy_resets))
		return dev_err_probe(device, PTR_ERR(board->phy_resets), "GPHY resets unavailable\n");
	for (port = 0; port < 2; port++) {
		struct grx500_dma *rx = &board->rx_dma[port];
		struct grx500_dma *tx = &board->tx_dma[port];

		board->gsw[port] = devm_platform_ioremap_resource_byname(pdev,
						port ? "wan-switch" : "switch");
		rx->base = devm_platform_ioremap_resource_byname(pdev,
								 port ? "wan-rx-dma" : "rx-dma");
		tx->base = devm_platform_ioremap_resource_byname(pdev,
								 port ? "wan-tx-dma" : "tx-dma");
		if (IS_ERR(board->gsw[port]) || IS_ERR(rx->base) || IS_ERR(tx->base))
			return -ENODEV;
		spin_lock_init(&rx->lock);
		spin_lock_init(&tx->lock);
		tx->transmit = true;
	}
	ret = reset_control_assert(board->phy_resets);
	if (ret)
		return ret;
	for (port = 0; port < 2; port++) {
		ret = grx500_dma_init(&board->rx_dma[port], false);
		if (!ret)
			ret = grx500_dma_init(&board->tx_dma[port], port == 1);
		if (ret)
			goto release_board;
		ret = grx500_switch_init(board, port);
		if (ret)
			goto release_board;
	}
	ret = grx500_load_phy(board);
	if (ret)
		goto release_board;
	board->p2p = dma_alloc_coherent(device, sizeof(*board->p2p),
					&board->p2p_dma, GFP_KERNEL);
	if (!board->p2p) {
		ret = -ENOMEM;
		goto release_board;
	}
	for (port = 0; port < 2; port++) {
		ret = grx500_register_port(pdev, board, port);
		if (ret)
			goto release_board;
	}
	platform_set_drvdata(pdev, board);
	ret = grx500_backpressure_init(board);
	if (ret)
		goto release_board;
	board->switch_data.base = board->gsw[0];
	board->switch_data.cpu_rx_channel = P2P_CHANNEL;
	WRITE_ONCE(board->ready, true);
	{
		const struct of_dev_auxdata aux[] = {
			OF_DEV_AUXDATA("lantiq,grx500-switch", 0, NULL, &board->switch_data),
			{ }
		};

		/* DSA's conduit lookup must see the fully registered unique OF netdev. */
		ret = of_platform_populate(device->of_node, NULL, aux, device);
		if (ret)
			goto release_board;
	}
	return 0;

release_board:
	grx500_board_release(board);
	return dev_err_probe(device, ret, "Ethernet initialization failed\n");
}

static void grx500_eth_remove(struct platform_device *pdev)
{
	grx500_board_release(platform_get_drvdata(pdev));
}

static void grx500_eth_shutdown(struct platform_device *pdev)
{
	struct grx500_eth_board *board = platform_get_drvdata(pdev);
	unsigned int port;

	/* Reboot does not run remove(). Close both users of WAN RX before
	 * fencing the shared P2P path, but retain all backing allocations.
	 */
	WRITE_ONCE(board->ready, false);
	rtnl_lock();
	for (port = 0; port < 2; port++)
		if (netif_running(board->netdev[port]))
			dev_close(board->netdev[port]);
	grx500_p2p_stop(board);
	for (port = 0; port < 2; port++) {
		struct grx500_eth *eth = netdev_priv(board->netdev[port]);

		cancel_work_sync(&eth->reset_work);
		grx500_rx_stop(eth);
		grx500_quiesce(eth);
	}
	rtnl_unlock();
	if (reset_control_assert(board->phy_resets))
		panic("GRX500: cannot fence GPHY firmware accesses at shutdown");
	grx500_writel(board->gsw[0], GSW_TOP, 0);
	grx500_writel(board->gsw[1], GSW_TOP, 0);
	dev_info(&pdev->dev, "Ethernet DMA and GPHYs fenced for shutdown\n");
}

static const struct of_device_id grx500_eth_match[] = {
	{ .compatible = "lantiq,grx500-ethernet" },
	{ }
};
MODULE_DEVICE_TABLE(of, grx500_eth_match);

static struct platform_driver grx500_eth_driver = {
	.probe = grx500_eth_probe,
	.remove = grx500_eth_remove,
	.shutdown = grx500_eth_shutdown,
	.driver = {
		.name = "grx500-ethernet",
		.of_match_table = grx500_eth_match,
	},
};
module_platform_driver(grx500_eth_driver);
MODULE_FIRMWARE("lantiq/grx500-phy11g-a21.bin");
MODULE_LICENSE("GPL");
