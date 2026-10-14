/* SPDX-License-Identifier: GPL-2.0-only */
/* Private, unselected source. Exact lower symbols are linked separately
 * for both A0 copies. No hardware register/descriptor addresses are new.
 * The linked image and all entry hooks remain unfinished at this stage.
 */

.equ FIXED_TAG, 0x5846
.equ FIXED_STAGE, 0x80
.equ FIXED_DISABLE, 0x81

.section .fixed_cap,"ax",@progbits
.global fixed_current_cap
.type fixed_current_cap,@function
fixed_current_cap:
 /* r0=declared policy flags; result r0=-1 for an inadmissible representation
  * domain, else a common aggregate ceiling12..63 half-dBm. This intentionally
  * uses the conservative UEVM calibration anchor. The exact admitted PSD
  * has nonnegative rate offsets/zero EVM backoff and zero external floors.
  * No adaptive per-peer3474 floor participates in FIXED.
  */
 enter_s [r13-r16,blink]
 mov_s r16,r0
 bl native_radio_antenna_mask
 bl native_antenna_count
 sub_s r0,r0,1
 brhs r0,2,.Lcap_invalid
 ldb r15,[native_max_width]
 brhs r15,4,.Lcap_invalid
 mov_s r13,0
 mov_s r14,63
.Lcap_width:
 add r0,gp,r13
 ldb.x r1,[r0,0xb0]
 brne r1,0,.Lcap_invalid
 add r0,r13,native_normal_limit
 ldb.x r1,[r0]
 min r14,r14,r1
 ldb.x r1,[r0,8]             /* same BF table */
 min r14,r14,r1
 ldb.x r1,[r0,4]             /* same MU table */
 min r14,r14,r1
 ldb.x r0,[native_uevm_limit,r13]
 min r14,r14,r0
 add_s r13,r13,1
 brhs r15,r13,.Lcap_width
 breq r16,0,.Lcap_finish
 ldb.x r0,[native_11b_limit]
 min r14,r14,r0
.Lcap_finish:
 /* Native DL-group clamps use unsigned bytes. This first ABI therefore
  * rejects an aggregate domain below the worst admitted two-chain plus
  * calibration correction12, rather than assuming a signed group format.
  */
 brlt r14,12,.Lcap_invalid
 mov_s r0,r14
 leave_s [r13-r16,blink,pcl]
.Lcap_invalid:
 mov r0,-1
 leave_s [r13-r16,blink,pcl]
.size fixed_current_cap,.-fixed_current_cap

.section .fixed_query,"ax",@progbits
.global fixed_policy_query
.type fixed_policy_query,@function
fixed_policy_query:
 /* The response is configured state from the actual lower owner. Applied
  * is published only after native212 and both active group copies complete.
  * Return status,active,requested,admissible in r0..r3; no state write.
  */
 mov_s r12,fixed_policy
 ldh_s r2,[r12,2]
 brne r2,FIXED_TAG,.Lquery_native
 ldb_s r0,[r12,9]
 brne r0,0,.Lquery_pending
 ldb_s r0,[r12,1]
 brne r0,0,.Lquery_error
 ldb_s r2,[r12,5]
 breq r2,0,.Lquery_error
 brne r2,FIXED_STAGE,.Lquery_pending
 mov_s r1,1
 ldb r4,[r12]
 ldb_s r2,[r12,4]
 j_s.d [blink]
 ldb_s r3,[r12,6]
.Lquery_native:
 ldb_s r0,[r12,5]
 breq r0,0,.Lquery_error
 mov_s r0,2
 b_s .Lquery_error
.Lquery_pending:
 mov_s r0,4
.Lquery_error:
 b fixed_query_error
.size fixed_policy_query,.-fixed_policy_query

.section .fixed_query_error,"ax",@progbits
.global fixed_query_error
fixed_query_error:
 mov_s r1,0
 mov_s r3,0
 mov_s r4,0
 j_s.d [blink]
 mov_s r2,0

.section .fixed_su_entry,"ax",@progbits
.global fixed_su_entry
fixed_su_entry:
 b fixed_su_dispatch

.section .fixed_su_dispatch,"ax",@progbits
.global fixed_su_dispatch
.type fixed_su_dispatch,@function
fixed_su_dispatch:
 /* Preserve every argument for the unmodified automatic/management path. */
 brne r4,2,.Lsu_native
 b fixed_su_mode
.Lsu_native:
.global fixed_su_native
fixed_su_native:
 enter_s [r13-r23,fp,blink]
 sub_s sp,sp,4
 b native_su_resume
.size fixed_su_dispatch,.-fixed_su_dispatch

.section .fixed_su_mode,"ax",@progbits
.global fixed_su_mode
fixed_su_mode:
 ldb r12,[fixed_policy+5]
 cmp r12,FIXED_STAGE
 bne_s .Lsu_mode_native
 b fixed_su_value
.Lsu_mode_native:
 b fixed_su_native

.section .fixed_su_value,"ax",@progbits
.global fixed_su_value
.type fixed_su_value,@function
fixed_su_value:
 /* Same inputs as e3748. The peer/rate/actual-mask identities already belong
  * to the native caller. The applied common aggregate cap is no greater
  * than normal/BF/MU/UEVM for EVERY current width. Subtracting the same
  * mask correction therefore already satisfies each native per-width
  * upper bound; no preceding peer's scratch or adaptive floor is read.
  */
 enter_s [r13-r18,blink]
 mov_s r13,r0                 /* SID */
 mov_s r15,r2                 /* native rate index */
 bl.d fixed_mask_value
 mov_s r0,r5                  /* actual two-bit-per-antenna mask */
 mov_s r18,r0
 mov_s r17,r1                 /* native chain-count correction */

 /* The native11b calculator adds the chain-count term back. Preserve that
  * PHY-specific encoding and native11b upper bound; do not treat its TCR
  * byte as the non-B per-chain encoding.
  */
 mpyuw r0,r13,0x154
 add r0,r0,0x3390
 bl native_sta_rate_table
 mpyuw r1,r15,0x1c
 ldb r0,[r0,r1]
 brne r0,1,.Lsu_result
 add r18,r18,r17
.Lsu_result:
 /* Admission of the exact two-chain/zero-floor PSD and nonnegative common
  * ceiling bounds this result at zero without an adaptive upward clamp.
  * An unexpected negative calibration domain is never admitted by212.
  */
 extb r0,r18
 leave_s [r13-r18,blink,pcl]
.size fixed_su_value,.-fixed_su_value
