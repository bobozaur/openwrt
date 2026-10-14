/* SPDX-License-Identifier: GPL-2.0-only */
/* Private, incomplete/unselected paired-firmware source.
 *
 * New policy storage is the EXISTING ten-byte FixedPower object. Boot clears
 * that object in each native lower-MAC BSS initialization. The new tag and
 * modes have no OEM meaning; only this paired ABI can create them.
 *
 * +0 declared policy flags (bit0 includes11b); +1 applied status;
 * +2 u16 new tag5846;
 * +4 APPLIED aggregate half-dBm; +5 APPLIED mode80 or native automatic0;
 * +6 APPLIED aggregate ceiling; +7 PENDING flags; +8 PENDING target;
 * +9 PENDING operation80/81 (zero means none). Byte1 holds the applied
 * result. A stage never alters the applied tuple. Native212 changes it
 * while the existing normal-channel owner fences transmission, refreshes
 * all DATA writers, then clears pending before completing that owner.
 * Management adaptation remains enabled. DATA enforcement is at the final
 * existing writers; this path never calls the legacy algorithm-disable loop.
 */

.equ FIXED_ABI, 1
.equ FIXED_MAGIC, 0xa5
.equ FIXED_TAG, 0x5846
.equ FIXED_STAGE, 0x80
.equ FIXED_DISABLE, 0x81
.equ FIXED_QUERY, 0x82
.equ FIXED_REPLAY, 0x83
.equ FIXED_OK, 0
.equ FIXED_BAD_REQUEST, 1
.equ FIXED_CONFLICT, 2
.equ FIXED_UNREPRESENTABLE, 3
.equ FIXED_NOT_READY, 4

.section .fixed_lower_entry,"ax",@progbits
.global fixed_lower_entry
fixed_lower_entry:
 b fixed_lower_dispatch
 nop_s

.section .fixed_lower_dispatch,"ax",@progbits
.global fixed_lower_dispatch
.type fixed_lower_dispatch,@function
fixed_lower_dispatch:
 ldb_s r1,[r0,5]
 breq r1,0,.Llower_legacy
 b fixed_lower_stage
.Llower_legacy:
 /* Original first six bytes, then exact untouched legacy/DUT owner. */
 enter_s [r13-r16,blink]
 ldh.as r15,[gp,0x5f]
 b legacy_lower_fixed_resume
.size fixed_lower_dispatch,.-fixed_lower_dispatch

.section .fixed_lower_stage,"ax",@progbits
.global fixed_lower_stage
.type fixed_lower_stage,@function
fixed_lower_stage:
 enter_s [r13-r15,blink]
 mov_s r13,r0
 mov_s r14,fixed_policy
 mov_s r0,FIXED_BAD_REQUEST
 stb_s r0,[r13,0x14]
 /* This is the actual persistent host object, not the small OEM ten-byte
  * internal message. No payload length is inferred from header capacity.
  */
 ldb_s r0,[r13,0x11]
 brne r0,FIXED_MAGIC,.Lcomplete
 /* status has just been initialized to1. The full native little-endian
  * word additionally requires ABI1 and both response bytes zero. */
 ld_s r0,[r13,0x14]
 brne r0,0x101,.Lcomplete
 ldb_s r0,[r13,0x10]
 brhs r0,2,.Lcomplete
 ldb_s r15,[r13,0x13]
 breq r15,FIXED_QUERY,.Lzero_power_query
 breq r15,FIXED_DISABLE,.Lzero_power_disable
 breq r15,FIXED_STAGE,.Lpower_range
 brne r15,FIXED_REPLAY,.Lcomplete
.Lpower_range:
 ldb_s r0,[r13,0x12]
 sub_s r0,r0,12
 brhs r0,52,.Lcomplete
 b_s .Lowner
.Lzero_power_query:
 ldb_s r0,[r13,0x12]
 brne r0,0,.Lcomplete
 b_s .Lquery
.Lzero_power_disable:
 ldb_s r0,[r13,0x12]
 brne r0,0,.Lcomplete
