/* SPDX-License-Identifier: GPL-2.0-only */
native_bank_begin = 0x130000;
native_bank_end = 0x180000;
native_unused_begin = 0x175350;
native_user_init = 0x15ca58;
native_unused_publication = 0x1304cc;
extension_code_begin = 0x176000;
extension_state_begin = 0x17c000;
extension_reserved_end = 0x17e000;
native_twt_limit_site = 0x151ab0;
native_twt_limit_acquire = 0x137a84;
native_twt_limit_release = 0x137a44;
native_twt_limit_assert = 0x135f14;
native_twt_limit_peer_resume = 0x14b014;
INCLUDE twt-limits.ld
SECTIONS
{
 .twt_limit_peer_hook 0x14afd8 : { *(.twt_limit_peer_hook) }
}
ASSERT(SIZEOF(.twt_limit_peer_hook) == SIZEOF(.twt_limit_policy_hook),
       "lower policy and station construction hooks must be paired")
INCLUDE extensions.ld
