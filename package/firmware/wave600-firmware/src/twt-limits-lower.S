/* SPDX-License-Identifier: GPL-2.0-only */
.section .twt_limit_policy_hook,"ax",@progbits
 b wave_twt_native_limit

.section .twt_limit_peer_hook,"ax",@progbits
 b twt_limit_peer

.section .twt.text,"ax",@progbits
.balign 4
.type twt_limit_peer,@function
twt_limit_peer:
 /* The native constructor has saved blink and r13-r25 already. Its final
  * nine-entry initialization loop is replaced as a whole; no native store
  * can publish an unclamped value after the replacement releases its lock.
  */
 mov r0,r22
 ldb r1,[gp,0x9c]
 bl wave_twt_limit_peer_initialize
 mov r0,r22
 b native_twt_limit_peer_resume
.size twt_limit_peer,.-twt_limit_peer
