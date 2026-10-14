/* SPDX-License-Identifier: GPL-2.0-only */

.section .twt_classifier_hook,"ax",@progbits
 b twt_classify

.section .twt_action_hook,"ax",@progbits
 b twt_action

.section .twt_control_hook,"ax",@progbits
 b twt_control

.section .twt_peer_add_hook,"a",@progbits
 .word wave_twt_peer_added

.section .twt_peer_remove_hook,"a",@progbits
 .word wave_twt_peer_removed

.section .twt_return_hooks,"a",@progbits
 .word wave_twt_management_return
 .word wave_twt_management_return

.section .twt_rx_task_hook,"a",@progbits
 .word wave_twt_rx_dispatch

.section .twt_limit_policy_hook,"ax",@progbits
 b wave_twt_native_limit

.section .twt_limits_start_hook,"ax",@progbits
 /* Preserve the original call's existing native delay-slot store. */
 bl.d wave_twt_limits_start

.section .twt.text,"ax",@progbits
.balign 4
.type twt_classify,@function
twt_classify:
 /* Both existing callers retain the original category separately. The
  * result selects only admission policy: index16 already supplies the
  * same associated/non-robust pair used by the reference's TWT index17.
  * Preserve r2 and the original category126/127 results17/18.
  */
 mov_s r1,r0
 breq r0,22,.Ltwt_category
 brlo r0,12,.Ltwt_accept_category
 b native_twt_classify_resume
.Ltwt_accept_category:
 b native_twt_classify_accept
.Ltwt_category:
 mov_s r0,16
 j_s [blink]
.size twt_classify,.-twt_classify

.balign 4
.type twt_action,@function
twt_action:
 brne r2,22,.Ltwt_native_action
 b wave_twt_management_receive
.Ltwt_native_action:
 /* Exactly the two displaced native instructions. */
 enter_s [r13-r17,blink]
 mov_s r14,r1
 b native_twt_action_resume
.size twt_action,.-twt_action

.balign 4
.type twt_control,@function
twt_control:
 /* New typed eight-byte body. Legacy and admitted FIXED/RTS requests
  * retain the already installed original-message transport.
  */
 ldb_s r1,[r0,5]
 breq r1,0,.Ltwt_fixed_dispatch
 ldb r1,[r0,0x12]
 brne r1,0xa7,.Ltwt_fixed_dispatch
 b wave_twt_control_receive
.Ltwt_fixed_dispatch:
 b native_twt_fixed_dispatch
.size twt_control,.-twt_control
