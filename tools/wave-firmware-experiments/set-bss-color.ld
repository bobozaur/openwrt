/* SPDX-License-Identifier: GPL-2.0-only */
/* Exact symbol maps for the two lower-MAC copies in 06.00.08.00.87. */
ASSERT(DEFINED(wave_lmac), "pass --defsym=wave_lmac=0 or 1")
ASSERT(wave_lmac == 0 || wave_lmac == 1, "invalid lower-MAC copy")
set_bss_addr = wave_lmac == 0 ? 0x1013a4 : 0x151150;
color_slot_addr = wave_lmac == 0 ? 0xe92f0 : 0x139170;
vap_valid_base = wave_lmac == 0 ? 0x114c28 : 0x164898;
station_nodes = wave_lmac == 0 ? 0x114204 : 0x163e74;
init_vap_record = wave_lmac == 0 ? 0xe84cc : 0x13834c;
init_vap_protection = wave_lmac == 0 ? 0xed018 : 0x13ceb4;
init_vap_tx = wave_lmac == 0 ? 0xfb2e0 : 0x14b08c;
init_vap_barrier = wave_lmac == 0 ? 0xe1930 : 0x131930;
set_protection = wave_lmac == 0 ? 0xed494 : 0x13d330;
set_preamble = wave_lmac == 0 ? 0xecad8 : 0x13c974;
message_alloc = wave_lmac == 0 ? 0xf12c8 : 0x141034;
message_send = wave_lmac == 0 ? 0xf1378 : 0x1410e4;
firmware_fatal = wave_lmac == 0 ? 0xe5f5c : 0x135f14;

SECTIONS
{
  .color_helpers color_slot_addr : { *(.color_helpers) }
  .set_bss set_bss_addr : { *(.set_bss) }
  /DISCARD/ : { *(.text) *(.data) *(.bss) *(.ARC.attributes) }
}
ASSERT(SIZEOF(.color_helpers) <= 108, "color helper exceeds original function")
ASSERT(SIZEOF(.set_bss) <= 144, "SET_BSS exceeds original function")
