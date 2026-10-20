/* SPDX-License-Identifier: GPL-2.0-only */
/* Link once for each actual lower copy. Upper bytes are identical. */
ASSERT(DEFINED(wave_lmac), "pass --defsym=wave_lmac=0 or1")
ASSERT(wave_lmac == 0 || wave_lmac == 1, "invalid lower copy")
upper_mode_write = 0x4bb60;
lower_dispatch = wave_lmac == 0 ? 0x101d5c : 0x151b08;
lower_set = wave_lmac == 0 ? 0x101d6e : 0x151b1a;
lower_get = wave_lmac == 0 ? 0xe5cd8 : 0x135c90;
SECTIONS {
 .txop_upper_hook 0x64da6 : { *(.txop_upper_hook) }
 /* The fixed transport ends66dd8; original shortened version string
  * allocation ends66df3. No padding outside that original string is used.
  */
 .txop_upper_guard 0x66dd8 : { *(.txop_upper_guard) }
 .txop_lower_dispatch lower_dispatch : { *(.txop_lower_dispatch) }
 .txop_lower_get lower_get : { *(.txop_lower_get) }
 /DISCARD/ : { *(.text) *(.data) *(.bss) *(.ARC.attributes) }
}
ASSERT(SIZEOF(.txop_upper_hook) == 4, "preserve original call delay slot")
ASSERT(SIZEOF(.txop_upper_guard) > 0 && SIZEOF(.txop_upper_guard) <= 27,
       "UMAC guard exceeds owned original string remainder")
ASSERT(ADDR(.txop_upper_guard) % 4 == 0, "ARC call target must be word aligned")
ASSERT(SIZEOF(.txop_lower_dispatch) == 10, "preserve GET call and SET entry")
ASSERT(ADDR(.txop_lower_dispatch) + 0x12 == lower_set,
       "same-section compare target must be the original SET entry")
ASSERT(SIZEOF(.txop_lower_get) == 36, "getter must fill original function extent")
