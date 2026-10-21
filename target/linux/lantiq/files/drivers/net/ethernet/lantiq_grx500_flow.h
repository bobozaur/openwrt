/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _LANTIQ_GRX500_FLOW_H
#define _LANTIQ_GRX500_FLOW_H

#include <linux/netdevice.h>
#include <linux/ethtool.h>

struct grx500_flow;
#define GRX500_FLOW_STATS_COUNT 17
extern const char grx500_flow_stats_names[GRX500_FLOW_STATS_COUNT][ETH_GSTRING_LEN];

struct grx500_flow *grx500_flow_create(struct device *dev, void __iomem *gsw,
				       struct net_device *lan,
				       struct net_device *wan);
int grx500_flow_bp_queues(struct grx500_flow *flow, void __iomem *gsw,
			  u32 queues[3]);
int grx500_flow_bp_publish(struct grx500_flow *flow, void __iomem *gsw,
			   const u32 queues[3]);
void grx500_flow_destroy(struct grx500_flow *flow);
void grx500_flow_stop(struct grx500_flow *flow);
void grx500_flow_get_stats(struct grx500_flow *flow, u64 *data);
void grx500_flow_invalidate(void *flow);
void grx500_flow_software_ingress(void *flow, bool software);
int grx500_flow_setup_tc(struct grx500_flow *flow, enum tc_setup_type type,
			void *type_data);
bool grx500_flow_receive(struct grx500_flow *flow, struct napi_struct *napi,
			u32 dw0, u32 dw1, const u8 *frame, unsigned int len);

#endif
