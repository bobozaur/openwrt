// SPDX-License-Identifier: GPL-2.0-or-later
/* GSWIP3 route-table protocol from Lantiq's gsw_pae/gsw_flow_pce drivers.
 *
 * PAE owns tuple lookup, NAT and TTL/checksum rewriting. A private PMAC
 * endpoint returns the completed Ethernet frame to Linux, which owns current
 * bridge/FDB/STP policy and ordinary egress. This deliberately does not claim
 * CPU-bypass forwarding. No CBM, vendor skb fields or fixed-jack return path
 * is involved.
 */
#include <linux/bitmap.h>
#include <linux/etherdevice.h>
#include <linux/if_bridge.h>
#include <linux/if_vlan.h>
#include <linux/ip.h>
#include <linux/ipv6.h>
#include <linux/iopoll.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/refcount.h>
#include <linux/rhashtable.h>
#include <linux/slab.h>
#include <linux/tc_act/tc_csum.h>
#include <linux/udp.h>
#include <linux/tcp.h>
#include <linux/unaligned.h>
#include <net/checksum.h>
#include <net/dsa.h>
#include <net/fib_notifier.h>
#include <net/flow_offload.h>
#include <net/ip.h>
#include <net/ipv6.h>
#include <net/netevent.h>
#include <net/switchdev.h>
#include <net/tcp.h>
#include "lantiq_grx500_flow.h"

#define GF_SESSIONS	4096
#define GF_RESOURCES	120
#define GF_MAC_ENTRIES	512
#define GF_MAC_CAP	(0x017 * 4)
#define GF_DOMAINS	32
#define GF_RETURN_EP	7
#define GF_RETURN_MARK	0xa
#define GF_GENERATION_MAX GENMASK(20, 0)
#define GF_PMAC_LEN	8

#define GF_PCE_CTRL	(0x44f * 4)
#define GF_PAE_CTRL	(0x46d * 4)
#define GF_ADDR		(0x44e * 4)
#define GF_KEY(i)	((0x447 - (i)) * 4)
#define GF_VAL(i)	(((i) < 5 ? 0x44d - (i) : 0x43c - (i)) * 4)
#define GF_MASK(i)	(((i) ? 0x42d - (i) : 0x448) * 4)
#define GF_BUSY		BIT(15)
#define GF_VALID	BIT(12)
#define GF_WRITE	BIT(5)
#define GF_KEYFORM	BIT(11)
#define GF_PMAC_CTRL	(0xd46 * 4)
#define GF_PMAC_ADDR	(0xd45 * 4)
#define GF_PMAC_VAL(i)	((0xd44 - (i)) * 4)
#define GF_FDMA_PCTRL(port) ((0xa80 + (port) * 6) * 4)
#define GF_SDMA_PCTRL(port) ((0xbc0 + (port) * 6) * 4)
#define GF_DMA_PORT_ENABLE BIT(0)

struct grx500_flow_words {
	u16 key[16], val[16], mask[4];
};

struct grx500_flow_tuple {
	union {
		struct { __be32 src, dst; };
		struct { struct in6_addr src6, dst6; };
	};
	__be16 sport, dport;
};

/* The fast-path reference is separate from a cookie's final statistics.
 * Invalidation detaches it before retiring hardware; an RCU grace period
 * protects a completion which already acquired the current Linux owner.
 */
struct grx500_flow_path {
	struct rcu_head rcu;
	struct net_device *ingress, *egress;
	u8 ingress_mac[ETH_ALEN];
	u16 source_ports;
	u16 mtu;
	u32 epoch;
};

struct grx500_flow_binding {
	struct grx500_flow *flow;
	refcount_t refs;
	bool detached;
};

struct grx500_flow_entry {
	struct rhash_head node;
	struct list_head list;
	struct rcu_head rcu;
	unsigned long cookie;
	struct grx500_flow_binding *binding;
	unsigned long retire_state;
	struct_group(incarnation,
		struct grx500_flow_path __rcu *path;
		struct grx500_flow_tuple before, after;
		u8 source_mac[ETH_ALEN], dest_mac[ETH_ALEN];
		u32 generation;
		u16 slot, resource;
		u16 source_mac_index, dest_mac_index;
		u8 domain, mtu_index, protocol;
		bool hardware_valid, resources_owned, source_nat, port_nat, nat, ipv6;
	);
	spinlock_t stats_lock;
	u64 packets, bytes;
	u64 reported_packets, reported_bytes;
	unsigned long lastused;
};

struct grx500_flow_domain {
	struct net_device *dev;
	u16 ports;
	u8 mac[ETH_ALEN];
	u8 extension;
};

struct grx500_flow_mac {
	u8 address[ETH_ALEN];
	u16 users;
};

enum grx500_flow_rx_reject {
	GF_RX_DESCRIPTOR,
	GF_RX_IP_HEADER,
	GF_RX_TRANSPORT,
	GF_RX_TUPLE,
	GF_RX_MAC,
	GF_RX_MTU,
	GF_RX_ALLOC,
	GF_RX_REJECT_COUNT,
};

struct grx500_flow {
	struct device *dev;
	void __iomem *gsw;
	struct net_device *lan, *wan;
	/* RTNL / DSA pce_lock -> lock. The flow worker never acquires RTNL. */
	struct mutex lock;
	struct rhashtable cookies;
	struct list_head entries, blocks;
	struct grx500_flow_entry __rcu *slots[GF_SESSIONS];
	u32 generations[GF_SESSIONS];
	DECLARE_BITMAP(resources, GF_RESOURCES);
	struct grx500_flow_mac macs[GF_MAC_ENTRIES];
	u32 mac_capabilities, source_mac_capacity, dest_mac_capacity;
	u16 source_mac_limit, dest_mac_limit;
	struct grx500_flow_domain domains[GF_DOMAINS];
	u16 mtu_value[8], mtu_users[8];
	u8 next_extension;
	atomic_t epoch;
	atomic_t admission_seq; /* Cold neighbour validation interval only. */
	struct work_struct invalidate_work;
	struct notifier_block netdev_nb, netevent_nb, fib_nb;
	atomic64_t returns, return_bytes, stale_returns, invalid_returns;
	atomic64_t table_failures, capacity_fallbacks;
	atomic64_t rx_examined, rx_candidates, rx_unmarked_nat;
	atomic64_t rx_validated, tx_rejected;
	spinlock_t rx_reject_lock;
	u64 rx_rejects[GF_RX_REJECT_COUNT];
	u64 add_requests, add_accepted, add_rejected;
	unsigned int active;
	bool stopping, faulted, software_ingress;
};

static const struct rhashtable_params grx500_flow_hash_params = {
	.head_offset = offsetof(struct grx500_flow_entry, node),
	.key_offset = offsetof(struct grx500_flow_entry, cookie),
	.key_len = sizeof(unsigned long),
	.automatic_shrinking = true,
};

/* Cumulative counters live until parent teardown. Active sessions and MAC
 * capacities/leases are gauges; accepted return bytes count the IP extent,
 * after Linux egress accepted the frame, without claiming wire delivery.
 */
const char grx500_flow_stats_names[GRX500_FLOW_STATS_COUNT][ETH_GSTRING_LEN] = {
	"pae_active_sessions", "pae_return_packets", "pae_return_l3_bytes",
	"pae_stale_returns", "pae_invalid_returns", "pae_table_failures",
	"pae_capacity_fallbacks",
	"pae_add_requests", "pae_add_accepted", "pae_add_rejected",
	"pae_rx_examined", "pae_rx_candidates", "pae_rx_unmarked_nat",
	"pae_rx_validated", "pae_tx_rejected",
	"pae_rx_reject_descriptor",
	"pae_rx_reject_ip_header",
	"pae_rx_reject_transport",
	"pae_rx_reject_tuple",
	"pae_rx_reject_mac",
	"pae_rx_reject_mtu",
	"pae_rx_reject_alloc",
	"pae_mac_cap14",
	"pae_mac_smac_capacity",
	"pae_mac_dmac_capacity",
	"pae_mac_smac_limit",
	"pae_mac_dmac_limit",
	"pae_mac_in_use",
	"pae_mac_references",
};

/* Statistics observe software ownership only; reading ethtool must not issue
 * hardware table transactions or retain packet-specific metadata.
 */
void grx500_flow_get_stats(struct grx500_flow *f, u64 *data)
{
	unsigned long flags;
	unsigned int i, index = 15 + GF_RX_REJECT_COUNT;
	u64 macs = 0, references = 0;

	memset(data, 0, sizeof(*data) * GRX500_FLOW_STATS_COUNT);
	if (!f)
		return;
	mutex_lock(&f->lock);
	data[0] = f->active;
	data[1] = atomic64_read(&f->returns);
	data[2] = atomic64_read(&f->return_bytes);
	data[3] = atomic64_read(&f->stale_returns);
	data[4] = atomic64_read(&f->invalid_returns);
	data[5] = atomic64_read(&f->table_failures);
	data[6] = atomic64_read(&f->capacity_fallbacks);
	data[7] = f->add_requests;
	data[8] = f->add_accepted;
	data[9] = f->add_rejected;
	data[10] = atomic64_read(&f->rx_examined);
	data[11] = atomic64_read(&f->rx_candidates);
	data[12] = atomic64_read(&f->rx_unmarked_nat);
	data[13] = atomic64_read(&f->rx_validated);
	data[14] = atomic64_read(&f->tx_rejected);
	spin_lock_irqsave(&f->rx_reject_lock, flags);
	memcpy(data + 15, f->rx_rejects, sizeof(f->rx_rejects));
	spin_unlock_irqrestore(&f->rx_reject_lock, flags);
	for (i = 1; i < GF_MAC_ENTRIES; i++) {
		macs += !!f->macs[i].users;
		references += f->macs[i].users;
	}
	data[index++] = f->mac_capabilities;
	data[index++] = f->source_mac_capacity;
	data[index++] = f->dest_mac_capacity;
	data[index++] = f->source_mac_limit;
	data[index++] = f->dest_mac_limit;
	data[index++] = macs;
	data[index] = references;
	mutex_unlock(&f->lock);
}

