/* SPDX-License-Identifier: GPL-2.0-only */
/* Private/unselected: exact terminal or self-contained SCD print spans
 * in already-owned power functions. No state producer, native bound,
 * error assertion, publication, or completion is retired.
 */

.section .fixed_su_log_return,"ax",@progbits
.global fixed_su_log_return
fixed_su_log_return:
 /* Original e37cc..e37f6 logged the already-computed byte, then returned
  * its low8 bits through e380c. The separate legacy-force arm e37f6 is
  * not part of this span. */
 ldb r0,[sp,3]
 leave_s [r13-r23,fp,blink,pcl]

.section .fixed_clamp_log_return,"ax",@progbits
.global fixed_clamp_log_return
fixed_clamp_log_return:
 /* ebd00..ebd14: output already stored; r13 is native changed boolean. */
 mov_s r0,r13
 leave_s [r13-r19,fp,blink,pcl]

.section .fixed_calibration_log_skip,"ax",@progbits
.global fixed_calibration_log_skip
fixed_calibration_log_skip:
 /* efc42..efc58: the print's delay slot alone initialized native r16. */
 mov_s r16,r3
 b native_calibration_log_resume

.section .fixed_peer_bounds_log_return,"ax",@progbits
.global fixed_peer_bounds_log_return
fixed_peer_bounds_log_return:
 /* ebc56..ebc84 is read-only print preparation after the actual output
  * bytes at caller+0xd/+0xe were written. The sole caller ec130 ignores
  * r0 and reloads every later argument. Keep those preceding writes. */
 leave_s [r13,blink,pcl]

.section .fixed_group_rate_log_a,"ax",@progbits
.global fixed_group_rate_log_a
fixed_group_rate_log_a:
 /*10a8c2..10a8d4 follows the first actual descriptor write at10a8c0. */
 b native_group_rate_log_resume

.section .fixed_group_rate_log_b,"ax",@progbits
.global fixed_group_rate_log_b
fixed_group_rate_log_b:
 /*10a8fa..10a90c follows the second actual write at10a8f8. The original
  * only incoming edge10a8d2 was entirely within retired print arm A. */
 b native_group_rate_log_resume

.section .fixed_group_rate_log_c,"ax",@progbits
.global fixed_group_rate_log_c
fixed_group_rate_log_c:
 /*10a930..10a948 follows the third actual write at10a92e. The original
  * edge10a90a was entirely within retired print arm B. The common print
  * return and all three arms converge on the untouched10a948 writer. */
 b native_group_rate_log_resume

.section .fixed_group_rate_log_d,"ax",@progbits
.global fixed_group_rate_log_d
fixed_group_rate_log_d:
 /*10a986..10a998 reads/logs the result of native10b098. Rate-mask and
  * final descriptor stores from10a998 remain unchanged. */
 b native_group_rate_tail_resume

.section .fixed_feedback_log_skip,"ax",@progbits
.global fixed_feedback_log_skip
fixed_feedback_log_skip:
 /*10281a..10282a follows the final DATA byte store at102818. SCD6115
  * alone is retired; following PHY conversion and TCR writers remain. */
 b native_feedback_log_resume

.section .fixed_lower_version,"a",@progbits
.global fixed_lower_version
fixed_lower_version:
 /* Same native version-text framing, confined to its original171B
  * allocation. Feature admission is the actual complete loaded spans. */
 .asciz "@@@ VERSION INFO @@@ version number: F1@@@ END @@@"
 .balign 4