.Lowner:
 /* A standard request cannot silently replace an active legacy/DUT
  * FixedPower policy. The existing zero policy or our exact tag are the
  * only admitted state owners.
  */
 ldb_s r0,[r14,5]
 breq r0,0,.Lstage
 ldh_s r1,[r14,2]
 brne r1,FIXED_TAG,.Lconflict
 brne r0,FIXED_STAGE,.Lconflict
.Lstage:
 /* Preserve the complete applied tuple, including on update/disable.
  * Only the separate pending operation is published here. Consequently
  * DATA writers keep the old acknowledged policy until native212 owns
  * the transmission fence and publishes the new calibrated tuple.
  */
 ldb_s r0,[r14,9]
 brne r0,0,.Lconflict
 ldb_s r0,[r13,0x10]
 stb_s r0,[r14,7]
 mov_s r0,FIXED_TAG
 sth_s r0,[r14,2]
 /* Copy requested+validated operation together, publishing pending last. */
 ldh_s r0,[r13,0x12]
 sth_s r0,[r14,8]
 mov_s r0,FIXED_OK
 stb_s r0,[r13,0x14]
 b_s .Lcomplete
.Lconflict:
 mov_s r0,FIXED_CONFLICT
 stb_s r0,[r13,0x14]
 b_s .Lcomplete
.Lquery:
 /* The bounded validation owner only reads current native calibration,
  * regulatory tables and the new applied byte. It does not refresh/write.
  * Return r0=status,r1=active,r2=requested,r3=current admissible ceiling.
  */
 bl fixed_policy_query
 stb r4,[r13,0x10]
 stb_s r0,[r13,0x14]
 stb_s r1,[r13,0x16]
 stb_s r2,[r13,0x12]
 stb_s r3,[r13,0x17]
.Lcomplete:
 b fixed_lower_complete
.size fixed_lower_stage,.-fixed_lower_stage

.section .fixed_lower_complete,"ax",@progbits
.global fixed_lower_complete
fixed_lower_complete:
 mov_s r2,r13
 ldb r4,[r13,0xc]
 mov_s r0,0xf7
 mov_s r1,7
 bl.d lower_message_send
 mov_s r3,0
 leave_s [r13-r15,blink,pcl]

.section .fixed_reset_guard,"ax",@progbits
.global fixed_reset_guard
.type fixed_reset_guard,@function
fixed_reset_guard:
 /* Replaces only the existing memset call in peer reset101028. Other
  * peer initialization and all six adaptation-enable calls are unchanged.
  * Both an active policy and a request awaiting channel completion survive
  * a peer reset. Full firmware boot still clears this native object.
  */
 ldh_s r3,[r0,2]
 brne r3,FIXED_TAG,.Lreset_legacy
 ldb_s r3,[r0,5]
 breq r3,FIXED_STAGE,.Lreset_return
 ldb_s r3,[r0,9]
 sub r3,r3,FIXED_STAGE
 /*80/81/83 and native212's84 all own this object;82 is never pending. */
 brhs r3,5,.Lreset_legacy
 brne r3,2,.Lreset_return
.Lreset_legacy:
 b lower_memset
.Lreset_return:
 j_s [blink]
.size fixed_reset_guard,.-fixed_reset_guard

.section .fixed_recalc_gate,"ax",@progbits
.global fixed_recalc_gate
.type fixed_recalc_gate,@function
fixed_recalc_gate:
 /* Replaces only `ldb r0,[r13,0x26]` in1026b0. No other live register may
  * change. Its original test/branch remains at the original next address.
  * Direct feedback updates must not bypass the same final DATA calculator.
  */
 ldb r0,[r13,0x23]
 brne r0,2,.Loriginal_skip
 ldb r0,[fixed_policy+5]
 breq r0,FIXED_STAGE,.Lforce_recalculate
.Loriginal_skip:
 j_s.d [blink]
 ldb r0,[r13,0x26]
.Lforce_recalculate:
 j_s.d [blink]
 mov_s r0,0
.size fixed_recalc_gate,.-fixed_recalc_gate
