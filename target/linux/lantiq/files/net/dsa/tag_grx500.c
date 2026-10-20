// SPDX-License-Identifier: GPL-2.0-only
/* GRX500 GSWIP3 prepended PMAC header. */
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <linux/unaligned.h>
#include <net/dsa.h>
#include "tag.h"

#define GRX500_TAG_LEN 8

static struct sk_buff *grx500_tag_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct dsa_port *dp = dsa_user_to_port(dev);
	u8 *tag;

	/* The PMAC header is not part of the minimum Ethernet frame length. */
	if (__skb_put_padto(skb, ETH_ZLEN, false))
		return NULL;
	tag = skb_push(skb, GRX500_TAG_LEN);
	memset(tag, 0, GRX500_TAG_LEN);
	tag[3] = 0x80;
	put_unaligned_be16(BIT(dp->index), tag + 6);
	return skb;
}

static struct sk_buff *grx500_tag_rcv(struct sk_buff *skb, struct net_device *dev)
{
	u8 *tag;
	unsigned int port;

	if (!pskb_may_pull(skb, GRX500_TAG_LEN))
		return NULL;
	/* The conduit has already called eth_type_trans() on the tagged frame. */
	tag = skb->data - ETH_HLEN;
	port = (tag[2] >> 4) & 0xf;
	skb->dev = dsa_conduit_find_user(dev, 0, port);
	if (!skb->dev)
		return NULL;
	skb_pull_rcsum(skb, GRX500_TAG_LEN);
	/* Unknown unicast, group and host traffic are CPU-only. Hardware
	 * unicast forwarding never creates a CPU copy.
	 */
	skb->offload_fwd_mark = false;
	return skb;
}

static const struct dsa_device_ops grx500_tag_ops = {
	.name = "grx500",
	.proto = DSA_TAG_PROTO_GRX500,
	.xmit = grx500_tag_xmit,
	.rcv = grx500_tag_rcv,
	.needed_headroom = GRX500_TAG_LEN,
};
module_dsa_tag_driver(grx500_tag_ops);
MODULE_ALIAS_DSA_TAG_DRIVER(DSA_TAG_PROTO_GRX500, "grx500");
MODULE_DESCRIPTION("Lantiq GRX500 PMAC tagger");
MODULE_LICENSE("GPL");