static void gf_write(struct grx500_flow *f, unsigned int reg, u32 value)
{
	__raw_writel(value, f->gsw + reg);
	wmb();
}

static int gf_idle(struct grx500_flow *f, unsigned int reg)
{
	u32 value;

	return read_poll_timeout(__raw_readl, value, !(value & GF_BUSY),
				1, 10000, false, f->gsw + reg);
}

static int gf_table(struct grx500_flow *f, bool route, unsigned int table,
		    unsigned int index, bool write, bool valid, bool keyform,
		    struct grx500_flow_words *words)
{
	unsigned int reg = route ? GF_PAE_CTRL : GF_PCE_CTRL;
	u32 ctrl = GF_BUSY | table;
	int i, ret;

	lockdep_assert_held(&f->lock);
	ret = gf_idle(f, GF_PCE_CTRL);
	if (!ret)
		ret = gf_idle(f, GF_PAE_CTRL);
	if (ret)
		return ret;
	if (write) {
		for (i = 0; i < 16; i++) {
			gf_write(f, GF_KEY(i), words->key[i]);
			gf_write(f, GF_VAL(i), words->val[i]);
		}
		for (i = 0; i < 4; i++)
			gf_write(f, GF_MASK(i), words->mask[i]);
		ctrl |= GF_WRITE | (valid ? GF_VALID : 0);
		if (keyform)
			ctrl |= GF_KEYFORM;
	}
	gf_write(f, GF_ADDR, index);
	gf_write(f, reg, ctrl);
	ret = gf_idle(f, reg);
	if (!ret && !write)
		for (i = 0; i < 16; i++) {
			words->key[i] = __raw_readl(f->gsw + GF_KEY(i));
			words->val[i] = __raw_readl(f->gsw + GF_VAL(i));
		}
	/* The route and classification APIs share these staging registers. */
	gf_write(f, GF_PCE_CTRL, 0);
	return ret;
}

/* Probe calls these before board publication and before either R transmitter
 * starts. A failed optional flow owner leaves no worker; the unpublished core
 * then owns the same staging registers without allocating a replacement owner.
 */
static int gf_bp_idle(void __iomem *gsw, unsigned int reg)
{
	u32 value;

	return read_poll_timeout(__raw_readl, value, !(value & GF_BUSY),
				1, 10000, false, gsw + reg);
}

static void gf_bp_write(void __iomem *gsw, unsigned int reg, u32 value)
{
	__raw_writel(value, gsw + reg);
	/* Complete each selector/command write before polling shared staging. */
	wmb();
}

static int gf_bp_queues(void __iomem *gsw, bool returns, u32 queues[3])
{
	static const unsigned int ports[] = { 0, GF_RETURN_EP, 15 };
	unsigned int p, bypass, tc, queue, count;
	int ret;

	count = (__raw_readl(gsw + 0x007 * 4) >> 8) & 0x7f;
	memset(queues, 0, 3 * sizeof(*queues));
	for (p = 0; p < ARRAY_SIZE(ports); p++) {
		if (p == 1 && !returns)
			continue;
		for (bypass = 0; bypass < 2; bypass++) {
			for (tc = 0; tc < 16; tc++) {
				ret = gf_bp_idle(gsw, GF_PCE_CTRL);
				if (!ret)
					ret = gf_bp_idle(gsw, GF_PAE_CTRL);
				if (ret)
					return ret;
				gf_bp_write(gsw, GF_ADDR, ports[p] << 4 | tc | bypass << 8);
				gf_bp_write(gsw, GF_PCE_CTRL, GF_BUSY | 0x11);
				ret = gf_bp_idle(gsw, GF_PCE_CTRL);
				if (ret)
					return ret;
				queue = __raw_readl(gsw + GF_VAL(0)) & 0x3f;
				gf_bp_write(gsw, GF_PCE_CTRL, 0);
				if (queue >= count || queue >= 32)
					return -ERANGE;
				queues[p] |= BIT(queue);
			}
		}
	}
	return 0;
}

int grx500_flow_bp_queues(struct grx500_flow *f, void __iomem *gsw, u32 queues[3])
{
	int ret;

	if (f)
		mutex_lock(&f->lock);
	ret = gf_bp_queues(gsw, !!f, queues);
	if (f)
		mutex_unlock(&f->lock);
	return ret;
}

static int gf_bp_map(void __iomem *gsw, unsigned int channel, bool write, u32 values[3])
{
	unsigned int i;
	int ret;

	ret = gf_bp_idle(gsw, GF_PMAC_CTRL);
	if (ret)
		return ret;
	gf_bp_write(gsw, GF_PMAC_ADDR, channel);
	if (write)
		for (i = 0; i < 3; i++)
			gf_bp_write(gsw, GF_PMAC_VAL(i), values[i]);
	gf_bp_write(gsw, GF_PMAC_CTRL, GF_BUSY | (write ? GF_WRITE : 0));
	ret = gf_bp_idle(gsw, GF_PMAC_CTRL);
	if (!ret && !write)
		for (i = 0; i < 3; i++)
			values[i] = __raw_readl(gsw + GF_PMAC_VAL(i)) & 0xffff;
	return ret;
}

int grx500_flow_bp_publish(struct grx500_flow *f, void __iomem *gsw,
			   const u32 queues[3])
{
	u32 current_queues[3], saved[2][3], wanted[2][3], readback[3], mask;
	unsigned int channel;
	int ret, undo;

	if (f)
		mutex_lock(&f->lock);
	/* Reject a queue-map change since the separate BM ownership check. */
	ret = gf_bp_queues(gsw, !!f, current_queues);
	if (ret || memcmp(current_queues, queues, sizeof(current_queues))) {
		ret = ret ?: -EAGAIN;
		goto out;
	}
	for (channel = 0; channel < 2; channel++) {
		ret = gf_bp_map(gsw, channel, false, saved[channel]);
		if (ret)
			goto out;
	}
	wanted[0][0] = BIT(0);
	mask = queues[2];
	wanted[0][1] = mask & 0xffff;
	wanted[0][2] = mask >> 16;
	wanted[1][0] = GENMASK(5, 2);
	mask = queues[0] | queues[1] | queues[2];
	wanted[1][1] = mask & 0xffff;
	wanted[1][2] = mask >> 16;
	for (channel = 0; channel < 2; channel++) {
		ret = gf_bp_map(gsw, channel, true, wanted[channel]);
		if (!ret)
			ret = gf_bp_map(gsw, channel, false, readback);
		if (!ret && memcmp(readback, wanted[channel], sizeof(readback)))
			ret = -EIO;
		if (ret)
			goto restore;
	}
	goto out;
restore:
	/* No transmitter is published. Restore both saved maps on partial setup. */
	for (channel = 0; channel < 2; channel++) {
		undo = gf_bp_map(gsw, channel, true, saved[channel]);
		if (!undo)
			undo = gf_bp_map(gsw, channel, false, readback);
		if (undo || memcmp(readback, saved[channel], sizeof(readback)))
			pr_err("GRX500 backpressure map restore failed on channel %u\n", channel);
	}
out:
	if (f)
		mutex_unlock(&f->lock);
	return ret;
}

static int gf_flags(struct grx500_flow *f, unsigned int index, u16 value,
		    u16 care)
{
	struct grx500_flow_words words = {};
	unsigned int nibble, candidate;

	/* Native parser-flags table stores each four-bit comparison as a
	 * sixteen-bit membership column, not as a flat flags/mask pair.
	 */
	for (nibble = 0; nibble < 4; nibble++)
		for (candidate = 0; candidate < 16; candidate++)
			if (!((candidate ^ (value >> (4 * nibble))) &
			      (care >> (4 * nibble)) & 15))
				words.key[nibble] |= BIT(candidate);
	return gf_table(f, false, 0x18, index, true, true, true, &words);
}

static void gf_rule_init(struct grx500_flow_words *words, unsigned int port)
{
	static const u16 wildcard[] = {
		0x7f00, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff,
		0x7f0f, 0x1f1f, 0x7f1f, 0x7fff, 0xffff, 0xffff,
		0xffff, 0xffff, 0x001f, 0,
	};

	memset(words, 0, sizeof(*words));
	memcpy(words->key, wildcard, sizeof(wildcard));
	words->key[0] |= port;
	words->val[1] = 0xffff;
	words->val[3] = 0x7f1f;
	words->val[6] = 6;
}

static int gf_domain_rules(struct grx500_flow *f, unsigned int index, bool on)
{
	struct grx500_flow_domain *domain = &f->domains[index];
	struct grx500_flow_words words = {};
	unsigned int port, kind;
	int ret;

	if (on) {
		words.key[0] = get_unaligned_be16(domain->mac + 4);
		words.key[1] = get_unaligned_be16(domain->mac + 2);
		words.key[2] = get_unaligned_be16(domain->mac);
		ret = gf_table(f, false, 0x16, index, true, true, false, &words);
		if (ret)
			return ret;
	}
	for (port = 0; port < 16; port++) {
		if (!(domain->ports & BIT(port)))
			continue;
		for (kind = 0; kind < 4; kind++) {
			gf_rule_init(&words, port);
			words.key[5] = (index << 8) | 0xff;
			words.key[12] = ((2 + kind) << 8) | (kind >> 1);
			/* The extension separates ingress domain, IP family and
			 * transport protocol, all absent from the tuple key itself.
			 */
			words.val[9] = ((domain->extension + kind) << 8) | BIT(1) | BIT(0);
			ret = gf_table(f, false, 0x0f, port * 4 + kind,
				       true, on, false, &words);
			if (ret)
				return ret;
		}
	}
	return 0;
}

