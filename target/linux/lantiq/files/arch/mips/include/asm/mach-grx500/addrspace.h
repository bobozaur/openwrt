/* SPDX-License-Identifier: GPL-2.0-only */
/* Board overrides applied after asm/addrspace.h's legacy definitions.
 * Preserve the vendor's DRAM and MMIO aliases under EVA_1GB.
 */
#undef KSEG0
#undef CKSEG0
#undef KSEGX
#undef CPHYSADDR
#undef CKSEG0ADDR
#undef CKSEG1ADDR
#undef KSEG0ADDR
#undef KSEG1ADDR
#define KSEG0 0x60000000
#define CKSEG0 0x60000000
#define KSEGX(a) (((_ACAST32_(a)) < _AC(0xa0000000, UL)) ? KSEG0 : ((_ACAST32_(a)) & _AC(0xe0000000, UL)))
#define CPHYSADDR(a) ((_ACAST32_(a)) - PAGE_OFFSET + PHYS_OFFSET)
#define GRX500_RPHYSADDR(a) ((_ACAST32_(a)) & 0x1fffffff)
#define CKSEG0ADDR(a) ((_ACAST32_(a)))
#define CKSEG1ADDR(a) (GRX500_RPHYSADDR(a) | KSEG1)
#define KSEG0ADDR(a) CKSEG0ADDR(a)
#define KSEG1ADDR(a) CKSEG1ADDR(a)
