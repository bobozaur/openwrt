/* SPDX-License-Identifier: GPL-2.0-only */
/* Shared exact arithmetic for the three DATA families,
 * reducing duplicated IRAM without changing their distinct bounds/PHY
 * adjustments or publication owners.
 *
 * Final DATA consumers test applied mode80. That new mode is published only
 * by the validated212 owner, after the tag/tuple. External legacy SET is
 * restricted to0..4; invalid internal/DUT selectors retain the existing
 * invalid-selector assertion before their descriptor calculation. The full
 * tag check remains at request, reset, channel and live-group ownership
 * boundaries. Repeating it at every typed DATA calculation adds no valid
 * producer and is not needed to establish the applied-mode invariant.
 */
.section .fixed_mask_value,"ax",@progbits
.global fixed_mask_value
.type fixed_mask_value,@function
fixed_mask_value:
 /* r0=actual native two-bit antenna mask. Return r0=bounded aggregate
  * request minus native chain/calibration correction, r1=chain correction
  * for the existing11b adjustment. All callee-saved registers survive.
  */
 enter_s [r13-r14,blink]
 mov_s r13,r0
 bl native_antenna_count
 add_s r0,r0,gp
 ldb r14,[r0,0xab]
 bl.d native_antenna_calibration
 mov_s r0,r13
 mov_s r12,fixed_policy
 ldb_s r1,[r12,4]
 ldb_s r2,[r12,6]
 min r1,r1,r2
 sub_s r0,r1,r0
 sub_s r0,r0,r14
 mov_s r1,r14
 leave_s [r13-r14,blink,pcl]
.size fixed_mask_value,.-fixed_mask_value