static int gf_return_setup(struct grx500_flow *f)
{
	unsigned int flow, class, i;
	int ret;

	/* The per-destination PMAC egress table is the producer of the DMA
	 * marker. Packet bytes and physical-ingress PMAC source IDs cannot set
	 * it. Cover every traffic class and both high flow-ID bits.
	 */
	for (flow = 0; flow < 4; flow++)
		for (class = 0; class < 16; class++) {
			ret = gf_idle(f, GF_PMAC_CTRL);
			if (ret)
				return ret;
			gf_write(f, GF_PMAC_ADDR,
				 (flow << 8) | (class << 4) | GF_RETURN_EP);
			for (i = 0; i < 5; i++)
				gf_write(f, GF_PMAC_VAL(i),
					 i == 0 ? GF_RETURN_MARK << 5 : i == 2 ? 1 : 0);
			gf_write(f, GF_PMAC_CTRL, GF_BUSY | GF_WRITE | 2);
		}
	ret = gf_idle(f, GF_PMAC_CTRL);
	if (ret)
		return ret;
	/* Endpoint7 is an internal PMAC destination, never a physical ingress.
	 * Keep the parent's transparent private VLAN and CPU-only miss policy.
	 */
	gf_write(f, (0x480 + GF_RETURN_EP * 10) * 4, BIT(11) | BIT(5) | 7);
	gf_write(f, (0x483 + GF_RETURN_EP * 10) * 4, BIT(15));
	gf_write(f, (0x485 + GF_RETURN_EP * 10) * 4, 0);
	gf_write(f, (0x486 + GF_RETURN_EP * 10) * 4, 4095);
	/* FDMA fetches the completed frame for egress toward PMAC/RX DMA.
	 * SDMA is ingress and must remain disabled on this reserved endpoint.
	 */
	gf_write(f, GF_SDMA_PCTRL(GF_RETURN_EP), 0);
	gf_write(f, GF_FDMA_PCTRL(GF_RETURN_EP), GF_DMA_PORT_ENABLE);
	if ((__raw_readl(f->gsw + GF_SDMA_PCTRL(GF_RETURN_EP)) & GF_DMA_PORT_ENABLE) ||
	    !(__raw_readl(f->gsw + GF_FDMA_PCTRL(GF_RETURN_EP)) & GF_DMA_PORT_ENABLE))
		return -EIO;
	return 0;
}

static u16 gf_hash(const struct grx500_flow_entry *entry, u8 extension)
{
	const struct grx500_flow_tuple *tuple = &entry->before;
	u8 data[13];
	u16 crc = 0;
	unsigned int i, bit;
	__be32 src = tuple->src, dst = tuple->dst;

	if (entry->ipv6) {
		src = tuple->src6.s6_addr32[0] ^ tuple->src6.s6_addr32[1] ^
		      tuple->src6.s6_addr32[2] ^ tuple->src6.s6_addr32[3];
		dst = tuple->dst6.s6_addr32[0] ^ tuple->dst6.s6_addr32[1] ^
		      tuple->dst6.s6_addr32[2] ^ tuple->dst6.s6_addr32[3];
	}
	memcpy(data, &src, 4);
	memcpy(data + 4, &dst, 4);
	memcpy(data + 8, &tuple->sport, 2);
	memcpy(data + 10, &tuple->dport, 2);
	data[12] = extension;
	for (i = 0; i < sizeof(data); i++) {
		crc ^= data[i] << 8;
		for (bit = 0; bit < 8; bit++)
			crc = (crc << 1) ^ ((crc & BIT(15)) ? 0x1021 : 0);
	}
	return crc & (GF_SESSIONS - 1);
}

static int gf_ip_write(struct grx500_flow *f, unsigned int index, __be32 address)
{
	struct grx500_flow_words words = {};
	u32 value = be32_to_cpu(address);

	words.val[0] = value;
	words.val[1] = value >> 16;
	return gf_table(f, true, 1, index, true, false, false, &words);
}

static int gf_ip6_write(struct grx500_flow *f, unsigned int index,
		       const struct in6_addr *address)
{
	struct grx500_flow_words words = {};
	unsigned int i;

	for (i = 0; i < 8; i++)
		words.val[i] = be16_to_cpu(address->s6_addr16[7 - i]);
	words.val[8] = BIT(15);
	return gf_table(f, true, 1, index, true, false, false, &words);
}

static int gf_mac_write(struct grx500_flow *f, unsigned int index, const u8 *mac)
{
	struct grx500_flow_words words = {}, readback = {};
	int ret;

	words.val[0] = get_unaligned_be16(mac + 4);
	words.val[1] = get_unaligned_be16(mac + 2);
	words.val[2] = get_unaligned_be16(mac);
	ret = gf_table(f, true, 2, index, true, false, false, &words);
	if (!ret)
		ret = gf_table(f, true, 2, index, false, false, false, &readback);
	if (!ret && memcmp(words.val, readback.val, 3 * sizeof(words.val[0])))
		ret = -EIO;
	return ret;
}

static int gf_mac_get(struct grx500_flow *f, const u8 *address, bool source)
{
	unsigned int limit = source ? f->source_mac_limit : f->dest_mac_limit;
	unsigned int first, index;
	int ret;

	lockdep_assert_held(&f->lock);
	/* Index zero disables rewriting. The two roles share table 2 but
	 * have independently reported capacities and action-field widths.
	 */
	for (index = 1; index < limit; index++) {
		if (f->macs[index].users &&
		    ether_addr_equal(f->macs[index].address, address)) {
			f->macs[index].users++;
			return index;
		}
	}
	/* Preserve the source-addressable prefix for future source leases.
	 * A matching existing destination address may still share that prefix.
	 */
	first = source ? 1 : min_t(unsigned int, f->source_mac_limit, limit);
	for (index = first; index < limit; index++)
		if (!f->macs[index].users)
			goto found;
	for (index = 1; index < first; index++)
		if (!f->macs[index].users)
			goto found;
	return -ENOSPC;

found:
	ret = gf_mac_write(f, index, address);
	if (ret)
		return ret;
	ether_addr_copy(f->macs[index].address, address);
	f->macs[index].users = 1;
	return index;
}

static void gf_mac_put(struct grx500_flow *f, u16 index)
{
	lockdep_assert_held(&f->lock);
	if (!index)
		return;
	if (WARN_ON(index >= GF_MAC_ENTRIES || !f->macs[index].users))
		return;
	/* Every referencing action has retired before the last lease ends.
	 * A new lease rewrites and checks the row before publishing any action.
	 */
	if (!--f->macs[index].users)
		eth_zero_addr(f->macs[index].address);
}

static void gf_mac_release(struct grx500_flow *f, struct grx500_flow_entry *entry)
{
	/* Index metadata remains immutable for existing RCU readers. Each
	 * incarnation releases once after retirement or before publication.
	 */
	gf_mac_put(f, entry->source_mac_index);
	gf_mac_put(f, entry->dest_mac_index);
}

static int gf_mtu_get(struct grx500_flow *f, u16 mtu)
{
	struct grx500_flow_words words = {};
	int i, ret;

	for (i = 0; i < 8; i++)
		if (f->mtu_users[i] && f->mtu_value[i] == mtu) {
			f->mtu_users[i]++;
			return i;
		}
	for (i = 0; i < 8; i++) {
		if (f->mtu_users[i])
			continue;
		/* Native hardware comparison is length < entry, not <= entry. */
		words.val[0] = mtu + 1;
		ret = gf_table(f, true, 6, i, true, false, false, &words);
		if (ret)
			return ret;
		f->mtu_value[i] = mtu;
		f->mtu_users[i] = 1;
		return i;
	}
	return -ENOSPC;
}

/* A retained cookie/statistics record does not retain live table leases. */
static void gf_resources_release(struct grx500_flow *f,
				 struct grx500_flow_entry *entry)
{
	if (!entry->resources_owned)
		return;
	gf_mac_release(f, entry);
	clear_bit(entry->resource, f->resources);
	f->mtu_users[entry->mtu_index]--;
	entry->resources_owned = false;
}

static int gf_session(struct grx500_flow *f, struct grx500_flow_entry *entry,
		      bool valid)
{
	struct grx500_flow_words words = {}, readback = {};
	unsigned int ip = entry->resource * 12;
	u32 generation = entry->generation;
	int ret;

	words.key[0] = ip + 4;
	words.key[1] = ip;
	words.key[2] = be16_to_cpu(entry->before.dport);
	words.key[3] = be16_to_cpu(entry->before.sport);
	words.key[4] = entry->domain | (valid ? BIT(15) : 0);
	words.key[5] = entry->slot; /* no collision chain: software on collision */
	words.val[0] = BIT(GF_RETURN_EP);
	words.val[1] = (generation & GENMASK(12, 0)) << 3;
	words.val[2] = ip + 8;
	words.val[3] = be16_to_cpu(entry->source_nat ? entry->after.sport : entry->after.dport);
	words.val[4] = entry->mtu_index | (entry->source_mac_index << 8);
	words.val[5] = entry->dest_mac_index;
	words.val[7] = 0x3f << 8;
	words.val[8] = (generation >> 13) << 8;
	words.val[12] = BIT(12) | (entry->source_nat ? BIT(0) : 0);
	if (entry->port_nat)
		words.val[12] |= 3 << 2;
	else if (entry->nat)
		words.val[12] |= 2 << 2;
	else
		words.val[12] |= 1 << 2;
	ret = gf_table(f, true, 0, entry->slot, true, valid, false, &words);
	if (!ret)
		ret = gf_table(f, true, 0, entry->slot, false, false, false, &readback);
	if (!ret && (readback.key[4] != words.key[4] ||
		     readback.key[5] != words.key[5] ||
		     readback.val[1] != words.val[1] || readback.val[8] != words.val[8] ||
		     readback.val[4] != words.val[4] || readback.val[5] != words.val[5]))
		ret = -EIO;
	return ret;
}

static void gf_path_free(struct rcu_head *rcu)
{
	struct grx500_flow_path *path = container_of(rcu, struct grx500_flow_path, rcu);

	dev_put(path->ingress);
	dev_put(path->egress);
	kfree(path);
}

