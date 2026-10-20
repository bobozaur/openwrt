/* SPDX-License-Identifier: GPL-2.0-only */
native_bank_begin = 0xe0000;
native_bank_end = 0x130000;
native_unused_begin = 0x1256e0;
native_user_init = 0x10cde4;
native_unused_publication = 0xe04cc;
extension_code_begin = 0x126000;
extension_state_begin = 0x12c000;
extension_reserved_end = 0x12e000;
INCLUDE extensions.ld
