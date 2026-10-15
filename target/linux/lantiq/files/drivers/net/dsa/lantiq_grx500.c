// SPDX-License-Identifier: GPL-2.0-only
/* GSWIP3 LAN switch with isolated bridge domains and CPU-only flooding.
 * Register protocol follows the Lantiq GSWIP3 switch API in the TP-Link GPL
 * release. The Ethernet parent owns reset, parser, MDIO and GPHY firmware.
 */
#include <linux/bitops.h>
#include <linux/ethtool.h>
#include <linux/etherdevice.h>
#include <linux/if_bridge.h>
#include <linux/if_vlan.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/jiffies.h>
#include <linux/math64.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/platform_device.h>
#include <linux/platform_data/grx500.h>
#include <linux/slab.h>
#include <net/dsa.h>
#include <net/switchdev.h>

#define GRX500_NUM_PORTS	6
#define GRX500_LAN_PORTS	GENMASK(5, 2)
#define GRX500_CPU_PORT	0
#define GRX500_CPU_VID	4095
#define GRX500_UNAWARE_VID 4094
#define GRX500_NUM_FIDS	64
#define GRX500_NUM_RULES	41
#define BM_RAM_VAL(i)	(0x043 - (i))
#define BM_RAM_ADDR	0x044
#define BM_RAM_CTRL	0x045
#define BM_PCFG(p)	(0x080 + 2 * (p))
#define BM_PCFG_CNTEN	BIT(0)
#define PCE_CTRL	0x44f
#define PCE_ADDR	0x44e
#define PCE_KEY(i)	(0x447 - (i))
#define PCE_MASK(i)	((i) ? 0x42d - (i) : 0x448)
#define PCE_VAL(i)	((i) < 5 ? 0x44d - (i) : 0x43c - (i))
#define PCE_AGE_EXP	0x451
#define PCE_AGE_EXP_MASK GENMASK(3, 0)
#define PCE_AGE_MANT	0x452
#define PCE_AGE_CLK_MS	25000
#define PCE_AGE_STEPS	16
#define PCE_GCTRL0	0x456
#define PCE_GCTRL1	0x457
#define PCE_VLAN_MODE	BIT(9)
#define PCE_PCTRL0(p)	(0x480 + 10 * (p))
#define PCE_PCTRL1(p)	(0x481 + 10 * (p))
#define PCE_PCTRL3(p)	(0x483 + 10 * (p))
#define PCE_VCTRL(p)	(0x485 + 10 * (p))
#define PCE_PVID(p)	(0x486 + 10 * (p))
#define PCE_TVM		BIT(5)
#define PCE_LEARNING_OFF BIT(15)
#define PCE_VLAN_REASSIGN BIT(5)
#define PCE_VLAN_EGRESS	BIT(4)
#define PCE_VLAN_INGRESS BIT(3)
#define PCE_TAGGED_ONLY	BIT(1)
#define PMAC_CTRL	0xd46
#define PMAC_ADDR	0xd45
#define PMAC_VAL(i)	(0xd44 - (i))
#define FDMA_PCTRL(p)	(0xa80 + 6 * (p))
#define FDMA_VLANMOD_MASK GENMASK(4, 3)
#define FDMA_VLANMOD_BOTH (3 << 3)
#define SDMA_PCTRL(p)	(0xbc0 + 6 * (p))
#define SDMA_PCTRL_FCEN	BIT(1)
#define MAC_CTRL0(p)	(0x903 + 12 * ((p) - 1))
#define MAC_CTRL0_FCON	GENMASK(6, 4)
#define MAC_CTRL0_FCON_RX	BIT(4)
#define MAC_CTRL0_FCON_TX	BIT(5)
#define MAC_CTRL0_FCON_OFF BIT(6)
#define PHY_ADDR(p)	(0xf44 + 4 * ((p) - 1))
#define PHY_ADDR_FCON_TX	GENMASK(8, 7)
#define PHY_ADDR_FCON_RX	GENMASK(6, 5)
#define PHY_ADDR_FCON_TX_EN BIT(7)
#define PHY_ADDR_FCON_RX_EN BIT(5)
#define TABLE_BUSY	BIT(15)
#define TABLE_VALID	BIT(12)
#define TABLE_WRITE	BIT(5)
#define TABLE_KEY_READ	BIT(6)
#define TABLE_KEY_WRITE	(BIT(6) | BIT(5))
#define VLAN_MATCH_TABLE 0x01
#define VLAN_MAP_TABLE	0x02
#define MAC_TABLE	0x0b
#define FLOW_TABLE	0x0f
#define MAC_DA_TABLE	0x16

struct grx500_pce_entry {
	u16 key[16];
	u16 mask[4];
	u16 val[16];
	u16 index;
	bool valid;
};

struct grx500_domain {
	u16 vid;
	u8 members;
	u8 untagged;
};

struct grx500_port {
	u16 pvid;
	u8 stp;
	bool learning;
	bool link;
};

struct grx500_fdb_entry {
	struct list_head list;
	unsigned char addr[ETH_ALEN];
	u16 vid;
	u8 owners;
};

struct grx500_rmon_counter {
	const char *name;
	u8 offset;
	bool bytes;
};

/* GSWIP3 port RMON indexes. The mode-dependent frame totals at 0x0c and
 * 0x1f are omitted: changing broadcast mode would change existing counts,
 * and deriving broadcasts from separately sampled totals can underflow.
 */