static void gf_entry_free(struct rcu_head *rcu)
{
	struct grx500_flow_entry *entry = container_of(rcu, struct grx500_flow_entry, rcu);

	if (refcount_dec_and_test(&entry->binding->refs))
		kfree(entry->binding);
	kfree(entry);
}

static int gf_port(struct grx500_flow *f, struct net_device *dev)
{
	struct dsa_port *dp;

	if (dev == f->wan)
		return 15;
	dp = dsa_port_from_netdev(dev);
	if (IS_ERR(dp) || dsa_port_to_conduit(dp) != f->lan ||
	    dp->index < 2 || dp->index > 5)
		return -EOPNOTSUPP;
	return dp->index;
}

/* Resolve a bridge rather than retaining the redirect action's cached DSA
 * jack. Linux bridge xmit consults the current FDB and STP state on each
 * completion. Tagged/stacked or foreign-device paths are not advertised.
 */
static struct net_device *gf_logical(struct grx500_flow *f, struct net_device *dev)
{
	struct net_device *upper;

	if (netif_is_bridge_master(dev))
		return br_vlan_enabled(dev) ? NULL : dev;
	if (gf_port(f, dev) < 0)
		return NULL;
	upper = netdev_master_upper_dev_get_rcu(dev);
	if (!upper)
		return dev;
	if (!netif_is_bridge_master(upper) || br_vlan_enabled(upper))
		return NULL;
	return upper;
}

static u16 gf_ports(struct grx500_flow *f, struct net_device *dev)
{
	struct net_device *lower;
	struct list_head *iter;
	int port;
	u16 ports = 0;

	if (!netif_is_bridge_master(dev)) {
		port = gf_port(f, dev);
		return port < 0 ? 0 : BIT(port);
	}
	if (br_vlan_enabled(dev))
		return 0;
	iter = &dev->adj_list.lower;
	while ((lower = netdev_next_lower_dev_rcu(dev, &iter))) {
		port = gf_port(f, lower);
		if (port >= 0)
			ports |= BIT(port);
	}
	return ports;
}

static bool gf_ingress_port(struct grx500_flow *f, struct net_device *dev)
{
	struct net_device *bridge;
	const unsigned long unsupported = BR_HAIRPIN_MODE | BR_ISOLATED |
					 BR_PORT_LOCKED | BR_PORT_MAB;
	int port = gf_port(f, dev);

	if (port < 0 || !netif_running(dev) || !netif_carrier_ok(dev))
		return false;
	if (port != 15 && READ_ONCE(f->software_ingress))
		return false;
	bridge = netdev_master_upper_dev_get_rcu(dev);
	if (!bridge)
		return true;
	return netif_is_bridge_master(bridge) && !br_vlan_enabled(bridge) &&
		!br_mst_enabled(bridge) && br_port_get_stp_state(dev) == BR_STATE_FORWARDING &&
		!br_port_flag_is_set(dev, unsupported);
}

static u16 gf_ingress_ports(struct grx500_flow *f, struct net_device *dev)
{
	struct net_device *lower;
	struct list_head *iter;
	u16 ports = 0;
	int port;

	if (!netif_is_bridge_master(dev)) {
		port = gf_port(f, dev);
		return port >= 0 && gf_ingress_port(f, dev) ? BIT(port) : 0;
	}
	if (br_vlan_enabled(dev) || br_mst_enabled(dev))
		return 0;
	iter = &dev->adj_list.lower;
	while ((lower = netdev_next_lower_dev_rcu(dev, &iter))) {
		port = gf_port(f, lower);
		if (port >= 0 && gf_ingress_port(f, lower))
			ports |= BIT(port);
	}
	return ports;
}

void grx500_flow_software_ingress(void *data, bool software)
{
	struct grx500_flow *f = data;

	/* Published by the DSA owner before its CPU/software fallback can
	 * bypass hardware ingress state. The caller then invalidates flows.
	 */
	if (f)
		WRITE_ONCE(f->software_ingress, software);
}

static int gf_domain_get(struct grx500_flow *f, struct net_device *dev, u16 ports,
			 const u8 *mac)
{
	struct grx500_flow_domain *domain;
	unsigned int i, free = GF_DOMAINS;
	int ret;

	for (i = 0; i < GF_DOMAINS; i++) {
		domain = &f->domains[i];
		if (!domain->dev) {
			if (free == GF_DOMAINS)
				free = i;
			continue;
		}
		if (domain->dev == dev && domain->ports == ports &&
		    ether_addr_equal(domain->mac, mac))
			return domain->extension;
		if (domain->ports & ports)
			return -EOPNOTSUPP;
	}
	/* Never reinterpret an old classifier's extension as a different Linux
	 * ingress incarnation. Exhaustion remains explicit software fallback;
	 * only the parent's full GSW reset permits this namespace to restart.
	 */
	if (free == GF_DOMAINS || !f->next_extension)
		return -ENOSPC;
	domain = &f->domains[free];
	domain->dev = dev;
	dev_hold(dev);
	domain->ports = ports;
	ether_addr_copy(domain->mac, mac);
	domain->extension = f->next_extension;
	f->next_extension += 4;
	ret = gf_domain_rules(f, free, true);
	if (ret) {
		gf_domain_rules(f, free, false);
		dev_put(domain->dev);
		domain->dev = NULL;
		return ret;
	}
	return domain->extension;
}

static int gf_parse(struct grx500_flow *f, struct flow_cls_offload *cls,
		    struct grx500_flow_entry *entry, u32 epoch)
{
	u64 keys = BIT_ULL(FLOW_DISSECTOR_KEY_META) |
		BIT_ULL(FLOW_DISSECTOR_KEY_CONTROL) |
		BIT_ULL(FLOW_DISSECTOR_KEY_BASIC) |
		BIT_ULL(FLOW_DISSECTOR_KEY_PORTS);
	struct flow_rule *rule = flow_cls_offload_flow_rule(cls);
	struct flow_match_ipv4_addrs addresses;
	struct flow_match_ipv6_addrs addresses6;
	struct flow_match_tcp tcp;
	struct flow_match_control control;
	struct flow_match_basic basic;
	struct flow_match_ports ports;
	struct flow_match_meta meta;
	struct flow_action_entry *action;
	struct grx500_flow_path *path;
	struct net_device *input, *logical, *output = NULL;
	u8 mac_fields = 0;
	bool snat, dnat;
	int i, ret = -EOPNOTSUPP;

	if ((rule->match.dissector->used_keys & keys) != keys)
		return -EOPNOTSUPP;
	flow_rule_match_meta(rule, &meta);
	flow_rule_match_control(rule, &control);
	flow_rule_match_basic(rule, &basic);
	flow_rule_match_ports(rule, &ports);
	entry->ipv6 = control.key->addr_type == FLOW_DISSECTOR_KEY_IPV6_ADDRS;
	entry->protocol = basic.key->ip_proto;
	keys |= BIT_ULL(entry->ipv6 ? FLOW_DISSECTOR_KEY_IPV6_ADDRS : FLOW_DISSECTOR_KEY_IPV4_ADDRS);
	if (entry->protocol == IPPROTO_TCP)
		keys |= BIT_ULL(FLOW_DISSECTOR_KEY_TCP);
	if (rule->match.dissector->used_keys != keys)
		return -EOPNOTSUPP;
	if (meta.mask->ingress_ifindex != U32_MAX ||
	    (!entry->ipv6 && control.key->addr_type != FLOW_DISSECTOR_KEY_IPV4_ADDRS) ||
	    control.mask->addr_type != U16_MAX || control.mask->flags ||
	    basic.key->n_proto != htons(entry->ipv6 ? ETH_P_IPV6 : ETH_P_IP) || basic.mask->n_proto != htons(U16_MAX) ||
	    (entry->protocol != IPPROTO_UDP && entry->protocol != IPPROTO_TCP) || basic.mask->ip_proto != U8_MAX ||
	    ports.mask->src != htons(U16_MAX) || ports.mask->dst != htons(U16_MAX))
		return -EOPNOTSUPP;
	if (entry->protocol == IPPROTO_TCP) {
		flow_rule_match_tcp(rule, &tcp);
		if (tcp.key->flags || tcp.mask->flags != htons(TCPHDR_FIN | TCPHDR_RST))
			return -EOPNOTSUPP;
	}
	if (entry->ipv6) {
		static const struct in6_addr full_mask = { .s6_addr32 = { ~0, ~0, ~0, ~0 } };

		flow_rule_match_ipv6_addrs(rule, &addresses6);
		if (memcmp(&addresses6.mask->src, &full_mask, sizeof(full_mask)) ||
		    memcmp(&addresses6.mask->dst, &full_mask, sizeof(full_mask)))
			return -EOPNOTSUPP;
		entry->before.src6 = addresses6.key->src;
		entry->before.dst6 = addresses6.key->dst;
	} else {
		flow_rule_match_ipv4_addrs(rule, &addresses);
		if (addresses.mask->src != htonl(U32_MAX) || addresses.mask->dst != htonl(U32_MAX))
			return -EOPNOTSUPP;
		entry->before.src = addresses.key->src;
		entry->before.dst = addresses.key->dst;
	}
	entry->before.sport = ports.key->src;
	entry->before.dport = ports.key->dst;
	entry->after = entry->before;

