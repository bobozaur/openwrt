/* SPDX-License-Identifier: GPL-2.0-only */
SECTIONS
{
  . = 0x63d0c;
  .text : { *(.text) }
  /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
}
wave_message_send = 0x4e69c;
ASSERT(SIZEOF(.text) == 0x1c, "ADD_STREAM replacement must fit its28-byte handler")
