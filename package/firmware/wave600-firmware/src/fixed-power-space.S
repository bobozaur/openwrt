/* SPDX-License-Identifier: GPL-2.0-only */
/* Private/unselected IRAM space ownership. These spans replace complete,
 * already-recovered operations; zero image padding is not used.
 */

.section .fixed_dl_dump_retire,"ax",@progbits
.global fixed_dl_dump_retire
fixed_dl_dump_retire:
 /* Native107f5c completed its last functional call107bc4 at108044.
  * 10804a..108150 contained only descriptor-dump logger calls/loops.
  */
 leave_s [r13-r17,blink,pcl]

.section .fixed_ul_dump_retire,"ax",@progbits
.global fixed_ul_dump_retire
fixed_ul_dump_retire:
 /* Native10aaa4 completed its last functional call107bc4 at10ab08.
  * 10ab0e..10ac0c contained only descriptor-dump logger calls/loops.
  */
 leave_s [r13-r18,blink,pcl]

.section .fixed_group_mgmt_copy,"ax",@progbits
.global fixed_group_mgmt_copy
fixed_group_mgmt_copy:
 /* Replace1079e0..107a74, preserving the native bound in r0 and the
  * resulting actual VAP pointer in r14. The original repeated nibble
  * copies are exactly low24 of word0 and bits28/29/31 of word+0x10.
  */
 mov_s r15,r0
 mpyuw r14,r14,0x180
 add r14,r14,0xa033b9a0
 add r0,r13,0x1c
 bl.d fixed_copy_vap_tcr_fields
 mov_s r1,r14
 mov_s r0,r15
 b native_group_mgmt_resume

.section .fixed_group_aux_copy,"ax",@progbits
.global fixed_group_aux_copy
fixed_group_aux_copy:
 /* Replace107af2..107b86. This is the same operation on the native
  * auxiliary/management TCR beginning at group+0x3c, not its DATA power.
  */
 mov_s r15,r0
 mpyuw r14,r14,0x180
 add r14,r14,0xa033b9a0
 add r0,r13,0x3c
 bl.d fixed_copy_vap_tcr_fields
 mov_s r1,r14
 mov_s r0,r15
 b native_group_aux_resume

.section .fixed_copy_vap_tcr_fields,"ax",@progbits
.global fixed_copy_vap_tcr_fields
.type fixed_copy_vap_tcr_fields,@function
fixed_copy_vap_tcr_fields:
 /* r0=actual group TCR,r1=actual VAP management TCR. Preserve both inputs.
  * Original1079e4..107a72 and107af6..107b84 perform precisely these masks,
  * repeatedly loading the same two source words. No flag outside these
  * masks, no power byte and no descriptor publication operation changes.
  */
 ld_s r2,[r0]
 ld_s r3,[r1]
 bmskn r2,r2,23
 bmsk r3,r3,23
 or_s r2,r2,r3
 st_s r2,[r0]
 ld_s r2,[r0,0x10]
 ld_s r3,[r1,0x18]
 bclr_s r2,r2,31
 bclr_s r2,r2,29
 bclr_s r2,r2,28
 and r3,r3,0xb0
 asl_s r3,r3,24
 or_s r2,r2,r3
 j_s.d [blink]
 st_s r2,[r0,0x10]
.size fixed_copy_vap_tcr_fields,.-fixed_copy_vap_tcr_fields

.section .fixed_reset_hook,"ax",@progbits
.global fixed_reset_hook
fixed_reset_hook:
 /* Native101066..10106c, including its unchanged argument delay slot. */
 bl.d fixed_reset_guard
 mov_s r2,10

.section .fixed_recalc_hook,"ax",@progbits
.global fixed_recalc_hook
fixed_recalc_hook:
 /* Native1027f2..1027f6; its original following branch is unchanged. */
 bl fixed_recalc_gate