	flow_action_for_each(i, action, &rule->action) {
		u32 value = action->mangle.val;
		u16 half;

		switch (action->id) {
		case FLOW_ACTION_REDIRECT:
			if (output)
				return -EOPNOTSUPP;
			output = action->dev;
			break;
		case FLOW_ACTION_CSUM:
			if (action->csum_flags & ~(TCA_CSUM_UPDATE_FLAG_IPV4HDR |
						 TCA_CSUM_UPDATE_FLAG_UDP | TCA_CSUM_UPDATE_FLAG_TCP))
				return -EOPNOTSUPP;
			break;
		case FLOW_ACTION_MANGLE:
			switch (action->mangle.htype) {
			case FLOW_ACT_MANGLE_HDR_TYPE_ETH:
				/* nf_flow_table's split MAC actions use native half-word
				 * values. Do not inherit a little-endian driver's byte
				 * pointer shortcut on this big-endian CPU.
				 */
				if (!action->mangle.offset && !action->mangle.mask) {
					memcpy(entry->dest_mac, &value, 4);
					mac_fields |= BIT(0);
				} else if (action->mangle.offset == 4 && action->mangle.mask == 0xffff0000) {
					half = value;
					memcpy(entry->dest_mac + 4, &half, 2);
					mac_fields |= BIT(1);
				} else if (action->mangle.offset == 4 && action->mangle.mask == 0xffff) {
					half = value >> 16;
					memcpy(entry->source_mac, &half, 2);
					mac_fields |= BIT(2);
				} else if (action->mangle.offset == 8 && !action->mangle.mask) {
					memcpy(entry->source_mac + 2, &value, 4);
					mac_fields |= BIT(3);
				} else {
					return -EOPNOTSUPP;
				}
				break;
			case FLOW_ACT_MANGLE_HDR_TYPE_IP4:
				if (entry->ipv6 || action->mangle.mask)
					return -EOPNOTSUPP;
				if (action->mangle.offset == offsetof(struct iphdr, saddr))
					entry->after.src = (__force __be32)value;
				else if (action->mangle.offset == offsetof(struct iphdr, daddr))
					entry->after.dst = (__force __be32)value;
				else
					return -EOPNOTSUPP;
				break;
			case FLOW_ACT_MANGLE_HDR_TYPE_IP6:
				if (!entry->ipv6 || action->mangle.mask || (action->mangle.offset & 3))
					return -EOPNOTSUPP;
				if (action->mangle.offset >= offsetof(struct ipv6hdr, saddr) &&
				    action->mangle.offset < offsetof(struct ipv6hdr, daddr))
					memcpy(entry->after.src6.s6_addr + action->mangle.offset -
					       offsetof(struct ipv6hdr, saddr), &value, 4);
				else if (action->mangle.offset >= offsetof(struct ipv6hdr, daddr) &&
					 action->mangle.offset < sizeof(struct ipv6hdr))
					memcpy(entry->after.dst6.s6_addr + action->mangle.offset -
					       offsetof(struct ipv6hdr, daddr), &value, 4);
				else
					return -EOPNOTSUPP;
				break;
			case FLOW_ACT_MANGLE_HDR_TYPE_UDP:
			case FLOW_ACT_MANGLE_HDR_TYPE_TCP:
				if (action->mangle.offset ||
				    action->mangle.htype != (entry->protocol == IPPROTO_TCP ?
					FLOW_ACT_MANGLE_HDR_TYPE_TCP : FLOW_ACT_MANGLE_HDR_TYPE_UDP))
					return -EOPNOTSUPP;
				if (action->mangle.mask == ~htonl(0xffff0000))
					entry->after.sport = htons(ntohl(value) >> 16);
				else if (action->mangle.mask == ~htonl(0xffff))
					entry->after.dport = htons(ntohl(value));
				else
					return -EOPNOTSUPP;
				break;
			default:
				return -EOPNOTSUPP;
			}
			break;
		default:
			return -EOPNOTSUPP;
		}
	}
	if (!output || mac_fields != 15 || !is_valid_ether_addr(entry->source_mac) ||
	    !is_valid_ether_addr(entry->dest_mac))
		return -EOPNOTSUPP;
	if (entry->ipv6) {
		snat = !ipv6_addr_equal(&entry->before.src6, &entry->after.src6);
		dnat = !ipv6_addr_equal(&entry->before.dst6, &entry->after.dst6);
	} else {
		snat = entry->before.src != entry->after.src;
		dnat = entry->before.dst != entry->after.dst;
	}
	snat |= entry->before.sport != entry->after.sport;
	dnat |= entry->before.dport != entry->after.dport;
	if (snat && dnat)
		return -EOPNOTSUPP;
	entry->source_nat = snat;
	entry->nat = snat || dnat;
	entry->port_nat = entry->before.sport != entry->after.sport || entry->before.dport != entry->after.dport;

	path = kzalloc(sizeof(*path), GFP_KERNEL);
	if (!path)
		return -ENOMEM;
	rcu_read_lock();
	input = dev_get_by_index_rcu(&init_net, meta.key->ingress_ifindex);
	if (input && net_eq(dev_net(output), &init_net)) {
		/* META authorizes exactly this input. Resolving its L3 bridge
		 * must not broaden a DSA user-port match to every bridge member.
		 */
		path->source_ports = gf_ingress_ports(f, input);
		logical = gf_logical(f, input);
		output = gf_logical(f, output);
		if (logical && output && logical != output && netif_running(input) && netif_running(output)) {
			if (path->source_ports && gf_ports(f, output)) {
				path->ingress = input;
				path->egress = output;
				ether_addr_copy(path->ingress_mac, logical->dev_addr);
				dev_hold(input);
				dev_hold(output);
				path->mtu = min_t(unsigned int, READ_ONCE(output->mtu), ETH_DATA_LEN);
				path->epoch = epoch;
				ret = 0;
			}
		}
	}
	rcu_read_unlock();
	if (ret) {
		kfree(path);
		return ret;
	}
	rcu_assign_pointer(entry->path, path);
	return 0;
}

static void gf_fault(struct grx500_flow *f, int error)
{
	f->faulted = true;
	atomic64_inc(&f->table_failures);
	atomic_inc(&f->epoch);
	dev_err_ratelimited(f->dev,
		"PAE table transaction failed (%d); admission stopped, late returns discarded\n",
		error);
}

static int gf_retire(struct grx500_flow *f, struct grx500_flow_entry *entry)
{
	struct grx500_flow_path *path;
	int ret = 0;

	path = rcu_replace_pointer(entry->path, NULL, lockdep_is_held(&f->lock));
	if (rcu_access_pointer(f->slots[entry->slot]) == entry)
		RCU_INIT_POINTER(f->slots[entry->slot], NULL);
	if (path) {
		/* Snapshot after both lookup owners detach, before later reuse. */
		entry->retire_state = get_state_synchronize_rcu();
		call_rcu(&path->rcu, gf_path_free);
	}
	if (entry->hardware_valid) {
		ret = gf_session(f, entry, false);
		if (ret) {
			gf_fault(f, ret);
			return ret;
		}
		entry->hardware_valid = false;
		f->active--;
	}
	gf_resources_release(f, entry);
	return ret;
}

static void gf_invalidate_locked(struct grx500_flow *f, bool domains)
{
	struct grx500_flow_entry *entry;
	unsigned int i;
	int ret;

	/* Both directions and all held topology references retire together.
	 * Keep cookie statistics until the flowtable's ordinary DESTROY call.
	 */
	list_for_each_entry(entry, &f->entries, list)
		gf_retire(f, entry);
	if (!domains)
		return;
	for (i = 0; i < GF_DOMAINS; i++) {
		if (!f->domains[i].dev)
			continue;
		ret = gf_domain_rules(f, i, false);
		if (ret)
			gf_fault(f, ret);
		dev_put(f->domains[i].dev);
		f->domains[i].dev = NULL;
	}
}

void grx500_flow_invalidate(void *data)
{
	struct grx500_flow *f = data;

	if (!f)
		return;
	atomic_inc(&f->epoch);
	mutex_lock(&f->lock);
	gf_invalidate_locked(f, false);
	mutex_unlock(&f->lock);
	/* A completed Linux transmit which acquired the old path is finished
	 * before the caller changes its DSA topology or stops the DMA owner.
	 */
	synchronize_net();
}

static void gf_invalidate_work(struct work_struct *work)
{
	struct grx500_flow *f = container_of(work, struct grx500_flow, invalidate_work);

	mutex_lock(&f->lock);
	gf_invalidate_locked(f, false);
	mutex_unlock(&f->lock);
}

static void gf_invalidate_async(struct grx500_flow *f)
{
	/* Atomic notifiers close the software return gate before queuing the
	 * sleeping MMIO transaction. No stale action can reach Linux egress.
	 */
	atomic_inc(&f->epoch);
	if (!READ_ONCE(f->stopping))
		schedule_work(&f->invalidate_work);
}

static int gf_netevent(struct notifier_block *nb, unsigned long event, void *data)
{
	struct grx500_flow *f = container_of(nb, struct grx500_flow, netevent_nb);
	struct neighbour *neigh = data;
	struct grx500_flow_entry *entry;
	struct grx500_flow_path *path;
	unsigned int i;
	u8 ha[MAX_ADDR_LEN], nud_state;

	if (event != NETEVENT_NEIGH_UPDATE)
		return NOTIFY_DONE;
	rcu_read_lock();
	/* Fence first/unpublished admissions without broadening the existing
	 * published-slot invalidation policy to every relevant notification.
	 */
	if (gf_ports(f, neigh->dev)) {
		atomic_inc(&f->admission_seq);
		/* Paired with the staged-slot publisher before its sequence check. */
		smp_mb__after_atomic();
	}
	nud_state = READ_ONCE(neigh->nud_state);
	neigh_ha_snapshot(ha, neigh, neigh->dev);
	for (i = 0; i < GF_SESSIONS; i++) {
		entry = rcu_dereference(f->slots[i]);
		if (!entry)
			continue;
		path = rcu_dereference(entry->path);
		if (path && path->egress == neigh->dev &&
		    (!(nud_state & NUD_VALID) ||
		     !ether_addr_equal(entry->dest_mac, ha))) {
			gf_invalidate_async(f);
			break;
		}
	}
	rcu_read_unlock();
	return NOTIFY_DONE;
}

static int gf_fib_event(struct notifier_block *nb, unsigned long event, void *data)
{
	struct grx500_flow *f = container_of(nb, struct grx500_flow, fib_nb);
	struct fib_notifier_info *info = data;

	if (info->family == AF_INET || info->family == AF_INET6)
		gf_invalidate_async(f);
	return NOTIFY_DONE;
}

