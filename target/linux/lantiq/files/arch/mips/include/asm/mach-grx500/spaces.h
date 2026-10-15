/* SPDX-License-Identifier: GPL-2.0-only */
/* GRX500 EVA_1GB layout from the TP-Link BSP; upstream EVA page conversions
 * already handle the PAGE_OFFSET/PHYS_OFFSET difference.
 */
#ifndef _ASM_GRX500_SPACES_H
#define _ASM_GRX500_SPACES_H
#include <linux/const.h>
#define PAGE_OFFSET _AC(0x60000000, UL)
#define PHYS_OFFSET _AC(0x20000000, UL)
#define CAC_BASE _AC(0x60000000, UL)
#define UNCAC_BASE _AC(0xa0000000, UL)
#define IO_BASE UNCAC_BASE
#define HIGHMEM_START _AC(0xffff0000, UL)
#define MAP_BASE _AC(0xc0000000, UL)
/* EVA maps only 0xc0000000-0xdfffffff through the TLB; keep fixmap/vmalloc here. */
#define FIXADDR_TOP _AC(0xdffe0000, UL)
#define __pa_symbol(x) __pa(x)
#include <asm/mach-generic/spaces.h>
#endif
