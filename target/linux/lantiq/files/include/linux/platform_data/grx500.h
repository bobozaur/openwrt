/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef __LINUX_PLATFORM_DATA_GRX500_H
#define __LINUX_PLATFORM_DATA_GRX500_H

#include <linux/io.h>

/* The Ethernet parent owns the MMIO, MDIO and GPHY firmware. It creates the
 * DSA child after both DMA netdevs and MDIO buses, and destroys it first.
 */
struct grx500_switch_pdata {
	void __iomem *base;
	/* Parent-owned LAN RX channel for CPU-bound frames. Its complete receive
	 * service starts in conduit ndo_open, before a DSA user admits traffic.
	 */
	u8 cpu_rx_channel;
	/* Blocking DSA policy changes retire PAE returns before mutation.
	 * Lock order is RTNL / DSA PCE lock -> Ethernet flow owner.
	 */
	void *flow_owner;
	void (*invalidate_flows)(void *owner);
	void (*software_flow_ingress)(void *owner, bool software);
};

#endif