static bool gf_domain_affected(struct grx500_flow_domain *domain,
			       struct net_device *dev, struct net_device *upper)
{
	struct net_device *master;

	if (!domain->dev)
		return false;
	master = netdev_master_upper_dev_get_rcu(domain->dev);
	return domain->dev == dev || domain->dev == upper || master == dev ||
		(upper && master == upper) ||
		netdev_master_upper_dev_get_rcu(dev) == domain->dev;
}

static int gf_netdev_event(struct notifier_block *nb, unsigned long event, void *data)
{
	struct grx500_flow *f = container_of(nb, struct grx500_flow, netdev_nb);
	struct net_device *dev = netdev_notifier_info_to_dev(data), *upper = NULL;
	struct grx500_flow_entry *entry;
	struct grx500_flow_path *path;
	bool relevant = false;
	unsigned int i;
	int ret;

	switch (event) {
	case NETDEV_PRECHANGEUPPER:
	case NETDEV_CHANGEUPPER:
		upper = ((struct netdev_notifier_changeupper_info *)data)->upper_dev;
		break;
	case NETDEV_PRECHANGEMTU:
	case NETDEV_CHANGEMTU:
	case NETDEV_PRE_CHANGEADDR:
	case NETDEV_CHANGEADDR:
	case NETDEV_GOING_DOWN:
	case NETDEV_UNREGISTER:
	case NETDEV_CHANGE:
		break;
	default:
		return NOTIFY_DONE;
	}
	/* Close the admission interval before waiting on the publisher's lock.
	 * It may be installing the first rule, with no slot/domain to scan yet.
	 */
	rcu_read_lock();
	relevant = gf_port(f, dev) >= 0 || gf_ports(f, dev) ||
		(upper && gf_ports(f, upper));
	rcu_read_unlock();
	if (relevant)
		atomic_inc(&f->epoch);

	/* The finite extension namespace belongs to actual ingress identities.
	 * Unrelated WLAN/WG/veth lifetimes must not consume it.
	 */
	mutex_lock(&f->lock);
	rcu_read_lock();
	for (i = 0; i < GF_DOMAINS; i++)
		if (gf_domain_affected(&f->domains[i], dev, upper))
			relevant = true;
	list_for_each_entry(entry, &f->entries, list) {
		path = rcu_dereference_protected(entry->path, lockdep_is_held(&f->lock));
		if (path && (path->ingress == dev || path->egress == dev ||
			     path->ingress == upper || path->egress == upper ||
			     netdev_master_upper_dev_get_rcu(dev) == path->egress ||
			     netdev_master_upper_dev_get_rcu(path->ingress) == dev))
			relevant = true;
	}
	rcu_read_unlock();
	mutex_unlock(&f->lock);
	if (!relevant)
		return NOTIFY_DONE;
	grx500_flow_invalidate(f);
	if (event == NETDEV_CHANGEUPPER || event == NETDEV_CHANGEADDR ||
	    event == NETDEV_UNREGISTER || event == NETDEV_CHANGE) {
		atomic_inc(&f->epoch);
		mutex_lock(&f->lock);
		for (i = 0; i < GF_DOMAINS; i++) {
			struct grx500_flow_domain *domain = &f->domains[i];
			bool affected;

			rcu_read_lock();
			affected = gf_domain_affected(domain, dev, upper);
			if (affected && event == NETDEV_CHANGE)
				affected = gf_ingress_ports(f, domain->dev) != domain->ports;
			rcu_read_unlock();
			if (!affected)
				continue;
			ret = gf_domain_rules(f, i, false);
			if (ret)
				gf_fault(f, ret);
			dev_put(domain->dev);
			domain->dev = NULL;
		}
		mutex_unlock(&f->lock);
	}
	return NOTIFY_DONE;
}

static bool gf_rule_equal(const struct grx500_flow_entry *old,
			  const struct grx500_flow_path *old_path,
			  const struct grx500_flow_entry *entry,
			  const struct grx500_flow_path *path)
{
	return old_path && old_path->epoch == path->epoch &&
		old_path->ingress == path->ingress && old_path->egress == path->egress &&
		old_path->source_ports == path->source_ports && old_path->mtu == path->mtu &&
		ether_addr_equal(old_path->ingress_mac, path->ingress_mac) &&
		!memcmp(&old->before, &entry->before, sizeof(entry->before)) &&
		!memcmp(&old->after, &entry->after, sizeof(entry->after)) &&
		ether_addr_equal(old->source_mac, entry->source_mac) &&
		ether_addr_equal(old->dest_mac, entry->dest_mac) &&
		old->protocol == entry->protocol && old->ipv6 == entry->ipv6 &&
		old->nat == entry->nat && old->source_nat == entry->source_nat &&
		old->port_nat == entry->port_nat;
}

static int gf_replace(struct grx500_flow *f, struct grx500_flow_binding *binding,
		      struct flow_cls_offload *cls)
{
	struct grx500_flow_entry *entry, *old;
	struct grx500_flow_path *path, *old_path;
	unsigned int resource, slot;
	int ret, domain, mtu;
	bool retained = false;
	u32 epoch, admission_seq;

	if (f->stopping || f->faulted || binding->detached ||
	    !netif_running(f->lan) || !netif_running(f->wan))
		return -EOPNOTSUPP;

	/* Validate the actual native source inside this admission interval.
	 * Atomic notifiers fence every later relevant change, including before
	 * a first slot exists. No provider lock/context survives this call.
	 */
	epoch = atomic_read(&f->epoch);
	admission_seq = atomic_read(&f->admission_seq);
	if (cls->validate && !cls->validate(cls->validate_priv))
		return -EAGAIN;

	old = rhashtable_lookup_fast(&f->cookies, &cls->cookie, grx500_flow_hash_params);
	if (old && old->binding != binding)
		return -EEXIST;

	entry = kzalloc(sizeof(*entry), GFP_KERNEL);
	if (!entry)
		return -ENOMEM;
	entry->cookie = cls->cookie;
	entry->binding = binding;
	spin_lock_init(&entry->stats_lock);
	ret = gf_parse(f, cls, entry, epoch);
	if (ret)
		goto free;
	path = rcu_dereference_protected(entry->path, lockdep_is_held(&f->lock));

	if (path->epoch != atomic_read(&f->epoch) ||
	    admission_seq != atomic_read(&f->admission_seq)) {
		ret = -EAGAIN;
		goto free_path;
	}

	if (old) {
		old_path = rcu_dereference_protected(old->path, lockdep_is_held(&f->lock));
		if (old->hardware_valid && gf_rule_equal(old, old_path, entry, path)) {
			ret = 0;
			goto free_path;
		}
		ret = gf_retire(f, old);
		if (ret)
			goto free_path;
		/* Old completions retain immutable rule data and update these same
		 * statistics until their read-side section finishes. Never reset the
		 * cookie, hash/list membership, binding reference or counters.
		 */
		cond_synchronize_rcu(old->retire_state);
		old->incarnation = entry->incarnation;
		kfree(entry);
		entry = old;
		retained = true;
	}

	domain = gf_domain_get(f, path->ingress, path->source_ports, path->ingress_mac);
	if (domain < 0) {
		ret = domain;
		goto free_path;
	}
	domain += (entry->ipv6 ? 2 : 0) | (entry->protocol == IPPROTO_TCP ? 1 : 0);

	slot = gf_hash(entry, domain);
	resource = find_first_zero_bit(f->resources, GF_RESOURCES);
	if (rcu_access_pointer(f->slots[slot]) || resource == GF_RESOURCES ||
	    f->generations[slot] == GF_GENERATION_MAX) {
		ret = -ENOSPC;
		goto free_path;
	}
	entry->slot = slot;
	entry->resource = resource;
	entry->domain = domain;
	entry->generation = ++f->generations[slot];

	mtu = gf_mtu_get(f, path->mtu);
	if (mtu < 0) {
		ret = mtu;
		goto free_path;
	}
	entry->mtu_index = mtu;
	set_bit(resource, f->resources);
	entry->resources_owned = true;

	if (entry->ipv6) {
		ret = gf_ip6_write(f, resource * 12, &entry->before.src6);
		if (!ret)
			ret = gf_ip6_write(f, resource * 12 + 4, &entry->before.dst6);
		if (!ret)
			ret = gf_ip6_write(f, resource * 12 + 8,
				entry->source_nat ? &entry->after.src6 : &entry->after.dst6);
	} else {
		ret = gf_ip_write(f, resource * 12, entry->before.src);
		if (!ret)
			ret = gf_ip_write(f, resource * 12 + 4, entry->before.dst);
		if (!ret)
			ret = gf_ip_write(f, resource * 12 + 8,
					  entry->source_nat ? entry->after.src : entry->after.dst);
	}
	if (!ret) {
		ret = gf_mac_get(f, entry->source_mac, true);
		if (ret > 0) {
			entry->source_mac_index = ret;
			ret = gf_mac_get(f, entry->dest_mac, false);
			if (ret > 0) {
				entry->dest_mac_index = ret;
				ret = 0;
			}
		}
	}
	if (ret) {
		if (ret != -ENOSPC)
			gf_fault(f, ret);
		goto free_resource;
	}

	if (!retained) {
		ret = rhashtable_insert_fast(&f->cookies, &entry->node, grx500_flow_hash_params);
		if (ret)
			goto free_resource;
		/* The entry may outlive callback removal, including a failed checked
		 * table retirement. Its RCU release owns this separate reference.
		 */
		refcount_inc(&binding->refs);
		list_add_tail(&entry->list, &f->entries);
	}
	/* Host return owner is ready before hardware validity is published. */
	rcu_assign_pointer(f->slots[slot], entry);
	/* A notifier either sees this staged path or this admission observes its
	 * sequence change before hardware validity can expose a stale action.
	 */
	smp_mb();
	if (admission_seq != atomic_read(&f->admission_seq) ||
	    path->epoch != atomic_read(&f->epoch)) {
		ret = -EAGAIN;
		goto retire;
	}
	entry->hardware_valid = true;
	f->active++;

