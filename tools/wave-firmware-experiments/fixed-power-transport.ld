/* SPDX-License-Identifier: GPL-2.0-only */
/* Exact owned spans for original-message transport reconstruction. */
upper_message_send = 0x4e69c;
upper_confirmation = 0x64214;
legacy_fixed_resume = 0x4f834;
request_counter = 0xb4994;
confirmation_counter = 0xb4a68;

SECTIONS
{
 .fixed_umac_entry 0x4f830 : { *(.fixed_umac_entry) }
 .fixed_umac_done_entry 0x62eb4 : { *(.fixed_umac_done_entry) }
 .fixed_umac_version 0x66d48 : { *(.fixed_umac_version) }
 .fixed_umac_transport : { *(.fixed_umac_transport) }
 /DISCARD/ : { *(.text) *(.data) *(.bss) *(.ARC.attributes) }
}
ASSERT(SIZEOF(.fixed_umac_entry) == 4, "047c hook must replace exactly two instructions")
ASSERT(SIZEOF(.fixed_umac_done_entry) == 4, "f7 hook must replace exactly the old no-op")
ASSERT(ADDR(.fixed_umac_version) == 0x66d48, "original version pointer moved")
ASSERT(ADDR(.fixed_umac_transport) % 4 == 0, "ARC call targets must be word aligned")
ASSERT(ADDR(.fixed_umac_transport) + SIZEOF(.fixed_umac_transport) <= 0x66df3,
       "upper transport exceeds the original version-string allocation")
