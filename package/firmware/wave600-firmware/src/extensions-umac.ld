/* SPDX-License-Identifier: GPL-2.0-only */
/* Upper-MAC extension storage. The native CRT stack and BSS stay unchanged;
 * only its separately published first-unused address advances. This layout
 * is linked with the consuming upper-MAC extension before image admission.
 */
native_bank_begin = 0x40000;
native_bank_end = 0xe0000;
native_unused_begin = 0xbc868;
native_user_init = 0x65308;
native_unused_publication = 0x40338;
extension_code_begin = 0xbd000;
extension_state_begin = 0xc8000;
extension_reserved_end = 0xd8000;
native_tsf32 = 0x489d4;
native_critical_enter = 0x4796c;
native_critical_leave = 0x47a78;
native_twt_pd_allocate = 0x49118;
native_twt_pd_release = 0x4930c;
native_twt_request_delivered = 0x493fc;
native_twt_request_cancel = 0x4942c;
native_twt_message_allocate = 0x4e428;
native_twt_message_send = 0x4e69c;
native_twt_management_submit = 0x4f118;
native_twt_management_return = 0x62054;
native_twt_rx_dispatch = 0x49d58;
native_twt_dispatch = 0x6395c;
native_twt_strip_protection = 0x5de7c;
native_twt_rx_return = 0x476d8;
native_twt_classify_accept = 0x5cbda;
native_twt_classify_resume = 0x5cbd2;
native_twt_action_resume = 0x5d8d8;
native_twt_peer_add = 0x5ca90;
native_twt_peer_remove = 0x5e118;
native_twt_vap_band = 0x427e4;
native_twt_assert = 0x44094;
native_twt_fixed_dispatch = 0x66d88;
native_twt_limit_site = 0x4baa8;
native_twt_limit_acquire = 0x477e8;
native_twt_limit_release = 0x477a8;
native_twt_limit_assert = 0x44094;
native_twt_shared_start = 0x6374c;
INCLUDE twt-limits.ld

SECTIONS
{
 .twt_classifier_hook 0x5cbcc : { *(.twt_classifier_hook) }
 .twt_action_hook 0x5d8d4 : { *(.twt_action_hook) }
 .twt_control_hook 0x4f830 : { *(.twt_control_hook) }
 .twt_peer_add_hook 0x66b80 : { *(.twt_peer_add_hook) }
 .twt_peer_remove_hook 0x66b90 : { *(.twt_peer_remove_hook) }
 .twt_return_hooks 0x67490 : { *(.twt_return_hooks) }
 .twt_rx_task_hook 0x67ce0 : { *(.twt_rx_task_hook) }
 .twt_limits_start_hook 0x63a26 : { *(.twt_limits_start_hook) }
 .twt_limits_storage twt_limits_storage_begin (NOLOAD) : {
  . += twt_limits_storage_size;
 }
}
ASSERT(SIZEOF(.twt_classifier_hook) == 4, "native classifier branch extent")
ASSERT(SIZEOF(.twt_action_hook) == 4, "native action prologue extent")
ASSERT(SIZEOF(.twt_control_hook) == 4, "existing047c transport entry only")
ASSERT(SIZEOF(.twt_peer_add_hook) == 4, "native RX phase0 handler only")
ASSERT(SIZEOF(.twt_peer_remove_hook) == 4, "native RX phase4 handler only")
ASSERT(SIZEOF(.twt_return_hooks) == 8, "native return types13/14 only")
ASSERT(SIZEOF(.twt_rx_task_hook) == 4, "existing RX task callback only")
ASSERT(SIZEOF(.twt_limits_start_hook) == 4, "native shared startup call only")
INCLUDE extensions.ld
ASSERT(__extension_state_end <= twt_limits_storage_begin,
       "private state overlaps shared limits or its low-alias clear range")
ASSERT(twt_limits_storage_begin + twt_limits_storage_size <= extension_reserved_end,
       "shared limits exceed the reserved UMAC allocation")