	ret = gf_session(f, entry, true);
	if (!ret && path->epoch == atomic_read(&f->epoch) &&
	    admission_seq == atomic_read(&f->admission_seq))
		return 0;
	if (!ret)
		ret = -EAGAIN;
	else
		gf_fault(f, ret);
retire:
	/* A failed publication is never reported as hardware support. Keep
	 * resources pinned if the checked invalidation also fails.
	 */
	if (gf_retire(f, entry))
		return ret;
	if (retained)
		return ret;
	list_del(&entry->list);
	rhashtable_remove_fast(&f->cookies, &entry->node, grx500_flow_hash_params);
	call_rcu(&entry->rcu, gf_entry_free);
	return ret;

free_resource:
	gf_resources_release(f, entry);
free_path:
	if (ret == -ENOSPC)
		atomic64_inc(&f->capacity_fallbacks);
	RCU_INIT_POINTER(entry->path, NULL);
	dev_put(path->ingress);
	dev_put(path->egress);
	kfree(path);
free:
	if (!retained)
		kfree(entry);
	return ret;
}

static int gf_remove_entry(struct grx500_flow *f, struct grx500_flow_entry *entry)
{
	int ret;

	ret = gf_retire(f, entry);
	if (ret)
		return ret;
	rhashtable_remove_fast(&f->cookies, &entry->node, grx500_flow_hash_params);
	list_del(&entry->list);
	call_rcu(&entry->rcu, gf_entry_free);
	return 0;
}

static int gf_destroy_entry(struct grx500_flow *f,
			    struct grx500_flow_binding *binding,
			    struct flow_cls_offload *cls)
{
	struct grx500_flow_entry *entry;

	entry = rhashtable_lookup_fast(&f->cookies, &cls->cookie, grx500_flow_hash_params);
	if (!entry || entry->binding != binding)
		return -ENOENT;
	return gf_remove_entry(f, entry);
}

static int gf_stats(struct grx500_flow *f, struct grx500_flow_binding *binding,
		    struct flow_cls_offload *cls)
{
	struct grx500_flow_entry *entry;
	u64 packets, bytes;

	entry = rhashtable_lookup_fast(&f->cookies, &cls->cookie, grx500_flow_hash_params);
	if (!entry || entry->binding != binding)
		return -ENOENT;
	spin_lock_bh(&entry->stats_lock);
	packets = entry->packets;
	bytes = entry->bytes;
	flow_stats_update(&cls->stats, bytes - entry->reported_bytes,
			  packets - entry->reported_packets, 0,
			  READ_ONCE(entry->lastused), FLOW_ACTION_HW_STATS_DELAYED);
	entry->reported_bytes = bytes;
	entry->reported_packets = packets;
	spin_unlock_bh(&entry->stats_lock);
	return 0;
}

static int gf_block_cb(enum tc_setup_type type, void *type_data, void *cb_priv)
{
	struct grx500_flow_binding *binding = cb_priv;
	struct grx500_flow *f = binding->flow;
	struct flow_cls_offload *cls = type_data;
	int ret;

	if (type != TC_SETUP_CLSFLOWER)
		return -EOPNOTSUPP;
	mutex_lock(&f->lock);
	switch (cls->command) {
	case FLOW_CLS_REPLACE:
		f->add_requests++;
		ret = gf_replace(f, binding, cls);
		if (!ret)
			f->add_accepted++;
		else
			f->add_rejected++;
		break;
	case FLOW_CLS_DESTROY:
		ret = gf_destroy_entry(f, binding, cls);
		break;
	case FLOW_CLS_STATS:
		ret = gf_stats(f, binding, cls);
		break;
	default:
		ret = -EOPNOTSUPP;
	}
	mutex_unlock(&f->lock);
	return ret;
}

static void gf_binding_release(void *data)
{
	struct grx500_flow_binding *binding = data;

	if (refcount_dec_and_test(&binding->refs))
		kfree(binding);
}

int grx500_flow_setup_tc(struct grx500_flow *f, enum tc_setup_type type,
			void *type_data)
{
	struct flow_block_offload *offload = type_data;
	struct grx500_flow_entry *entry, *next;
	struct grx500_flow_binding *binding;
	struct flow_block_cb *block;
	bool detached = false;
	int ret = 0;

	if (!f || type != TC_SETUP_FT ||
	    offload->binder_type != FLOW_BLOCK_BINDER_TYPE_CLSACT_INGRESS)
		return -EOPNOTSUPP;
	offload->driver_block_list = &f->blocks;
	mutex_lock(&f->lock);
	switch (offload->command) {
	case FLOW_BLOCK_BIND:
		if (f->stopping) {
			ret = -EOPNOTSUPP;
			break;
		}
		block = flow_block_cb_lookup(offload->block, gf_block_cb, f);
		if (block) {
			flow_block_cb_incref(block);
			break;
		}
		/* nftables binds the replacement block before unbinding the old
		 * generation. Share within one block, never across generations.
		 */
		binding = kzalloc(sizeof(*binding), GFP_KERNEL);
		if (!binding) {
			ret = -ENOMEM;
			break;
		}
		binding->flow = f;
		refcount_set(&binding->refs, 1);
		block = flow_block_cb_alloc(gf_block_cb, f, binding, gf_binding_release);
		if (IS_ERR(block)) {
			ret = PTR_ERR(block);
			gf_binding_release(binding);
			break;
		}
		flow_block_cb_incref(block);
		flow_block_cb_add(block, offload);
		list_add_tail(&block->driver_list, &f->blocks);
		break;
	case FLOW_BLOCK_UNBIND:
		block = flow_block_cb_lookup(offload->block, gf_block_cb, f);
		if (!block) {
			ret = -ENOENT;
			break;
		}
		if (!flow_block_cb_decref(block)) {
			binding = flow_block_cb_priv(block);
			/* Close this owner's returns together, without advancing the
			 * shared epoch or invalidating a replacement block's entries.
			 */
			WRITE_ONCE(binding->detached, true);
			list_for_each_entry_safe(entry, next, &f->entries, list) {
				if (entry->binding != binding)
					continue;
				/* A failed table retirement retains the resource and its
				 * binding reference under the existing faulted gate.
				 */
				gf_remove_entry(f, entry);
			}
			flow_block_cb_remove(block, offload);
			list_del(&block->driver_list);
			detached = true;
		}
		break;
	default:
		ret = -EOPNOTSUPP;
		break;
	}
	mutex_unlock(&f->lock);
	if (detached)
		synchronize_net();
	return ret;
}

static void gf_rx_reject(struct grx500_flow *f,
			enum grx500_flow_rx_reject stage)
{
	unsigned long flags;

	spin_lock_irqsave(&f->rx_reject_lock, flags);
	f->rx_rejects[stage]++;
	spin_unlock_irqrestore(&f->rx_reject_lock, flags);
	atomic64_inc(&f->invalid_returns);
}

