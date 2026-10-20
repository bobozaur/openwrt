/* SPDX-License-Identifier: GPL-2.0-only */
/* The native ST_NORMAL owner and its actual212/45
 * pending-count completion remain authoritative. No direct f713c call,
 * undocumented TX register, extra pending-message global or fake hardware
 * completion is introduced.
 */
.equ FIXED_TAG, 0x5846
.equ FIXED_STAGE, 0x80
.equ FIXED_DISABLE, 0x81
.equ FIXED_REPLAY, 0x83
.equ FIXED_REFRESHING, 0x84

.section .fixed_calibration_hook,"ax",@progbits
.global fixed_calibration_hook
fixed_calibration_hook:
 /* Include the unchanged original delay slot in this six-byte hook. */
 bl.d fixed_after_calibration
 mov_s r0,r20

.section .fixed_completion_hook,"ax",@progbits
.global fixed_completion_hook
fixed_completion_hook:
 /* Include the unchanged original delay slot in this six-byte hook. */
 bl.d fixed_channel_complete
 bmsk_s r0,r0,0

.section .fixed_after_calibration,"ax",@progbits
.global fixed_after_calibration
.type fixed_after_calibration,@function
fixed_after_calibration:
 enter_s [r13-r16,blink]
 bl native_apply_calibration
 mov_s r13,fixed_policy
 ldh_s r0,[r13,2]
 brne r0,FIXED_TAG,.Lcal_done
 ldb r16,[r13,9]
 breq r16,FIXED_DISABLE,.Lcal_automatic
 breq r16,FIXED_STAGE,.Lcal_pending
 breq r16,FIXED_REPLAY,.Lcal_pending
 ldb_s r0,[r13,5]
 brne r0,FIXED_STAGE,.Lcal_done
 /* A native recalibration of an already applied policy is itself a
  * replay. Publish the in-progress byte before changing the tuple.
  */
 mov_s r0,FIXED_REFRESHING
 stb_s r0,[r13,9]
 ldb_s r14,[r13]
 ldb_s r15,[r13,4]
 b .Lcal_cap
.Lcal_pending:
 ldb_s r14,[r13,7]
 ldb_s r15,[r13,8]
.Lcal_cap:
 bl.d fixed_current_cap
 mov_s r0,r14
 brlt r0,12,.Lcal_unrepresentable
 stb_s r0,[r13,6]
 stb_s r14,[r13]
 stb_s r15,[r13,4]
 mov_s r1,0
 brne r16,FIXED_STAGE,.Lcal_publish
 brhs r0,r15,.Lcal_publish
 /* A fresh user value clipped by current calibration/regulation must not
  * receive a standard success. Apply only the bounded effective value
  * inside the TX-fenced transition, then return explicit failure through
  * QUERY. The host quiesces and recovers its unchanged last-ACK policy.
  */
 mov_s r1,3
.Lcal_publish:
 stb_s r1,[r13,1]
 mov_s r0,FIXED_STAGE
 stb_s r0,[r13,5]
 b .Lcal_done
.Lcal_unrepresentable:
 b fixed_cal_unrepresentable
.Lcal_automatic:
 b fixed_cal_automatic
.Lcal_done:
 leave_s [r13-r16,blink,pcl]
.size fixed_after_calibration,.-fixed_after_calibration

.section .fixed_cal_unrepresentable,"ax",@progbits
.global fixed_cal_unrepresentable
fixed_cal_unrepresentable:
 /* A domain not covered by the paired image/PSD admission must never
  * generate an invented signed group byte. Restore native automatic
  * calculation within this already fenced transaction, report failure,
  * and let the checked host owner quiesce/recover before user success.
  */
 mov_s r0,3
 stb_s r0,[r13,1]
 mov_s r0,0
 stb_s r0,[r13,5]
 stb_s r0,[r13,6]
 leave_s [r13-r16,blink,pcl]

.section .fixed_cal_automatic,"ax",@progbits
.global fixed_cal_automatic
fixed_cal_automatic:
 mov_s r0,0
 stb_s r0,[r13]
 stb_s r0,[r13,1]
 stb_s r0,[r13,4]
 stb_s r0,[r13,5]
 stb_s r0,[r13,6]
 leave_s [r13-r16,blink,pcl]

.section .fixed_channel_complete,"ax",@progbits
.global fixed_channel_complete
.type fixed_channel_complete,@function
fixed_channel_complete:
 enter_s [r13,blink]
 bl native_channel_tail
 bl fixed_refresh_groups
 /* All native SU/fixed-rate/VAP refreshes and both owned DL-group copies
  * and pending publication have completed. The untouched caller sends45 through
  * the real HDK pending-count/SET_CHANNEL completion and TX-release owner.
  */
 leave_s [r13,blink,pcl]
.size fixed_channel_complete,.-fixed_channel_complete
