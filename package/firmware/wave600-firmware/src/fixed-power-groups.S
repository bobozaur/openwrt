/* SPDX-License-Identifier: GPL-2.0-only */
/* Private/unselected. The16 active slots are the actual DL DATA pool:
 * ff714's internal phase2 alone allocates/binds/publishes that pool.
 * Internal phase3 calls setUlDataPhaseParams10aaa4 without entering it;
 * its local trigger/control TCR keeps the native VAP management policy.
 */
.equ FIXED_TAG, 0x5846
.equ FIXED_STAGE, 0x80

.section .fixed_group_entry,"ax",@progbits
.global fixed_group_entry
fixed_group_entry:
 /* Replaces the SCD-only prefix1079b4 plus original1079c6..1079cc,
  * already inside native r3!=0/DL branch. Native10b098 has completed. */
 b fixed_group_dispatch

.section .fixed_group_dispatch,"ax",@progbits
.global fixed_group_dispatch
.type fixed_group_dispatch,@function
fixed_group_dispatch:
 ldb r0,[fixed_policy+5]
 cmp r0,FIXED_STAGE
 bne_s .Lgroup_original
 b fixed_group_active
.Lgroup_original:
 b fixed_group_native
.size fixed_group_dispatch,.-fixed_group_dispatch

.section .fixed_group_active,"ax",@progbits
.global fixed_group_active
fixed_group_active:
 bl.d fixed_group_value
 mov_s r0,r13
 stb r0,[r13,0x20]
 leave_s [r13-r16,fp,blink,pcl]

.section .fixed_group_native,"ax",@progbits
.global fixed_group_native
fixed_group_native:
 ld_s r0,[r13,0x20]
 xbfu r0,r0,0x2a
 b native_group_resume

.section .fixed_group_value,"ax",@progbits
.global fixed_group_value
.type fixed_group_value,@function
fixed_group_value:
 /* r0 is an owned DL group. Native width/bit9 select the same real limits
  * as107930. With standard FIXED off, restore the exact native seed/clamp
  * calculation used by a fresh automatic group; do not retain the old
  * fixed byte as the input to min() when disabling or increasing policy.
  */
 enter_s [r13-r16,blink]
 mov_s r13,r0
 ldb r0,[fixed_policy+5]
 breq r0,FIXED_STAGE,.Lgroup_fixed
 ld_s r0,[r13,0x20]
 xbfu r14,r0,0x2a
 ld_s r0,[r13,0x14]
 bbit1 r0,9,.Lgroup_mu_bound
 bl.d native_group_normal_bound
 mov_s r0,r14
 b .Lgroup_bound_ready
.Lgroup_mu_bound:
 bl.d native_group_mu_bound
 mov_s r0,r14
.Lgroup_bound_ready:
 mov_s r15,r0
 /* Preserve native unsigned group encoding on the automatic return. */
 bl.d native_group_calibration_seed
 mov_s r0,r14
 extb_s r0,r0
 extb_s r15,r15
 min r0,r0,r15
 leave_s [r13-r16,blink,pcl]
.Lgroup_fixed:
 /* Same native full mask as the group calibration. The conservative
  * aggregate cap includes both normal and MU ceilings before subtracting
  * this mask's correction, so the native unsigned bound is already met. */
 bl native_radio_antenna_mask
 bl fixed_mask_value
 extb_s r0,r0
 leave_s [r13-r16,blink,pcl]
.size fixed_group_value,.-fixed_group_value

.section .fixed_group_refresh,"ax",@progbits
.global fixed_refresh_groups
.type fixed_refresh_groups,@function
fixed_refresh_groups:
 /* Called only by the native212 completion wrapper while the existing
  * channel process owns TX exclusion. Exactly16 slots, live state2 and
  * the native stored pointer join are required. No complemented32-bit
  * allocation bitmap and no new PROT_DB protocol are invented.
  */
 enter_s [r13-r17,blink]
 mov_s r12,fixed_policy
 ldh_s r0,[r12,2]
 brne r0,FIXED_TAG,.Lgroups_done
 ldb_s r0,[r12,5]
 breq r0,FIXED_STAGE,.Lgroups_owned
 ldb_s r0,[r12,9]
 breq r0,0,.Lgroups_done
.Lgroups_owned:
 mov_s r13,0
 mov r14,native_group_slot_state
 mov r15,native_group_slot_pointer
.Lgroup_slot:
 ldb_s r0,[r14]
 brne r0,2,.Lnext_group
 ld r16,[r15]
 ld r17,[r16,0x18]
 bl.d fixed_group_value
 mov_s r0,r16
 /* Native creation copies the whole group into the paired record. Here
  * only its existing low power byte is changed; mutable rate/user fields
  * and enable/back-pointer ownership remain intact in both records.
  * Native ff714 writes the pointer, full paired copy and back pointer
  * before publishing state2; removal clears state before returning its
  * allocation. The same serialized, TX-fenced owner therefore supplies
  * the current pair without a new speculative descriptor/fatal predicate.
  */
 stb r0,[r16,0x20]
 stb r0,[r17,0x20]
.Lnext_group:
 add_s r13,r13,1
 add r14,r14,0x16c4
 add r15,r15,0x16c4
 brlo r13,16,.Lgroup_slot
 /* This helper has one caller, the212 wrapper after its native tail.
  * Clear pending only here, after the last paired-group store. Invalid
  * tags branch directly to .Lgroups_done; an inactive/zero-pending tuple
  * also bypasses this no-op publication. */
 mov_s r0,0
 stb r0,[fixed_policy+9]
.Lgroups_done:
 leave_s [r13-r17,blink,pcl]
.size fixed_refresh_groups,.-fixed_refresh_groups
