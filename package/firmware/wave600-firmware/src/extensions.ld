/* SPDX-License-Identifier: GPL-2.0-only */
/* Shared layout; the selected CPU script supplies exact native bounds. */
SECTIONS
{
 .extension_init_hook native_user_init : { *(.extension_init_hook) }
 .extension_unused_marker native_unused_publication : {
  *(.extension_unused_marker)
 }
 .extension_code extension_code_begin : {
  *(.extension_boot)
  *(.extension.text .extension.text.*)
  *(.rts.text .rts.text.*)
  *(.extension.rodata .extension.rodata.*)
  . = ALIGN(4);
  __extension_code_end = .;
 }
 .extension_state extension_state_begin (NOLOAD) : {
  __extension_state_start = .;
  *(.extension.state .extension.state.*)
  *(.rts.state .rts.state.*)
  . = ALIGN(4);
  __extension_state_end = .;
 }
 /DISCARD/ : { *(.text) *(.data) *(.bss) *(.ARC.attributes) }
}
ASSERT(SIZEOF(.extension_init_hook) == 4,
       "replace only the original optional-init NOP")
ASSERT(SIZEOF(.extension_unused_marker) == 8,
       "replace only the native first-unused pointer immediate")
ASSERT(extension_code_begin >= native_unused_begin,
       "extension overlaps native allocated storage or system stack")
ASSERT(extension_code_begin >= native_bank_begin &&
       extension_reserved_end <= native_bank_end,
       "extension exceeds the native CPU private bank")
ASSERT((extension_code_begin & 3) == 0 &&
       (extension_state_begin & 3) == 0,
       "extension code and state must be word aligned")
ASSERT(__extension_code_end <= extension_state_begin,
       "extension code exceeds its explicit allocation")
ASSERT(__extension_state_end <= extension_reserved_end,
       "extension state exceeds its explicit allocation")
