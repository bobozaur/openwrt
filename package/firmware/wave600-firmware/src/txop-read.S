/* SPDX-License-Identifier: GPL-2.0-only */
/* WAVE600 A0 TXOP GET correction. Native eight-byte UMI ABI unchanged.
 * Root alone assembles/links these inputs. See txop-read.ld and delivery.md.
 */

.section .txop_upper_hook,"ax",@progbits
.global txop_upper_hook
txop_upper_hook:
 /* Preserve the original SID load in the existing delay slot. */
 bl.d txop_update_for_set

.section .txop_upper_guard,"ax",@progbits
.global txop_update_for_set
.type txop_update_for_set,@function
txop_update_for_set:
 /* Existing64da0 frame owns r13=original persistent UMI wrapper.
  * r0=SID and r1=mode arrive from the unchanged original call setup.
  * Only GET1 skips the upper state write. Other operations retain the
  * existing lower handler's SET choice; no new completion is generated.
  */
 ldb_s r2,[r13,0x15]
 breq r2,1,.Lget
 b upper_mode_write
.Lget:
 j_s [blink]
.size txop_update_for_set,.-txop_update_for_set

.section .txop_lower_dispatch,"ax",@progbits
.global txop_lower_dispatch
txop_lower_dispatch:
 /* Preserve the original16-bit SID narrowing for both operations.
  * The no-delay branch leaves the tag exclusive to the GET path. The
  * persistent wrapper and its on-wire SID are never changed.
  */
 ldh_s r0,[r0,0x10]
 ldb_s r1,[r13,0x15]
 /* GNU ARC requires a same-section target for this short compare branch. */
 brne r1,1,.Lnative_set
 bset_s r0,r0,15
.set .Lnative_set,txop_lower_dispatch + 0x12

.section .txop_lower_get,"ax",@progbits
.global txop_lower_get
.type txop_lower_get,@function
txop_lower_get:
 /* Legal native SID domain is0..255 plus ALL511. Untagged individual
  * callers retain effective-mode semantics. Untagged ALL retains the
  * original slot0/saved-mode semantics. Only the UMI GET dispatch tags
  * bit15 to select the actual requested SID's saved mode when overridden.
  *
  * r2 is live across the native10aa28/15a7b8 call, so it is preserved.
  * r3 is dead in all six direct callers; there is no literal function
  * pointer to either original entry. r0/r1 are original scratch outputs.
  */
 mov_s r1,0x349f
 extb_s r3,r0
 bbit1 r0,8,.Lselected
 mpyuw r3,r3,0x154
 add_s r1,r1,r3
.Lselected:
 lsr_s r0,r0,8
 breq_s r0,0,.Leffective
 ldb_s r0,[r1,2]
 breq_s r0,0,.Leffective
 j_s.d [blink]
 ldb_s r0,[r1,1]
.Leffective:
 ldb_s r0,[r1]
 j_s.d [blink]
 lsr_s r0,r0,6
.size txop_lower_get,.-txop_lower_get
