/* SPDX-License-Identifier: GPL-2.0-only */
native_bank_begin = 0xe0000;
native_bank_end = 0x130000;
native_unused_begin = 0x1256e0;
native_user_init = 0x10cde4;
native_unused_publication = 0xe04cc;
extension_code_begin = 0x126000;
extension_state_begin = 0x12c000;
extension_reserved_end = 0x12e000;
native_twt_limit_site = 0x101d04;
native_twt_limit_acquire = 0xe7bbc;
native_twt_limit_release = 0xe7b7c;
native_twt_limit_assert = 0xe5f5c;
native_twt_limit_peer_resume = 0xfb268;
INCLUDE twt-limits.ld
SECTIONS
{
 .twt_limit_peer_hook 0xfb22c : { *(.twt_limit_peer_hook) }
}
ASSERT(SIZEOF(.twt_limit_peer_hook) == SIZEOF(.twt_limit_policy_hook),
       "lower policy and station construction hooks must be paired")
INCLUDE extensions.ld