bool grx500_flow_receive(struct grx500_flow *f, struct napi_struct *napi,
			u32 dw0, u32 dw1, const u8 *frame, unsigned int len)
{
	struct grx500_flow_entry *entry;
	struct grx500_flow_path *path;
	struct grx500_flow_tuple tuple;
	struct ethhdr eth;
	enum grx500_flow_rx_reject rejection = GF_RX_DESCRIPTOR;
	union {
		struct iphdr v4;
		struct ipv6hdr v6;
	} ip;
	union {
		struct udphdr udp;
		struct tcphdr tcp;
	} transport;
	const u8 *packet;
	struct sk_buff *skb;
	unsigned int iplen, iphlen, l4len, available, source, slot = dw1 >> 20;
	u32 generation = (dw0 & GENMASK(12, 0)) | (((dw0 >> 17) & 0xff) << 13);
	unsigned long flags;
	u8 protocol;
	bool ipv6;
	int ret;

	if (f)
		atomic64_inc(&f->rx_examined);
	if (((dw1 >> 8) & 15) != GF_RETURN_EP && ((dw1 >> 4) & 15) != GF_RETURN_MARK) {
		/* Native RX DW1 bit18 is the NAT result flag. It does not by
		 * itself establish a trusted completed-return identity.
		 */
		if (f && (dw1 & BIT(18)))
			atomic64_inc(&f->rx_unmarked_nat);
		return false;
	}
	/* A marked but malformed/stale return can never re-enter IP receive:
	 * its header may already have been translated and TTL decremented.
	 */
	if (!f)
		return true;
	atomic64_inc(&f->rx_candidates);
	if (((dw1 >> 8) & 15) != GF_RETURN_EP || ((dw1 >> 4) & 15) != GF_RETURN_MARK ||
	    (dw1 & BIT(19)) || len < GF_PMAC_LEN + ETH_HLEN)
		goto invalid;
	source = frame[2] >> 4;
	/* Descriptor offsets do not guarantee CPU alignment. Copy each fixed
	 * header before inspecting fields, including the IPv4 checksum words.
	 */
	memcpy(&eth, frame + GF_PMAC_LEN, sizeof(eth));
	rejection = GF_RX_IP_HEADER;
	packet = frame + GF_PMAC_LEN + ETH_HLEN;
	available = len - GF_PMAC_LEN - ETH_HLEN;
	if (eth.h_proto == htons(ETH_P_IP)) {
		iphlen = sizeof(ip.v4);
		if (available < iphlen)
			goto invalid;
		memcpy(&ip.v4, packet, iphlen);
		if (ip.v4.version != 4 || ip.v4.ihl != 5 ||
		    ip_is_fragment(&ip.v4) || !ip.v4.ttl ||
		    ip_fast_csum(&ip.v4, 5))
			goto invalid;
		ipv6 = false;
		protocol = ip.v4.protocol;
		iplen = ntohs(ip.v4.tot_len);
		tuple.src = ip.v4.saddr;
		tuple.dst = ip.v4.daddr;
	} else if (eth.h_proto == htons(ETH_P_IPV6)) {
		iphlen = sizeof(ip.v6);
		if (available < iphlen)
			goto invalid;
		memcpy(&ip.v6, packet, iphlen);
		if (ip.v6.version != 6 || !ip.v6.hop_limit)
			goto invalid;
		ipv6 = true;
		protocol = ip.v6.nexthdr;
		iplen = iphlen + ntohs(ip.v6.payload_len);
		tuple.src6 = ip.v6.saddr;
		tuple.dst6 = ip.v6.daddr;
	} else {
		goto invalid;
	}
	if (iplen < iphlen || iplen > available)
		goto invalid;
	l4len = iplen - iphlen;
	rejection = GF_RX_TRANSPORT;
	if (protocol == IPPROTO_UDP) {
		if (l4len < sizeof(transport.udp))
			goto invalid;
		memcpy(&transport.udp, packet + iphlen, sizeof(transport.udp));
		if (ntohs(transport.udp.len) != l4len ||
		    (ipv6 && !transport.udp.check))
			goto invalid;
		tuple.sport = transport.udp.source;
		tuple.dport = transport.udp.dest;
	} else if (protocol == IPPROTO_TCP) {
		if (l4len < sizeof(transport.tcp))
			goto invalid;
		memcpy(&transport.tcp, packet + iphlen, sizeof(transport.tcp));
		/* FIN/RST must miss the hardware route classifier. A marked
		 * exception cannot safely re-enter receive after NAT/TTL rewrite.
		 */
		if (transport.tcp.doff < sizeof(transport.tcp) / 4 ||
		    transport.tcp.doff * 4 > l4len ||
		    transport.tcp.fin || transport.tcp.rst)
			goto invalid;
		tuple.sport = transport.tcp.source;
		tuple.dport = transport.tcp.dest;
	} else {
		/* No IPv6 extension/fragment headers or other L4 protocol. */
		goto invalid;
	}
	rcu_read_lock();
	entry = rcu_dereference(f->slots[slot]);
	if (!entry || entry->generation != generation)
		goto stale;
	path = rcu_dereference(entry->path);
	if (!path || READ_ONCE(entry->binding->detached) ||
	    path->epoch != atomic_read(&f->epoch) ||
	    !(path->source_ports & BIT(source)) ||
	    !(gf_ingress_ports(f, path->ingress) & BIT(source)) ||
	    !netif_running(path->ingress) || !netif_running(path->egress))
		goto stale;
	rejection = GF_RX_TUPLE;
	if (entry->ipv6 != ipv6 || entry->protocol != protocol ||
	    entry->after.sport != tuple.sport || entry->after.dport != tuple.dport)
		goto invalid_locked;
	if (ipv6) {
		if (memcmp(&entry->after.src6, &tuple.src6, sizeof(tuple.src6)) ||
		    memcmp(&entry->after.dst6, &tuple.dst6, sizeof(tuple.dst6)))
			goto invalid_locked;
	} else if (entry->after.src != tuple.src || entry->after.dst != tuple.dst) {
		goto invalid_locked;
	}
	rejection = GF_RX_MAC;
	if (!ether_addr_equal(entry->source_mac, eth.h_source) ||
	    !ether_addr_equal(entry->dest_mac, eth.h_dest))
		goto invalid_locked;
	rejection = GF_RX_MTU;
	if (iplen > min_t(unsigned int, path->mtu, READ_ONCE(path->egress->mtu)))
		goto invalid_locked;
	atomic64_inc(&f->rx_validated);
	rejection = GF_RX_ALLOC;
	skb = napi_alloc_skb(napi, ETH_HLEN + iplen);
	if (!skb)
		goto invalid_locked;
	/* Ethernet padding is not part of the declared IP packet. */
	skb_put_data(skb, frame + GF_PMAC_LEN, ETH_HLEN + iplen);
	skb_reset_mac_header(skb);
	skb_set_network_header(skb, ETH_HLEN);
	skb_set_transport_header(skb, ETH_HLEN + iphlen);
	skb->dev = path->egress;
	skb->protocol = eth.h_proto;
	skb->ip_summed = CHECKSUM_NONE;
	ret = dev_queue_xmit(skb);
	if (!net_xmit_eval(ret)) {
		spin_lock_irqsave(&entry->stats_lock, flags);
		entry->packets++;
		entry->bytes += iplen;
		WRITE_ONCE(entry->lastused, jiffies);
		spin_unlock_irqrestore(&entry->stats_lock, flags);
		atomic64_inc(&f->returns);
		atomic64_add(iplen, &f->return_bytes);
	} else
		atomic64_inc(&f->tx_rejected);
	rcu_read_unlock();
	return true;
stale:
	atomic64_inc(&f->stale_returns);
	rcu_read_unlock();
	return true;
invalid_locked:
	gf_rx_reject(f, rejection);
	rcu_read_unlock();
	return true;
invalid:
	gf_rx_reject(f, rejection);
	return true;
}

struct grx500_flow *grx500_flow_create(struct device *dev, void __iomem *gsw,
				       struct net_device *lan, struct net_device *wan)
{
	struct grx500_flow *f;
	struct grx500_flow_words vlan = {};
	int ret;

	f = kzalloc(sizeof(*f), GFP_KERNEL);
	if (!f)
		return ERR_PTR(-ENOMEM);
	f->dev = dev;
	f->gsw = gsw;
	/* Native CAP14 sizes are powers of two. Table 2 is shared, while
	 * route action/readback source indexes are 8 bits and destination 9.
	 */
	f->mac_capabilities = __raw_readl(gsw + GF_MAC_CAP);
	f->source_mac_capacity = 1U << ((f->mac_capabilities >> 12) & 15);
	f->dest_mac_capacity = 1U << ((f->mac_capabilities >> 8) & 15);
	f->source_mac_limit = min_t(u32, f->source_mac_capacity, 256);
	f->dest_mac_limit = min_t(u32, f->dest_mac_capacity, GF_MAC_ENTRIES);
	f->lan = lan;
	f->wan = wan;
	f->software_ingress = true;
	mutex_init(&f->lock);
	spin_lock_init(&f->rx_reject_lock);
	INIT_LIST_HEAD(&f->entries);
	INIT_LIST_HEAD(&f->blocks);
	INIT_WORK(&f->invalidate_work, gf_invalidate_work);
	f->next_extension = 4;
	atomic_set(&f->epoch, 1);
	atomic_set(&f->admission_seq, 0);
	ret = rhashtable_init(&f->cookies, &grx500_flow_hash_params);
	if (ret)
		goto free;
	mutex_lock(&f->lock);
	/* Plain outer IPv4/IPv6, no VLAN, encapsulation, fragments or options.
	 * The loaded parser's TCP rows test FIN and RST explicitly and set
	 * ROUTEXP; requiring that flag clear retains their native software path.
	 */
	ret = gf_flags(f, 0, 0x8000, 0xfe1f);
	if (!ret)
		ret = gf_flags(f, 1, 0, 0xfe1f);
	if (!ret)
		ret = gf_flags(f, 2, 0x0020, 0x65bf);
	if (!ret)
		ret = gf_flags(f, 3, 0x0010, 0x65bf);
	if (!ret)
		ret = gf_flags(f, 4, 0x0021, 0x65bf);
	if (!ret)
		ret = gf_flags(f, 5, 0x0011, 0x65bf);
	if (!ret)
		ret = gf_table(f, false, 2, 4095, false, false, false, &vlan);
	if (!ret) {
		vlan.val[1] |= BIT(GF_RETURN_EP);
		ret = gf_table(f, false, 2, 4095, true, false, false, &vlan);
	}
	if (!ret)
		ret = gf_return_setup(f);
	mutex_unlock(&f->lock);
	if (ret)
		goto hash;
	f->netdev_nb.notifier_call = gf_netdev_event;
	f->netevent_nb.notifier_call = gf_netevent;
	f->fib_nb.notifier_call = gf_fib_event;
	ret = register_netdevice_notifier(&f->netdev_nb);
	if (ret)
		goto hash;
	ret = register_netevent_notifier(&f->netevent_nb);
	if (ret)
		goto netdev;
	ret = register_fib_notifier(&init_net, &f->fib_nb, NULL, NULL);
	if (ret)
		goto netevent;
	return f;
netevent:
	unregister_netevent_notifier(&f->netevent_nb);
netdev:
	unregister_netdevice_notifier(&f->netdev_nb);
hash:
	WRITE_ONCE(f->stopping, true);
	cancel_work_sync(&f->invalidate_work);
	rhashtable_destroy(&f->cookies);
free:
	kfree(f);
	return ERR_PTR(ret);
}

void grx500_flow_destroy(struct grx500_flow *f)
{
	struct grx500_flow_entry *entry, *next;

	if (!f)
		return;
	WRITE_ONCE(f->stopping, true);
	unregister_fib_notifier(&init_net, &f->fib_nb);
	unregister_netevent_notifier(&f->netevent_nb);
	unregister_netdevice_notifier(&f->netdev_nb);
	cancel_work_sync(&f->invalidate_work);
	grx500_flow_invalidate(f);
	mutex_lock(&f->lock);
	gf_invalidate_locked(f, true);
	list_for_each_entry_safe(entry, next, &f->entries, list) {
		rhashtable_remove_fast(&f->cookies, &entry->node, grx500_flow_hash_params);
		list_del(&entry->list);
		call_rcu(&entry->rcu, gf_entry_free);
	}
	mutex_unlock(&f->lock);
	rhashtable_destroy(&f->cookies);
	/* Called only after all native netdev/block and RX owners are retired. */
	WARN_ON(!list_empty(&f->blocks));
	rcu_barrier();
	kfree(f);
}

void grx500_flow_stop(struct grx500_flow *f)
{
	if (!f)
		return;
	WRITE_ONCE(f->stopping, true);
	grx500_flow_invalidate(f);
	cancel_work_sync(&f->invalidate_work);
}
