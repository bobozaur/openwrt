/* SPDX-License-Identifier: GPL-2.0-only */
/* Private, unselected source for the exact A0 OEM image/FW3 derivative.
 * This file owns only the original-message transport. It is not an
 * installable candidate without both lower policy owners and the host.
 *
 * The original 0x047c entry is 0x4f830. Its displaced two instructions are
 * reproduced in .Llegacy; all bytes at 0x4f834..0x4f888 remain untouched.
 * The 0xf7 handler was exactly j_s [blink]; nop_s at 0x62eb4..0x62eb8.
 * Existing nonpersistent DUT/legacy 0x20f completions retain that no-op.
 *
 * No pending-message global is added. 4e69c/f1378 retain the actual native
 * persistent host object; its +0xe word remains the native slot capacity.
 */

.section .fixed_umac_entry,"ax",@progbits
.global fixed_umac_entry
fixed_umac_entry:
 b fixed_umac_dispatch

.section .fixed_umac_done_entry,"ax",@progbits
.global fixed_umac_done_entry
fixed_umac_done_entry:
 b fixed_umac_done

.section .fixed_umac_version,"a",@progbits
/* All original references keep the same string address. Only the string's
 * own 171-byte allocation is reused; no following padding/data is claimed.
 */
.global fixed_umac_version
fixed_umac_version:
 .asciz "@@@ VERSION INFO @@@ version number: AX50-DATA-FIX1@@@ END @@@"
 .balign 4

.section .fixed_umac_transport,"ax",@progbits
.global fixed_umac_dispatch
.type fixed_umac_dispatch,@function
fixed_umac_dispatch:
 ldb r12,[r0,0x13]
 bbit0 r12,7,.Llegacy
 /* As for the existing 044f owner, route using the original native VAP,
  * not a new payload byte. The lower owner validates the
  * complete new eight-byte body before publishing any policy state.
  */
 mov_s r2,r0
 mov_s r1,request_counter
 ldh_s r0,[r1]
 add_s r0,r0,1
 sth_s r0,[r1]
 ldb r4,[r2,0xc]
 mov r0,0x20f
 mov_s r1,3
 b.d upper_message_send
 mov_s r3,0
.Llegacy:
 enter_s [r13,blink]
 mov_s r13,r0
 b legacy_fixed_resume
.size fixed_umac_dispatch,.-fixed_umac_dispatch

.balign 4
.global fixed_umac_done
.type fixed_umac_done,@function
fixed_umac_done:
 /* All old f7 producers allocate nonpersistent internal messages. The new
  * typed lower branch is the only persistent f7 producer. In particular,
  * this does not try to turn an empty legacy completion into a host CFM.
  */
 ldb_s r1,[r0,5]
 breq r1,0,.Lold_done
 mov_s r1,r0
 mov_s r2,confirmation_counter
 ldh_s r0,[r2]
 add_s r0,r0,1
 sth_s r0,[r2]
 mov_s r0,0x147c
 b upper_confirmation
.Lold_done:
 j_s [blink]
.size fixed_umac_done,.-fixed_umac_done