static const struct grx500_rmon_counter grx500_rmon_counters[] = {
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

struct grx500_rmon_stats {
	u64 counters[ARRAY_SIZE(grx500_rmon_counters)];
	u64 read_failures;
};

struct grx500_switch {
	struct dsa_switch ds;
	void *flow_owner;
	void (*invalidate_flows)(void *owner);
	void (*software_flow_ingress)(void *owner, bool software);
	void __iomem *base;
	/* All ports share the BM table command and result registers. */
	struct mutex bm_lock;
	struct grx500_rmon_stats rmon[GRX500_NUM_PORTS];
	/* PCE commands, forwarding state and phylink port enables share a lock. */
	struct mutex pce_lock;
	struct grx500_port ports[GRX500_NUM_PORTS];
	struct grx500_domain domains[GRX500_NUM_FIDS];
	u16 programmed_vid[GRX500_NUM_FIDS];
	struct list_head fdb;
	unsigned int fdb_count;
	unsigned int mac_entries;
	unsigned int ageing_time;
	struct net_device *bridge;
	struct notifier_block switchdev_nb;
	u8 bridge_ports;
	u8 cpu_rx_channel;
	bool vlan_filtering;
	bool software;
	bool failed;
};

static u32 grx500_read(struct grx500_switch *sw, u32 word)
{
	return __raw_readl(sw->base + word * 4);
}

static void grx500_write(struct grx500_switch *sw, u32 word, u32 value)
{
	__raw_writel(value, sw->base + word * 4);
	wmb();
}

static void grx500_update(struct grx500_switch *sw, u32 word, u32 mask, u32 value)
{
	grx500_write(sw, word, (grx500_read(sw, word) & ~mask) | value);
}

static int grx500_table_wait(struct grx500_switch *sw, u32 word)
{
	u32 value;

	return read_poll_timeout(__raw_readl, value, !(value & TABLE_BUSY),
				 1, 10000, false, sw->base + word * 4);
}

static int grx500_rmon_read32(struct grx500_switch *sw, int port,
			      u8 offset, u32 *value)
{
	int ret;

	lockdep_assert_held(&sw->bm_lock);
	ret = grx500_table_wait(sw, BM_RAM_CTRL);
	if (ret)
		return ret;
	grx500_write(sw, BM_RAM_ADDR, offset);
	/* LAN ports 0..5 directly select their GSWIP3 BM table. OPMOD=0 reads. */
	grx500_write(sw, BM_RAM_CTRL, TABLE_BUSY | port);
	ret = grx500_table_wait(sw, BM_RAM_CTRL);
	if (ret)
		return ret;
	*value = (grx500_read(sw, BM_RAM_VAL(0)) & 0xffff) |
		 ((grx500_read(sw, BM_RAM_VAL(1)) & 0xffff) << 16);
	return 0;
}

static int grx500_rmon_read64(struct grx500_switch *sw, int port,
			      u8 offset, u64 *value)
{
	u32 high, low, high_after;
	int attempt, ret;

	/* The free-running byte counter spans two independently read entries. */
	for (attempt = 0; attempt < 3; attempt++) {
		ret = grx500_rmon_read32(sw, port, offset + 1, &high);
		if (ret)
			return ret;
		ret = grx500_rmon_read32(sw, port, offset, &low);
		if (ret)
			return ret;
		ret = grx500_rmon_read32(sw, port, offset + 1, &high_after);
		if (ret)
			return ret;
		if (high == high_after) {
			*value = ((u64)high << 32) | low;
			return 0;
		}
	}
	return -EAGAIN;
}

static void grx500_get_strings(struct dsa_switch *ds, int port,
			       u32 stringset, u8 *data)
{
	unsigned int i;

	if (stringset != ETH_SS_STATS)
		return;
	for (i = 0; i < ARRAY_SIZE(grx500_rmon_counters); i++)
		ethtool_puts(&data, grx500_rmon_counters[i].name);
	ethtool_puts(&data, "rmon_read_failures");
	ethtool_puts(&data, "rmon_last_read_ok");
}

static int grx500_get_sset_count(struct dsa_switch *ds, int port, int sset)
{
	return sset == ETH_SS_STATS ? ARRAY_SIZE(grx500_rmon_counters) + 2 : 0;
}

static void grx500_get_ethtool_stats(struct dsa_switch *ds, int port, u64 *data)
{
	struct grx500_switch *sw = ds->priv;
	struct grx500_rmon_stats *stats = &sw->rmon[port];
	u64 sample[ARRAY_SIZE(grx500_rmon_counters)];
	unsigned int i;
	int ret = 0;

	mutex_lock(&sw->bm_lock);
	for (i = 0; i < ARRAY_SIZE(grx500_rmon_counters); i++) {
		const struct grx500_rmon_counter *counter = &grx500_rmon_counters[i];
		u32 value;

		if (counter->bytes) {
			ret = grx500_rmon_read64(sw, port, counter->offset, &sample[i]);
		} else {
			ret = grx500_rmon_read32(sw, port, counter->offset, &value);
			if (!ret)
				sample[i] = value;
		}
		if (ret)
			break;
	}
	/* ethtool has no error return here. Keep the last complete read and
	 * expose freshness explicitly instead of inventing zero counter values.
	 */
	if (ret) {
		stats->read_failures++;
		dev_warn_ratelimited(ds->dev, "port %d: RMON read failed (%d)\n", port, ret);
	} else {
		memcpy(stats->counters, sample, sizeof(sample));
	}
	memcpy(data, stats->counters, sizeof(stats->counters));
	data[ARRAY_SIZE(grx500_rmon_counters)] = stats->read_failures;
	data[ARRAY_SIZE(grx500_rmon_counters) + 1] = !ret;
	mutex_unlock(&sw->bm_lock);
}

static int grx500_pce_xfer(struct grx500_switch *sw, u16 table, u16 op,
			   struct grx500_pce_entry *entry)
{
	u32 ctrl;
	int i, ret;

	lockdep_assert_held(&sw->pce_lock);
	ret = grx500_table_wait(sw, PCE_CTRL);
	if (ret)
		return ret;
	if (op) {
		for (i = 0; i < ARRAY_SIZE(entry->key); i++)
			grx500_write(sw, PCE_KEY(i), entry->key[i]);
	}
	if (op & TABLE_WRITE) {
		for (i = 0; i < ARRAY_SIZE(entry->val); i++)
			grx500_write(sw, PCE_VAL(i), entry->val[i]);
		for (i = 0; i < ARRAY_SIZE(entry->mask); i++)
			grx500_write(sw, PCE_MASK(i), entry->mask[i]);
	}
	grx500_write(sw, PCE_ADDR, entry->index);
	ctrl = TABLE_BUSY | table | op;
	if ((op & TABLE_WRITE) && entry->valid)
		ctrl |= TABLE_VALID;
	grx500_write(sw, PCE_CTRL, ctrl);
	ret = grx500_table_wait(sw, PCE_CTRL);
	if (ret || (op & TABLE_WRITE))
		return ret;
	entry->valid = !!(grx500_read(sw, PCE_CTRL) & TABLE_VALID);
	entry->index = grx500_read(sw, PCE_ADDR) & 0x1fff;
	for (i = 0; i < ARRAY_SIZE(entry->key); i++)
		entry->key[i] = grx500_read(sw, PCE_KEY(i)) & 0xffff;
	for (i = 0; i < ARRAY_SIZE(entry->val); i++)
		entry->val[i] = grx500_read(sw, PCE_VAL(i)) & 0xffff;
	return 0;
}

static void grx500_rule_init(struct grx500_pce_entry *entry, int port)
{
	static const u16 wildcard[] = {
		0x7f00, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff,
		0x7f0f, 0x1f1f, 0x7f1f, 0x7fff, 0xffff, 0xffff,
		0xffff, 0xffff, 0x001f, 0,
	};

	/* Disabled fields use parser wildcard indexes, not zero. Enhanced VLAN
	 * mode also requires the S-VLAN wildcard in key[8]'s high byte.
	 */
	memset(entry, 0, sizeof(*entry));
	memcpy(entry->key, wildcard, sizeof(wildcard));
	entry->key[0] |= port;
	entry->val[1] = 0xffff;
	entry->val[3] = 0x7f1f;
	entry->val[6] = 0x0006;
	entry->valid = true;
}

static void grx500_rule_trap(struct grx500_pce_entry *entry)
{
	/* Alternative CPU-only map, cross VLAN/state, and never learn from
	 * software-owned traffic. These actions also admit STP control frames.
	 */
	entry->val[0] = BIT(0) | BIT(4) | BIT(5) | BIT(9);
	entry->val[1] = BIT(GRX500_CPU_PORT);
	entry->val[3] |= BIT(15);
	entry->val[4] = BIT(13) | GENMASK(3, 2) | BIT(1);
}

static int grx500_redirect(struct grx500_switch *sw, int port, bool enable)
{
	struct grx500_pce_entry entry;

	grx500_rule_init(&entry, port);
	grx500_rule_trap(&entry);
	entry.index = port - 2;
	entry.valid = enable;
	return grx500_pce_xfer(sw, FLOW_TABLE, TABLE_WRITE, &entry);
}

static int grx500_traps_setup(struct grx500_switch *sw)
{
	struct grx500_pce_entry entry;
	int port, nibble, index = 9, ret;

	/* CPU TX already contains Linux's completed egress frame. Leave PMAC's
	 * explicit single-port map intact, including BPDU TX on blocked ports.
	 */
	grx500_rule_init(&entry, GRX500_CPU_PORT);
	entry.index = 4;
	entry.val[0] = BIT(4) | BIT(5) | BIT(9);
	entry.val[3] |= BIT(15);
	entry.val[4] = BIT(13) | BIT(1);
	ret = grx500_pce_xfer(sw, FLOW_TABLE, TABLE_WRITE, &entry);
	if (ret)
		return ret;

	/* GSWIP3 DA masks ignore whole nibbles. Eight odd low-nibble values
	 * of the first octet cover every group address, including broadcast.
	 */
	for (nibble = 0; nibble < 8; nibble++) {
		memset(&entry, 0, sizeof(entry));
		entry.index = nibble;
		entry.key[2] = (2 * nibble + 1) << 8;
		entry.mask[0] = 0xfff & ~BIT(10);
		entry.valid = true;
		ret = grx500_pce_xfer(sw, MAC_DA_TABLE, TABLE_WRITE, &entry);
		if (ret)
			return ret;
		for (port = 2; port <= 5; port++) {
			grx500_rule_init(&entry, port);
			grx500_rule_trap(&entry);
			entry.index = index++;
			entry.key[5] = (nibble << 8) | 0xff;
			ret = grx500_pce_xfer(sw, FLOW_TABLE, TABLE_WRITE, &entry);
			if (ret)
				return ret;
		}
	}
	memset(&entry, 0, sizeof(entry));
	entry.key[0] = GRX500_CPU_VID;
	entry.valid = true;
	return grx500_pce_xfer(sw, VLAN_MATCH_TABLE, TABLE_WRITE, &entry);
}

static int grx500_private_vid_drop(struct grx500_switch *sw, int port, bool enable)
{
	struct grx500_pce_entry entry;

	/* These precede group traps. A physical VLAN-aware sender cannot enter
	 * CPU0's internal VID4095, whose untagged egress is reserved for TX.
	 * TVM ports treat existing VLAN bytes as payload and need no such rule.
	 */
	grx500_rule_init(&entry, port);
	grx500_rule_trap(&entry);
	entry.index = 5 + port - 2;
	entry.key[0] = port;
	entry.val[1] = 0;
	entry.valid = enable;
	return grx500_pce_xfer(sw, FLOW_TABLE, TABLE_WRITE, &entry);
}

static int grx500_vlan_write(struct grx500_switch *sw, u16 vid, u8 fid,
			     u8 members, u8 untagged)
{
	struct grx500_pce_entry entry = { .index = vid };

	/* GSWIP3 has a direct 4096-entry VID map, unlike GSWIP2's active-VLAN
	 * indirection. FID is six bits; VAL1/2 are member/tagged port maps.
	 */
	entry.val[0] = fid;
	entry.val[1] = members;
	entry.val[2] = members & ~untagged;
	return grx500_pce_xfer(sw, VLAN_MAP_TABLE, TABLE_WRITE, &entry);
}

static int grx500_pmac_setup(struct grx500_switch *sw)
{
	unsigned int flow, class, i;
	int ret;

	/* The parent owns the destination DMA channel and its receive service.
	 * Keep the same PMAC identity for every CPU-bound classification. CPU TX
	 * selects a physical jack explicitly and cannot recirculate to CPU0.
	 */
	for (flow = 0; flow < 4; flow++) {
		for (class = 0; class < 16; class++) {
			ret = grx500_table_wait(sw, PMAC_CTRL);
			if (ret)
				return ret;
			grx500_write(sw, PMAC_ADDR, (flow << 8) | (class << 4));
			for (i = 0; i < 5; i++)
				grx500_write(sw, PMAC_VAL(i), i == 2 ? 1 : 0);
			grx500_write(sw, PMAC_VAL(1), sw->cpu_rx_channel);
			grx500_write(sw, PMAC_CTRL, TABLE_BUSY | TABLE_WRITE | 2);
		}
	}
	ret = grx500_table_wait(sw, PMAC_CTRL);
	if (ret)
		return ret;
	grx500_write(sw, PMAC_ADDR, 0);
	for (i = 0; i < 5; i++)
		grx500_write(sw, PMAC_VAL(i), i == 4 ? BIT(0) : 0);
	/* CPU TX supplies both port-map enable and the destination bitmap. */
	grx500_write(sw, PMAC_CTRL, TABLE_BUSY | TABLE_WRITE | 1);
	return grx500_table_wait(sw, PMAC_CTRL);
}

static void grx500_port_gate(struct grx500_switch *sw, int port, bool enable)
{
	grx500_update(sw, FDMA_PCTRL(port), BIT(0), enable ? BIT(0) : 0);
	grx500_update(sw, SDMA_PCTRL(port), BIT(0), enable ? BIT(0) : 0);
}

static void grx500_pause_lan(struct grx500_switch *sw)
{
	int port;

	for (port = 2; port <= 5; port++)
		grx500_port_gate(sw, port, false);
}

static void grx500_resume_lan(struct grx500_switch *sw)
{
	int port;

	for (port = 2; port <= 5; port++)
		grx500_port_gate(sw, port, sw->ports[port].link && !sw->failed);
}

static bool grx500_port_offloaded(struct grx500_switch *sw, int port)
{
	return !sw->software && (sw->bridge_ports & BIT(port));
}

static void grx500_port_config(struct grx500_switch *sw, int port)
{
	struct grx500_port *p = &sw->ports[port];
	bool offloaded = grx500_port_offloaded(sw, port);
	bool aware = offloaded && sw->vlan_filtering;
	bool learn = offloaded && p->learning &&
		     (p->stp == BR_STATE_LEARNING || p->stp == BR_STATE_FORWARDING);
	u16 pvid = offloaded ? GRX500_UNAWARE_VID : GRX500_CPU_VID;
	u32 state = 7, vctrl = 0;

	if (offloaded) {
		vctrl = PCE_VLAN_INGRESS | PCE_VLAN_EGRESS;
		if (aware) {
			pvid = p->pvid;
			if (!pvid)
				vctrl |= PCE_TAGGED_ONLY;
		} else {
			vctrl |= PCE_VLAN_REASSIGN;
		}
		if (p->stp == BR_STATE_LEARNING)
			state = 4;
		else if (p->stp != BR_STATE_FORWARDING)
			state = 0;
	}
	grx500_write(sw, PCE_PVID(port), pvid);
	grx500_write(sw, PCE_VCTRL(port), vctrl);
	grx500_update(sw, PCE_PCTRL0(port), PCE_TVM | GENMASK(2, 0),
		      state | (aware ? 0 : PCE_TVM));
	grx500_update(sw, PCE_PCTRL1(port), 0xff, learn ? 0xff : 0);
	grx500_update(sw, PCE_PCTRL3(port), PCE_LEARNING_OFF,
		      learn ? 0 : PCE_LEARNING_OFF);
}

static int grx500_mac_flush(struct grx500_switch *sw)
{
	u32 value;

	grx500_update(sw, PCE_GCTRL0, BIT(0), BIT(0));
	return read_poll_timeout(__raw_readl, value, !(value & BIT(0)),
				 1, 100000, false, sw->base + PCE_GCTRL0 * 4);
}

static int grx500_fid(struct grx500_switch *sw, u16 vid)
{
	int fid;

	if (!sw->vlan_filtering)
		return !vid && sw->domains[1].members ? 1 : 0;
	for (fid = 1; fid < GRX500_NUM_FIDS; fid++) {
		if (sw->domains[fid].vid == vid && sw->domains[fid].members)
			return fid;
	}
	return 0;
}

static void grx500_mac_key(struct grx500_pce_entry *entry,
			   const unsigned char *addr, u8 fid)
{
	memset(entry, 0, sizeof(*entry));
	entry->key[0] = (addr[4] << 8) | addr[5];
	entry->key[1] = (addr[2] << 8) | addr[3];
	entry->key[2] = (addr[0] << 8) | addr[1];
	entry->key[3] = fid;
}

static struct grx500_fdb_entry *
grx500_fdb_find(struct grx500_switch *sw, const unsigned char *addr, u16 vid)
{
	struct grx500_fdb_entry *fdb;

	list_for_each_entry(fdb, &sw->fdb, list) {
		if (fdb->vid == vid && ether_addr_equal(fdb->addr, addr))
			return fdb;
	}
	return NULL;
}

static int grx500_fdb_write_fid(struct grx500_switch *sw,
				const struct grx500_fdb_entry *fdb, u8 fid)
{
	struct grx500_pce_entry entry, check;
	struct grx500_fdb_entry *global;
	u8 destinations;
	int ret;

	destinations = fdb->owners & (sw->domains[fid].members | BIT(0));
	global = grx500_fdb_find(sw, fdb->addr, 0);
	/* A local VID0 address also protects VLAN-aware FIDs. Sending a MAC
	 * shared with a VLAN-specific owner to Linux preserves either policy.
	 */
	if (global && (global->owners & BIT(0)))
		destinations = BIT(0);
	/* Never replicate a unicast to CPU and physical ports. Conflicting or
	 * currently ineligible owners are resolved by Linux; a zero effective
	 * map must replace a previous static destination too.
	 */
	if ((destinations & BIT(0)) || hweight8(destinations) != 1)
		destinations = BIT(0);
	grx500_mac_key(&entry, fdb->addr, fid);
	entry.val[0] = destinations;
	entry.val[1] = BIT(1) | BIT(0);
	entry.valid = true;
	ret = grx500_pce_xfer(sw, MAC_TABLE, TABLE_KEY_WRITE, &entry);
	if (ret)
		return ret;
	/* A completed key-search write can still fail because the table is full. */
	grx500_mac_key(&check, fdb->addr, fid);
	ret = grx500_pce_xfer(sw, MAC_TABLE, TABLE_KEY_READ, &check);
	if (ret)
		return ret;
	if (!check.valid || check.val[0] != entry.val[0] ||
	    (check.val[1] & ~BIT(2)) != entry.val[1] ||
	    (check.key[3] & 0x3f) != fid ||
	    memcmp(check.key, entry.key, 3 * sizeof(u16)))
		return -ENOSPC;
	return 0;
}

static int grx500_fdb_write(struct grx500_switch *sw,
			    const struct grx500_fdb_entry *fdb)
{
	int fid, ret;

	if (sw->vlan_filtering && !fdb->vid && (fdb->owners & BIT(0))) {
		for (fid = 1; fid < GRX500_NUM_FIDS; fid++) {
			if (!sw->domains[fid].members)
				continue;
			ret = grx500_fdb_write_fid(sw, fdb, fid);
			if (ret)
				return ret;
		}
		return 0;
	}
	/* VLAN deletion can precede its asynchronous FDB notifications. */
	fid = grx500_fid(sw, fdb->vid);
	return fid ? grx500_fdb_write_fid(sw, fdb, fid) : 0;
}

static int grx500_fdb_erase(struct grx500_switch *sw,
			    const unsigned char *addr, u16 vid)
{
	struct grx500_pce_entry entry;
	struct grx500_fdb_entry *global;
	int fid = grx500_fid(sw, vid), ret;

	if (!fid)
		return 0;
	global = grx500_fdb_find(sw, addr, 0);
	if (global && (global->owners & BIT(0)))
		return grx500_fdb_write_fid(sw, global, fid);
	grx500_mac_key(&entry, addr, fid);
	ret = grx500_pce_xfer(sw, MAC_TABLE, TABLE_KEY_READ, &entry);
	if (ret || !entry.valid)
		return ret;
	entry.valid = false;
	return grx500_pce_xfer(sw, MAC_TABLE, TABLE_WRITE, &entry);
}

static int grx500_fdb_replay(struct grx500_switch *sw)
{
	struct grx500_fdb_entry *fdb;
	int ret;

	ret = grx500_mac_flush(sw);
	if (ret)
		return ret;
	list_for_each_entry(fdb, &sw->fdb, list) {
		ret = grx500_fdb_write(sw, fdb);
		if (ret)
			return ret;
	}
	return 0;
}

static void grx500_fdb_clear(struct grx500_switch *sw, bool notify)
{
	struct grx500_fdb_entry *fdb, *next;
	struct dsa_port *dp = NULL;
	int port;

	/* The bridge notifier finds the FDB by bridge + MAC/VID. Any remaining
	 * member identifies that bridge, including for CPU/foreign ownership.
	 */
	for (port = 2; port <= 5; port++) {
		if (sw->bridge_ports & BIT(port)) {
			dp = dsa_to_port(&sw->ds, port);
			break;
		}
	}
	list_for_each_entry_safe(fdb, next, &sw->fdb, list) {
		if (notify && dp) {
			struct switchdev_notifier_fdb_info info = {
				.addr = fdb->addr,
				.vid = fdb->vid,
				.offloaded = false,
			};

			rcu_read_lock();
			call_switchdev_notifiers(SWITCHDEV_FDB_OFFLOADED,
						 dp->user, &info.info, NULL);
			rcu_read_unlock();
		}
		list_del(&fdb->list);
		kfree(fdb);
	}
	sw->fdb_count = 0;
}

static int grx500_software(struct grx500_switch *sw, const char *reason)
{
	int port, ret;

	lockdep_assert_held(&sw->pce_lock);
	if (sw->software_flow_ingress)
		sw->software_flow_ingress(sw->flow_owner, true);
	if (sw->invalidate_flows)
		sw->invalidate_flows(sw->flow_owner);
	grx500_pause_lan(sw);
	/* Keep ports stopped if the command engine cannot restore isolation.
	 * A later phylink event must not reopen a partially configured switch.
	 */
	sw->software = true;
	for (port = 2; port <= 5; port++) {
		ret = grx500_redirect(sw, port, true);
		if (ret)
			goto failed;
		grx500_port_config(sw, port);
	}
	grx500_fdb_clear(sw, true);
	grx500_resume_lan(sw);
	dev_warn(sw->ds.dev, "bridge forwarding in software: %s\n", reason);
	return 0;

failed:
	sw->failed = true;
	grx500_fdb_clear(sw, true);
	dev_err(sw->ds.dev, "could not restore CPU isolation (%d); LAN ports stopped\n", ret);
	return ret;
}

static int grx500_reconfigure(struct grx500_switch *sw)
{
	struct grx500_domain *domain;
	int port, fid, ret;

	lockdep_assert_held(&sw->pce_lock);
	if (sw->software_flow_ingress)
		sw->software_flow_ingress(sw->flow_owner, true);
	if (sw->invalidate_flows)
		sw->invalidate_flows(sw->flow_owner);
	if (sw->failed)
		return -EIO;
	if (sw->software)
		return 0;
	grx500_pause_lan(sw);
	/* Install guards before changing any domain, including a port leaving
	 * its bridge. Remove them only after all forwarding state is complete.
	 */
	for (port = 2; port <= 5; port++) {
		ret = grx500_redirect(sw, port, true);
		if (ret)
			goto fallback;
	}
	for (fid = 1; fid < GRX500_NUM_FIDS; fid++) {
		if (!sw->programmed_vid[fid])
			continue;
		ret = grx500_vlan_write(sw, sw->programmed_vid[fid], 0, BIT(0), 0);
		if (ret)
			goto fallback;
		sw->programmed_vid[fid] = 0;
	}
	for (fid = 1; fid < GRX500_NUM_FIDS; fid++) {
		domain = &sw->domains[fid];
		if (!domain->members)
			continue;
		ret = grx500_vlan_write(sw, domain->vid, fid,
					domain->members | BIT(0),
					sw->vlan_filtering ? domain->untagged :
					domain->members | BIT(0));
		if (ret)
			goto fallback;
		sw->programmed_vid[fid] = domain->vid;
	}
	ret = grx500_fdb_replay(sw);
	if (ret)
		goto fallback;
	for (port = 2; port <= 5; port++) {
		bool offloaded = grx500_port_offloaded(sw, port);

		grx500_port_config(sw, port);
		ret = grx500_private_vid_drop(sw, port,
					      offloaded && sw->vlan_filtering);
		if (ret)
			goto fallback;
		ret = grx500_redirect(sw, port, !offloaded);
		if (ret)
			goto fallback;
	}
	grx500_resume_lan(sw);
	if (sw->software_flow_ingress)
		sw->software_flow_ingress(sw->flow_owner, false);
	return 0;

fallback:
	return grx500_software(sw, "PCE update failed");
}

static int grx500_ageing(struct grx500_switch *sw, unsigned int msecs)
{
	u64 cycles;
	u32 step;
	u16 mant;
	u8 exp, old_exp;

	/* The vendor API supports positive seconds. Keep subsecond and special
	 * Linux ageing modes in software, including a netlink value of one tick.
	 */
	if (msecs < 1000 || !sw->mac_entries)
		return -EOPNOTSUPP;

	/* GSWIP3: time = (MANT + 1) * 2^EXP * MAC table size * 16 / 25 MHz.
	 * Round down so the encoded period does not exceed the requested age.
	 * EXP is 4 bits and MANT is 16 bits, for at most 2^31
	 * cycles. Use the table capability, which also determines sweep time.
	 */
	step = sw->mac_entries * PCE_AGE_STEPS;
	cycles = (u64)msecs * PCE_AGE_CLK_MS;
	if (cycles > ((u64)step << 31))
		return -EOPNOTSUPP;
	cycles = div_u64(cycles, step);
	if (!cycles)
		return -EOPNOTSUPP;
	exp = fls((u32)((cycles - 1) >> 16));
	mant = (cycles >> exp) - 1;

	/* Neither intermediate register period should exceed both the old
	 * and new periods. The caller serializes updates with pce_lock.
	 */
	old_exp = grx500_read(sw, PCE_AGE_EXP) & PCE_AGE_EXP_MASK;
	if (exp > old_exp) {
		grx500_write(sw, PCE_AGE_MANT, mant);
		grx500_update(sw, PCE_AGE_EXP, PCE_AGE_EXP_MASK, exp);
	} else {
		grx500_update(sw, PCE_AGE_EXP, PCE_AGE_EXP_MASK, exp);
		grx500_write(sw, PCE_AGE_MANT, mant);
	}
	return 0;
}

static int grx500_set_ageing_time(struct dsa_switch *ds, unsigned int msecs)
{
	struct grx500_switch *sw = ds->priv;
	int ret = 0;

	mutex_lock(&sw->pce_lock);
	sw->ageing_time = msecs;
	if (!sw->software && grx500_ageing(sw, msecs) && sw->bridge_ports)
		ret = grx500_software(sw, "unsupported ageing time");
	mutex_unlock(&sw->pce_lock);
	return ret;
}

static int grx500_bridge_join(struct dsa_switch *ds, int port,
			      struct dsa_bridge bridge, bool *tx_fwd_offload,
			      struct netlink_ext_ack *extack)
{
	struct grx500_switch *sw = ds->priv;
	struct net_device *user = dsa_to_port(ds, port)->user;
	unsigned long unsupported = BR_HAIRPIN_MODE | BR_ISOLATED |
				    BR_PORT_LOCKED | BR_PORT_MAB;
	clock_t ageing_clock;
	u16 proto;
	int flag, ret = -EOPNOTSUPP;

	*tx_fwd_offload = false;
	mutex_lock(&sw->pce_lock);
	if (sw->failed || (sw->bridge && sw->bridge != bridge.dev))
		goto out;
	if (br_mst_enabled(bridge.dev))
		goto out;
	/* VLAN-aware reconstruction clears the ingress C-tag DEI bit. Keep
	 * these bridges in Linux: standalone ports retain the original tag
	 * bytes through TVM and CPU isolation. Unaware bridges can offload.
	 */
	if (br_vlan_enabled(bridge.dev))
		goto out;
	if (br_vlan_get_proto(bridge.dev, &proto) || proto != ETH_P_8021Q)
		goto out;
	for_each_set_bit(flag, &unsupported, BITS_PER_LONG) {
		if (br_port_flag_is_set(user, BIT(flag)))
			goto out;
	}
	if (!sw->bridge_ports) {
		ageing_clock = br_get_ageing_time(bridge.dev);
		sw->ageing_time = jiffies_to_msecs(clock_t_to_jiffies(ageing_clock));
		if (grx500_ageing(sw, sw->ageing_time))
			goto out;
		sw->bridge = bridge.dev;
		sw->vlan_filtering = br_vlan_enabled(bridge.dev);
		sw->software = false;
	}
	/* DSA replays these attributes after join. Use their current values
	 * before enabling this port so an STP-blocked join cannot forward.
	 */
	sw->ports[port].stp = br_port_get_stp_state(user);
	sw->ports[port].learning = br_port_flag_is_set(user, BR_LEARNING);
	sw->bridge_ports |= BIT(port);
	if (!sw->vlan_filtering) {
		sw->domains[1].vid = GRX500_UNAWARE_VID;
		sw->domains[1].members |= BIT(port);
	}
	ret = grx500_reconfigure(sw);
	if (ret) {
		/* DSA does not call bridge_leave after a failed driver join. */
		sw->bridge_ports &= ~BIT(port);
		sw->domains[1].members &= ~BIT(port);
		if (!sw->bridge_ports)
			sw->bridge = NULL;
	}
out:
	mutex_unlock(&sw->pce_lock);
	return ret;
}

static void grx500_bridge_leave(struct dsa_switch *ds, int port,
				struct dsa_bridge bridge)
{
	struct grx500_switch *sw = ds->priv;
	struct grx500_fdb_entry *fdb, *next;
	int fid;

	mutex_lock(&sw->pce_lock);
	if (sw->bridge != bridge.dev || !(sw->bridge_ports & BIT(port)))
		goto out;
	sw->bridge_ports &= ~BIT(port);
	sw->ports[port].pvid = 0;
	sw->ports[port].learning = false;
	for (fid = 1; fid < GRX500_NUM_FIDS; fid++) {
		sw->domains[fid].members &= ~BIT(port);
		sw->domains[fid].untagged &= ~BIT(port);
		if (!sw->domains[fid].members)
			sw->domains[fid].vid = 0;
	}
	list_for_each_entry_safe(fdb, next, &sw->fdb, list) {
		fdb->owners &= ~BIT(port);
		if (!fdb->owners) {
			list_del(&fdb->list);
			kfree(fdb);
			sw->fdb_count--;
		}
	}
	if (!sw->bridge_ports) {
		grx500_fdb_clear(sw, false);
		sw->bridge = NULL;
		sw->software = false;
	}
	grx500_reconfigure(sw);
out:
	mutex_unlock(&sw->pce_lock);
}

static void grx500_stp_state_set(struct dsa_switch *ds, int port, u8 state)
{
	struct grx500_switch *sw = ds->priv;

	mutex_lock(&sw->pce_lock);
	if (sw->invalidate_flows)
		sw->invalidate_flows(sw->flow_owner);
	sw->ports[port].stp = state;
	grx500_port_config(sw, port);
	mutex_unlock(&sw->pce_lock);
}

static void grx500_fast_age(struct dsa_switch *ds, int port)
{
	struct grx500_switch *sw = ds->priv;

	mutex_lock(&sw->pce_lock);
	if (grx500_port_offloaded(sw, port) && !sw->failed) {
		grx500_pause_lan(sw);
		if (grx500_fdb_replay(sw))
			grx500_software(sw, "FDB flush failed");
		else
			grx500_resume_lan(sw);
	}
	mutex_unlock(&sw->pce_lock);
}

static int grx500_mst_state_set(struct dsa_switch *ds, int port,
				const struct switchdev_mst_state *state)
{
	struct grx500_switch *sw = ds->priv;
	int ret = 0;

	mutex_lock(&sw->pce_lock);
	if (grx500_port_offloaded(sw, port))
		ret = grx500_software(sw, "multiple spanning trees requested");
	mutex_unlock(&sw->pce_lock);
	return ret;
}

static int grx500_vlan_msti_set(struct dsa_switch *ds, struct dsa_bridge bridge,
				const struct switchdev_vlan_msti *msti)
{
	struct grx500_switch *sw = ds->priv;
	int ret = 0;

	mutex_lock(&sw->pce_lock);
	if (msti->msti && sw->bridge == bridge.dev && !sw->software)
		ret = grx500_software(sw, "multiple spanning trees requested");
	mutex_unlock(&sw->pce_lock);
	return ret;
}

static int grx500_vlan_fast_age(struct dsa_switch *ds, int port, u16 vid)
{
	grx500_fast_age(ds, port);
	return 0;
}

static int grx500_pre_bridge_flags(struct dsa_switch *ds, int port,
				   struct switchdev_brport_flags flags,
				   struct netlink_ext_ack *extack)
{
	/* EOPNOTSUPP here skips the apply callback while Linux accepts the flag.
	 * Accept the precheck so the apply callback can restore CPU forwarding.
	 */
	return 0;
}

static int grx500_bridge_flags(struct dsa_switch *ds, int port,
			       struct switchdev_brport_flags flags,
			       struct netlink_ext_ack *extack)
{
	struct grx500_switch *sw = ds->priv;
	unsigned long software_flags = BR_HAIRPIN_MODE | BR_ISOLATED |
				       BR_PORT_LOCKED | BR_PORT_MAB;
	int ret = 0;

	mutex_lock(&sw->pce_lock);
	if (sw->invalidate_flows)
		sw->invalidate_flows(sw->flow_owner);
	if (flags.mask & BR_LEARNING)
		sw->ports[port].learning = !!(flags.val & BR_LEARNING);
	if (!grx500_port_offloaded(sw, port))
		goto out;
	if (flags.val & flags.mask & software_flags) {
		ret = grx500_software(sw, "unsupported bridge port flags");
		goto out;
	}
	/* Flood controls and multicast-to-unicast are applied by Linux because
	 * unknown unicast and all groups have no autonomous physical copies.
	 */
	grx500_port_config(sw, port);
out:
	mutex_unlock(&sw->pce_lock);
	return ret;
}

static int grx500_vlan_filtering(struct dsa_switch *ds, int port, bool enabled,
				 struct netlink_ext_ack *extack)
{
	struct grx500_switch *sw = ds->priv;
	int ret = 0;

	mutex_lock(&sw->pce_lock);
	if (sw->invalidate_flows)
		sw->invalidate_flows(sw->flow_owner);
	if (grx500_port_offloaded(sw, port) && enabled != sw->vlan_filtering)
		ret = grx500_software(sw, "bridge VLAN mode changed");
	mutex_unlock(&sw->pce_lock);
	return ret;
}

static int grx500_vlan_add(struct dsa_switch *ds, int port,
			   const struct switchdev_obj_port_vlan *vlan,
			   struct netlink_ext_ack *extack)
{
	struct grx500_switch *sw = ds->priv;
	struct grx500_domain *domain;
	int fid, ret = 0;

	mutex_lock(&sw->pce_lock);
	if (!grx500_port_offloaded(sw, port) || !sw->vlan_filtering)
		goto out;
	if (!vlan->vid || vlan->vid >= GRX500_CPU_VID) {
		ret = grx500_software(sw, "reserved VLAN requested");
		goto out;
	}
	fid = grx500_fid(sw, vlan->vid);
	if (!fid) {
		for (fid = 1; fid < GRX500_NUM_FIDS; fid++) {
			if (!sw->domains[fid].members)
				break;
		}
		if (fid == GRX500_NUM_FIDS) {
			ret = grx500_software(sw, "VLAN FID capacity exceeded");
			goto out;
		}
	}
	domain = &sw->domains[fid];
	domain->vid = vlan->vid;
	domain->members |= BIT(port);
	if (vlan->flags & BRIDGE_VLAN_INFO_UNTAGGED)
		domain->untagged |= BIT(port);
	else
		domain->untagged &= ~BIT(port);
	if (vlan->flags & BRIDGE_VLAN_INFO_PVID)
		sw->ports[port].pvid = vlan->vid;
	else if (sw->ports[port].pvid == vlan->vid)
		sw->ports[port].pvid = 0;
	ret = grx500_reconfigure(sw);
out:
	mutex_unlock(&sw->pce_lock);
	return ret;
}

static int grx500_vlan_del(struct dsa_switch *ds, int port,
			   const struct switchdev_obj_port_vlan *vlan)
{
	struct grx500_switch *sw = ds->priv;
	int fid, ret = 0;

	mutex_lock(&sw->pce_lock);
	if (!grx500_port_offloaded(sw, port) || !sw->vlan_filtering)
		goto out;
	fid = grx500_fid(sw, vlan->vid);
	if (!fid)
		goto out;
	sw->domains[fid].members &= ~BIT(port);
	sw->domains[fid].untagged &= ~BIT(port);
	if (!sw->domains[fid].members)
		sw->domains[fid].vid = 0;
	if (sw->ports[port].pvid == vlan->vid)
		sw->ports[port].pvid = 0;
	ret = grx500_reconfigure(sw);
out:
	mutex_unlock(&sw->pce_lock);
	return ret;
}

static int grx500_fdb_add(struct dsa_switch *ds, int port,
			  const unsigned char *addr, u16 vid, struct dsa_db db)
{
	struct grx500_switch *sw = ds->priv;
	struct grx500_fdb_entry *fdb;
	int ret = 0;

	mutex_lock(&sw->pce_lock);
	/* Standalone ports already redirect every ingress frame to CPU. */
	if (db.type == DSA_DB_PORT)
		goto out;
	if (db.type != DSA_DB_BRIDGE || db.bridge.dev != sw->bridge ||
	    sw->software || sw->failed || is_multicast_ether_addr(addr) ||
	    (port && !(sw->bridge_ports & BIT(port)))) {
		ret = -EOPNOTSUPP;
		goto out;
	}
	if (sw->vlan_filtering && !vid && port) {
		grx500_software(sw, "VLAN-independent physical FDB entry requested");
		ret = -EOPNOTSUPP;
		goto out;
	}
	fdb = grx500_fdb_find(sw, addr, vid);
	if (!fdb) {
		if (sw->fdb_count >= sw->mac_entries) {
			grx500_software(sw, "static FDB capacity exceeded");
			ret = -ENOSPC;
			goto out;
		}
		fdb = kzalloc(sizeof(*fdb), GFP_KERNEL);
		if (!fdb) {
			grx500_software(sw, "FDB allocation failed");
			ret = -ENOMEM;
			goto out;
		}
		ether_addr_copy(fdb->addr, addr);
		fdb->vid = vid;
		list_add_tail(&fdb->list, &sw->fdb);
		sw->fdb_count++;
	}
	fdb->owners |= BIT(port);
	ret = grx500_fdb_write(sw, fdb);
	if (ret)
		grx500_software(sw, "static FDB update failed");
out:
	mutex_unlock(&sw->pce_lock);
	return ret;
}

static int grx500_fdb_del(struct dsa_switch *ds, int port,
			  const unsigned char *addr, u16 vid, struct dsa_db db)
{
	struct grx500_switch *sw = ds->priv;
	struct grx500_fdb_entry *fdb;
	int ret = 0;

	mutex_lock(&sw->pce_lock);
	if (db.type != DSA_DB_BRIDGE || db.bridge.dev != sw->bridge)
		goto out;
	fdb = grx500_fdb_find(sw, addr, vid);
	if (!fdb)
		goto out;
	fdb->owners &= ~BIT(port);
	if (fdb->owners) {
		ret = grx500_fdb_write(sw, fdb);
	} else {
		list_del(&fdb->list);
		kfree(fdb);
		sw->fdb_count--;
		if (sw->vlan_filtering && !vid) {
			/* Removing a global CPU owner restores all specific owners. */
			grx500_pause_lan(sw);
			ret = grx500_fdb_replay(sw);
			if (!ret)
				grx500_resume_lan(sw);
		} else {
			ret = grx500_fdb_erase(sw, addr, vid);
		}
	}
	if (ret)
		grx500_software(sw, "static FDB deletion failed");
out:
	mutex_unlock(&sw->pce_lock);
	return ret;
}

static int grx500_fdb_dump(struct dsa_switch *ds, int port,
			   dsa_fdb_dump_cb_t *cb, void *data)
{
	struct grx500_switch *sw = ds->priv;
	struct grx500_pce_entry entry;
	unsigned char addr[ETH_ALEN];
	unsigned int index;
	u16 vid;
	u8 fid;
	bool is_static;
	int ret = 0;

	mutex_lock(&sw->pce_lock);
	if (!grx500_port_offloaded(sw, port))
		goto out;
	for (index = 0; index < sw->mac_entries; index++) {
		memset(&entry, 0, sizeof(entry));
		entry.index = index;
		ret = grx500_pce_xfer(sw, MAC_TABLE, 0, &entry);
		if (ret)
			break;
		if (!entry.valid)
			continue;
		fid = entry.key[3] & 0x3f;
		if (!fid || !sw->domains[fid].members)
			continue;
		is_static = !!(entry.val[1] & BIT(0));
		if (is_static ? !(entry.val[0] & BIT(port)) :
		    ((entry.val[0] >> 4) & 0xf) != port)
			continue;
		addr[0] = entry.key[2] >> 8;
		addr[1] = entry.key[2];
		addr[2] = entry.key[1] >> 8;
		addr[3] = entry.key[1];
		addr[4] = entry.key[0] >> 8;
		addr[5] = entry.key[0];
		vid = sw->vlan_filtering ? sw->domains[fid].vid : 0;
		ret = cb(addr, vid, is_static, data);
		if (ret)
			break;
	}
out:
	mutex_unlock(&sw->pce_lock);
	return ret;
}

static int grx500_switchdev_event(struct notifier_block *nb,
				  unsigned long event, void *ptr)
{
	struct grx500_switch *sw = container_of(nb, struct grx500_switch, switchdev_nb);
	struct switchdev_notifier_port_attr_info *info = ptr;
	const struct switchdev_attr *attr;
	int ret = 0;

	/* DSA has no driver callback for these bridge-wide mode changes.
	 * Restore software ownership before Linux changes classification/state.
	 */
	if (event != SWITCHDEV_PORT_ATTR_SET)
		return NOTIFY_DONE;
	attr = info->attr;
	if (attr->id == SWITCHDEV_ATTR_ID_BRIDGE_VLAN_PROTOCOL) {
		if (attr->u.vlan_protocol == ETH_P_8021Q)
			return NOTIFY_DONE;
	} else if (attr->id == SWITCHDEV_ATTR_ID_BRIDGE_MST) {
		if (!attr->u.mst)
			return NOTIFY_DONE;
	} else {
		return NOTIFY_DONE;
	}
	mutex_lock(&sw->pce_lock);
	if (attr->orig_dev == sw->bridge && !sw->software) {
		ret = grx500_software(sw, "unsupported bridge mode");
		info->handled = true;
	}
	mutex_unlock(&sw->pce_lock);
	return notifier_from_errno(ret);
}

static int grx500_setup(struct dsa_switch *ds)
{
	struct grx500_switch *sw = ds->priv;
	struct grx500_pce_entry entry;
	unsigned int rules = grx500_read(sw, 0x00f) & 0x3ff;
	unsigned int vlan_matches = grx500_read(sw, 0x00a) & 0xff;
	unsigned int da_matches = (grx500_read(sw, 0x00c) >> 8) & 0xff;
	unsigned int port, rule, vid;
	int ret;

	if (rules < GRX500_NUM_RULES || !vlan_matches || da_matches < 8)
		return -ENODEV;
	sw->mac_entries = grx500_read(sw, 0x010) & 0x1fff;
	if (!sw->mac_entries)
		return -ENODEV;
	mutex_lock(&sw->pce_lock);
	for (port = 0; port < 16; port++) {
		grx500_port_gate(sw, port, false);
		/* Clear inherited monitoring, special forwarding and learning. */
		grx500_write(sw, PCE_PCTRL0(port), port ? 7 : BIT(11) | PCE_TVM | 7);
		grx500_write(sw, PCE_PCTRL3(port), PCE_LEARNING_OFF);
		grx500_write(sw, PCE_VCTRL(port), 0);
		grx500_write(sw, PCE_PVID(port), GRX500_CPU_VID);
		/* VLAN maps need egress tag modification, including toward CPU0.
		 * Ingress PCP remarking remains disabled in PCTRL0 above.
		 */
		grx500_update(sw, FDMA_PCTRL(port),
			      BIT(1) | FDMA_VLANMOD_MASK | GENMASK(7, 6),
			      FDMA_VLANMOD_BOTH);
	}
	/* VLAN classification is global; standalone ports and CPU0 use TVM to
	 * preserve existing tags as payload. CPU0's internal map is untagged.
	 */
	/* Preserve parser validity and forward learning-limit/locking
	 * violations normally. Linux owns those policies and a full learning
	 * table must not turn unknown-source traffic into a hardware drop.
	 */
	grx500_write(sw, PCE_GCTRL0, (grx500_read(sw, PCE_GCTRL0) & BIT(3)) |
		      BIT(14) | BIT(2) | BIT(1));
	/* Use the direct GSWIP3 VLAN format and clear inherited flow/lock state. */
	grx500_write(sw, PCE_GCTRL1, PCE_VLAN_MODE);
	grx500_update(sw, 0xa47, 3, 0);
	for (rule = 0; rule < rules; rule++) {
		memset(&entry, 0, sizeof(entry));
		entry.index = rule;
		ret = grx500_pce_xfer(sw, FLOW_TABLE, TABLE_WRITE, &entry);
		if (ret)
			goto out;
	}
	for (rule = 0; rule < vlan_matches; rule++) {
		memset(&entry, 0, sizeof(entry));
		entry.index = rule;
		ret = grx500_pce_xfer(sw, VLAN_MATCH_TABLE, TABLE_WRITE, &entry);
		if (ret)
			goto out;
	}
	for (rule = 0; rule < da_matches; rule++) {
		memset(&entry, 0, sizeof(entry));
		entry.index = rule;
		ret = grx500_pce_xfer(sw, MAC_DA_TABLE, TABLE_WRITE, &entry);
		if (ret)
			goto out;
	}
	/* Unknown VIDs have no physical members. CPU-tagged defaults preserve
	 * the VID of trapped groups so Linux can apply its own VLAN admission.
	 */
	for (vid = 0; vid < VLAN_N_VID; vid++) {
		ret = grx500_vlan_write(sw, vid, 0, BIT(0), 0);
		if (ret)
			goto out;
	}
	ret = grx500_vlan_write(sw, GRX500_CPU_VID, 0,
				GRX500_LAN_PORTS | BIT(0), GRX500_LAN_PORTS | BIT(0));
	if (ret)
		goto out;
	ret = grx500_traps_setup(sw);
	if (ret)
		goto out;
	for (port = 2; port <= 5; port++) {
		ret = grx500_redirect(sw, port, true);
		if (ret)
			goto out;
		grx500_port_config(sw, port);
	}
	grx500_write(sw, 0x453, BIT(0));
	grx500_write(sw, 0x454, BIT(0));
	grx500_write(sw, 0x455, BIT(0));
	ret = grx500_mac_flush(sw);
	if (ret)
		goto out;
	ret = grx500_ageing(sw, sw->ageing_time);
	if (ret)
		goto out;
	ret = grx500_pmac_setup(sw);
	if (ret)
		goto out;
	for (port = 0; port < ds->num_ports; port++)
		grx500_update(sw, BM_PCFG(port), BM_PCFG_CNTEN, BM_PCFG_CNTEN);
out:
	if (!ret && sw->software_flow_ingress)
		sw->software_flow_ingress(sw->flow_owner, false);
	mutex_unlock(&sw->pce_lock);
	if (ret)
		return ret;
	return register_switchdev_blocking_notifier(&sw->switchdev_nb);
}

static void grx500_teardown(struct dsa_switch *ds)
{
	struct grx500_switch *sw = ds->priv;
	int port;

	unregister_switchdev_blocking_notifier(&sw->switchdev_nb);
	mutex_lock(&sw->pce_lock);
	if (sw->software_flow_ingress)
		sw->software_flow_ingress(sw->flow_owner, true);
	if (sw->invalidate_flows)
		sw->invalidate_flows(sw->flow_owner);
	sw->failed = true;
	for (port = 0; port < ds->num_ports; port++)
		grx500_port_gate(sw, port, false);
	grx500_fdb_clear(sw, false);
	mutex_unlock(&sw->pce_lock);
}

static enum dsa_tag_protocol grx500_tag_protocol(struct dsa_switch *ds, int port,
						enum dsa_tag_protocol conduit)
{
	return DSA_TAG_PROTO_GRX500;
}

static void grx500_phylink_get_caps(struct dsa_switch *ds, int port,
				    struct phylink_config *config)
{
	__set_bit(PHY_INTERFACE_MODE_INTERNAL, config->supported_interfaces);
	config->mac_capabilities = port ? MAC_10 | MAC_100 | MAC_1000FD : MAC_1000FD;
	if (GRX500_LAN_PORTS & BIT(port))
		config->mac_capabilities |= MAC_SYM_PAUSE | MAC_ASYM_PAUSE;
}

static void grx500_port_set_pause(struct grx500_switch *sw, int port,
				  bool tx, bool rx)
{
	u32 mac = (tx ? MAC_CTRL0_FCON_TX : 0) |
		  (rx ? MAC_CTRL0_FCON_RX : 0);
	u32 phy = (tx ? PHY_ADDR_FCON_TX_EN : PHY_ADDR_FCON_TX) |
		  (rx ? PHY_ADDR_FCON_RX_EN : PHY_ADDR_FCON_RX);

	lockdep_assert_held(&sw->pce_lock);
	if (!(GRX500_LAN_PORTS & BIT(port)))
		return;
	if (!mac)
		mac = MAC_CTRL0_FCON_OFF;
	/* GSW-L PHY ports use bank port - 1, with GSWIP3 force values 1/3
	 * for enable/disable. CPU/P2P and speed/duplex polling stay unchanged.
	 */
	grx500_update(sw, MAC_CTRL0(port), MAC_CTRL0_FCON, mac);
	grx500_update(sw, PHY_ADDR(port), PHY_ADDR_FCON_TX | PHY_ADDR_FCON_RX, phy);
	grx500_update(sw, SDMA_PCTRL(port), SDMA_PCTRL_FCEN,
		       tx ? SDMA_PCTRL_FCEN : 0);
}

static void grx500_mac_config(struct phylink_config *config, unsigned int mode,
			      const struct phylink_link_state *state)
{
	/* The internal MACs take speed/duplex from the switch's MDIO autopoller. */
}

static void grx500_mac_link_down(struct phylink_config *config, unsigned int mode,
				 phy_interface_t interface)
{
	struct dsa_port *dp = dsa_phylink_to_port(config);
	struct grx500_switch *sw = dp->ds->priv;

	mutex_lock(&sw->pce_lock);
	grx500_port_set_pause(sw, dp->index, false, false);
	if (sw->invalidate_flows)
		sw->invalidate_flows(sw->flow_owner);
	sw->ports[dp->index].link = false;
	grx500_port_gate(sw, dp->index, false);
	mutex_unlock(&sw->pce_lock);
}

static void grx500_mac_link_up(struct phylink_config *config, struct phy_device *phy,
			       unsigned int mode, phy_interface_t interface, int speed,
			       int duplex, bool tx_pause, bool rx_pause)
{
	struct dsa_port *dp = dsa_phylink_to_port(config);
	struct grx500_switch *sw = dp->ds->priv;

	mutex_lock(&sw->pce_lock);
	grx500_port_set_pause(sw, dp->index, duplex == DUPLEX_FULL && tx_pause,
			      duplex == DUPLEX_FULL && rx_pause);
	sw->ports[dp->index].link = true;
	grx500_port_gate(sw, dp->index, !sw->failed || !dp->index);
	mutex_unlock(&sw->pce_lock);
}

static int grx500_port_max_mtu(struct dsa_switch *ds, int port)
{
	return ETH_DATA_LEN;
}

static int grx500_port_change_mtu(struct dsa_switch *ds, int port, int mtu)
{
	/* All port buffers already admit the full Ethernet frame plus VLANs. */
	return mtu <= ETH_DATA_LEN ? 0 : -EINVAL;
}

static const struct phylink_mac_ops grx500_phylink_ops = {
	.mac_config = grx500_mac_config,
	.mac_link_down = grx500_mac_link_down,
	.mac_link_up = grx500_mac_link_up,
};

static const struct dsa_switch_ops grx500_switch_ops = {
	.get_tag_protocol = grx500_tag_protocol,
	.setup = grx500_setup,
	.teardown = grx500_teardown,
	.phylink_get_caps = grx500_phylink_get_caps,
	.port_max_mtu = grx500_port_max_mtu,
	.port_change_mtu = grx500_port_change_mtu,
	.get_strings = grx500_get_strings,
	.get_sset_count = grx500_get_sset_count,
	.get_ethtool_stats = grx500_get_ethtool_stats,
	.set_ageing_time = grx500_set_ageing_time,
	.port_bridge_join = grx500_bridge_join,
	.port_bridge_leave = grx500_bridge_leave,
	.port_stp_state_set = grx500_stp_state_set,
	.port_mst_state_set = grx500_mst_state_set,
	.port_fast_age = grx500_fast_age,
	.port_vlan_fast_age = grx500_vlan_fast_age,
	.port_pre_bridge_flags = grx500_pre_bridge_flags,
	.port_bridge_flags = grx500_bridge_flags,
	.port_vlan_filtering = grx500_vlan_filtering,
	.port_vlan_add = grx500_vlan_add,
	.port_vlan_del = grx500_vlan_del,
	.vlan_msti_set = grx500_vlan_msti_set,
	.port_fdb_add = grx500_fdb_add,
	.port_fdb_del = grx500_fdb_del,
	.port_fdb_dump = grx500_fdb_dump,
};

static int grx500_switch_probe(struct platform_device *pdev)
{
	const struct grx500_switch_pdata *data = dev_get_platdata(&pdev->dev);
	struct grx500_switch *sw;

	if (!data || data->cpu_rx_channel > 15)
		return -ENODEV;
	sw = devm_kzalloc(&pdev->dev, sizeof(*sw), GFP_KERNEL);
	if (!sw)
		return -ENOMEM;
	mutex_init(&sw->bm_lock);
	mutex_init(&sw->pce_lock);
	INIT_LIST_HEAD(&sw->fdb);
	sw->ageing_time = 300000;
	sw->switchdev_nb.notifier_call = grx500_switchdev_event;
	sw->base = data->base;
	sw->flow_owner = data->flow_owner;
	sw->invalidate_flows = data->invalidate_flows;
	sw->software_flow_ingress = data->software_flow_ingress;
	sw->cpu_rx_channel = data->cpu_rx_channel;
	sw->ds.dev = &pdev->dev;
	sw->ds.num_ports = GRX500_NUM_PORTS;
	sw->ds.priv = sw;
	sw->ds.ops = &grx500_switch_ops;
	sw->ds.phylink_mac_ops = &grx500_phylink_ops;
	sw->ds.max_num_bridges = 1;
	sw->ds.fdb_isolation = true;
	sw->ds.assisted_learning_on_cpu_port = true;
	sw->ds.untag_vlan_aware_bridge_pvid = true;
	platform_set_drvdata(pdev, sw);
	return dsa_register_switch(&sw->ds);
}

static void grx500_switch_remove(struct platform_device *pdev)
{
	struct grx500_switch *sw = platform_get_drvdata(pdev);

	if (!sw)
		return;

	dsa_unregister_switch(&sw->ds);
}

static void grx500_switch_shutdown(struct platform_device *pdev)
{
	struct grx500_switch *sw = platform_get_drvdata(pdev);

	if (!sw)
		return;

	/* The Ethernet parent fences the shared register/DMA owners afterward. */
	unregister_switchdev_blocking_notifier(&sw->switchdev_nb);
	dsa_switch_shutdown(&sw->ds);
	platform_set_drvdata(pdev, NULL);
}

static const struct of_device_id grx500_switch_match[] = {
	{ .compatible = "lantiq,grx500-switch" },
	{ }
};
MODULE_DEVICE_TABLE(of, grx500_switch_match);

static struct platform_driver grx500_switch_driver = {
	.probe = grx500_switch_probe,
	.remove = grx500_switch_remove,
	.shutdown = grx500_switch_shutdown,
	.driver = {
		.name = "grx500-switch",
		.of_match_table = grx500_switch_match,
	},
};
module_platform_driver(grx500_switch_driver);
MODULE_LICENSE("GPL");
