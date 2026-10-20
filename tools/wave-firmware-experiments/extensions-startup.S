/* SPDX-License-Identifier: GPL-2.0-only */
/* Application-owned storage in the exact A0 CPU banks.
 *
 * Each native low-level initializer publishes its first unused address to
 * ThreadX. The existing application initializer discards that argument and
 * creates its queues, threads and message pool from fixed storage instead.
 * The paired linker scripts reserve an explicit part of that unused area;
 * neither the native BSS bounds nor any stack limit is moved.
 *
 * The original optional initialization hook runs before SP and GP are set.
 * Its replacement therefore uses only r0-r2 and no calls or stack accesses.
 * The following native CRT entry overwrites all three before using them.
 * Clear extension state on CPU startup, including startup without a new
 * host file load. Do not rely only on zero bytes in the downloaded image.
 */

.section .extension_init_hook,"ax",@progbits
.global extension_init_hook
extension_init_hook:
 b extension_boot_init

.section .extension_unused_marker,"ax",@progbits
.global extension_unused_marker
extension_unused_marker:
 /* Only the separately published first-unused pointer changes. */
 mov r0,extension_reserved_end

.section .extension_boot,"ax",@progbits
.balign 4
.global extension_boot_init
.type extension_boot_init,@function
extension_boot_init:
 mov r0,__extension_state_start
 mov r1,__extension_state_end
 mov_s r2,0
 b_s .Lextension_state_check
.Lextension_state_word:
 st.ab r2,[r0,4]
.Lextension_state_check:
 brlo r0,r1,.Lextension_state_word
 j_s [blink]
.size extension_boot_init,.-extension_boot_init
