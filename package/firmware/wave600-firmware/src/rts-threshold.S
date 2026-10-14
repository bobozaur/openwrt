/* SPDX-License-Identifier: GPL-2.0-only */
/* Paired A0 byte-threshold policy.
 *
 * The native protection writers own two two-bit methods together, and the
 * aggregation constructor owns both low20 length fields together. Explicit
 * byte policy selects SIZE for that pair; it replaces adaptive duration
 * selection while enabled. NONE becomes THRESHOLD, other native methods
 * (including FORCE) and the separate forced-VAP bitmaps remain native.
 *
 * Keep the unmodified latest native method in a byte per STA/VHT group.
 * Constructors replace that shadow before publishing the new incarnation;
 * native active-state predicates exclude retired owners. First enable takes
 * a snapshot, updates never snapshot our effective hardware overlay, and
 * disable restores current live owners from the native shadows.
 *
 * STAGE changes only pending state. The existing212 transaction calls the
 * commit after its original calibration/fixed-power tail, before native45
 * completion releases TX. Native task3 serializes events208/210/212/226/227,
 * including complete STA/VAP/VHT construction/removal and this commit.
 * Local interrupt exclusion also serializes the final rate-path writers'
 * shadow+hardware publication against the bounded commit walk.
 * Full CPU startup clears this separately allocated state; ordinary channel
 * transitions reapply it and host recovery replays only acknowledged state.
 */

.equ RTS_MAGIC, 0xa6
.equ RTS_ABI, 1
.equ RTS_STAGE, 0x90
.equ RTS_DISABLE, 0x91
.equ RTS_QUERY, 0x92
.equ RTS_MAX, 2353
.equ RTS_NATIVE_LENGTH, 11454
.equ RTS_SELECTOR, 0xa0932018
.equ RTS_LENGTH_TABLE, 0xa034b3e0
.equ RTS_STA_TABLE, 0xa032dd7c
.equ RTS_GROUP_TABLE, 0xa0310f64
.equ RTS_VHT_ACTIVE, 0x751
.equ RTS_VHT_STRIDE, 0x2f0

/* State is local to the lower CPU, not the existing FixedPower object.
 * +0 applied boolean, +1 pending operation, +2 applied threshold(u16),
 * +4 pending threshold(u16), +6 original paired selector, +7 reserved.
 */
.section .rts.state,"aw",@nobits
.balign 4
.global rts_policy
rts_policy:
 .space 8
rts_sta_native:
 .space 256
rts_group_native:
 .space 16

.section .rts_dispatch_hook,"a",@progbits
 .word rts_lower_dispatch

.section .rts_completion_hook,"ax",@progbits
 bl rts_channel_commit
 nop_s

.section .rts_vap_hook,"ax",@progbits
 bl rts_constructor_length
 nop_s

.section .rts_sta_hook,"ax",@progbits
 b rts_sta_method
 nop_s

.section .rts_group_hook,"ax",@progbits
 b rts_group_method
 nop_s

.section .rts_group_constructor_hook,"ax",@progbits
 bl rts_group_constructor
 nop_s
 nop_s

.section .rts.text,"ax",@progbits
.balign 4
.global rts_lower_dispatch
.type rts_lower_dispatch,@function
rts_lower_dispatch:
 /* The table wrapper preserves the complete admitted FIXED implementation.
  * Persistent payload byte2=A6 is disjoint from valid FIXED half-dBm12..63.
  * Legacy internal messages must never be interpreted as eight-byte bodies.
  */
 ldb_s r1,[r0,5]
 breq r1,0,.Lrts_forward_fixed
 ldb r1,[r0,0x12]
 breq r1,RTS_MAGIC,rts_lower_request
.Lrts_forward_fixed:
 b native_fixed_entry
.size rts_lower_dispatch,.-rts_lower_dispatch

.balign 4
.type rts_lower_request,@function
rts_lower_request:
 enter_s [r13-r15,blink]
 mov_s r13,r0
 mov_s r14,rts_policy
 mov_s r0,1
 stb_s r0,[r13,0x14]
 /* ABI1, zero input active/pending bytes; status is the initialized1. */
 ld_s r0,[r13,0x14]
 brne r0,0x101,.Lrts_reply
 ldb_s r15,[r13,0x13]
 breq r15,RTS_QUERY,.Lrts_query
 breq r15,RTS_DISABLE,.Lrts_disable_request
 brne r15,RTS_STAGE,.Lrts_reply
 ldh_s r0,[r13,0x10]
 brhi r0,RTS_MAX,.Lrts_reply
 b_s .Lrts_stage
.Lrts_disable_request:
 ldh_s r0,[r13,0x10]
 brne r0,0,.Lrts_reply
.Lrts_stage:
 ldb_s r1,[r14,1]
 brne r1,0,.Lrts_conflict
 sth_s r0,[r14,4]
 /* Publish pending last. Applied writers retain their acknowledged tuple. */
 stb_s r15,[r14,1]
 mov_s r0,0
 stb_s r0,[r13,0x14]
 b_s .Lrts_reply
.Lrts_conflict:
 mov_s r0,2
 stb_s r0,[r13,0x14]
 b_s .Lrts_reply
.Lrts_query:
 ldh_s r0,[r13,0x10]
 brne r0,0,.Lrts_reply
 ldh_s r0,[r14,2]
 sth_s r0,[r13,0x10]
 ldb_s r0,[r14]
 stb_s r0,[r13,0x16]
 ldb_s r0,[r14,1]
 stb_s r0,[r13,0x17]
 mov_s r1,0
 breq r0,0,.Lrts_query_status
 mov_s r1,4
.Lrts_query_status:
 stb_s r1,[r13,0x14]
.Lrts_reply:
 mov_s r2,r13
 ldb r4,[r13,0xc]
 mov_s r0,0xf7
 mov_s r1,7
 mov_s r3,0
 bl native_message_send
 leave_s [r13-r15,blink,pcl]
.size rts_lower_request,.-rts_lower_request

.balign 4
.global rts_constructor_length
.type rts_constructor_length,@function
rts_constructor_length:
 /* The replaced instruction changed only r1. r2/r3 are the live original
  * DWORD pair and the caller also has live r0/r8/r11; leave them untouched.
  */
 ldb r1,[rts_policy]
 breq r1,0,.Lrts_default_length
 ldh r1,[rts_policy+2]
 j_s [blink]
.Lrts_default_length:
 mov r1,RTS_NATIVE_LENGTH
 j_s [blink]
.size rts_constructor_length,.-rts_constructor_length

/* Original writers change r0-r2 only. Preserve every other register, not
 * merely the normal ABI's caller-saved set: these native leaves are small.
 */
.macro RTS_METHOD name, shadow, stride, base, shift, mask
.balign 4
.global \name
.type \name,@function
\name:
 enter_s [r13-r15,blink]
 bmsk r1,r1,1
 asl r2,r1,2
 or r2,r2,r1
 mov r13,\shadow
 add r13,r13,r0
 clri r15
 stb r2,[r13]
 ldb r14,[rts_policy]
 breq r14,0,.Lnative_method\@
 brne r1,0,.Lnative_method\@
 mov_s r2,5
.Lnative_method\@:
 asl r2,r2,\shift
 mpyuw r0,r0,\stride
 add r1,r0,\base
 ld_s r0,[r1]
 and r0,r0,\mask
 or_s r0,r0,r2
 st_s r0,[r1]
 seti r15
 leave_s [r13-r15,blink,pcl]
.size \name,.-\name
.endm

RTS_METHOD rts_sta_method, rts_sta_native, 220, RTS_STA_TABLE, 20, 0xff0fffff
RTS_METHOD rts_group_method, rts_group_native, 100, RTS_GROUP_TABLE, 8, 0xfffff0ff

.balign 4
.global rts_group_constructor
.type rts_group_constructor,@function
rts_group_constructor:
 /* Native r19 is this new group's ID and r15 its hardware record. The
  * displaced code cleared only bits8..9. Save the resulting full nibble;
  * do not assume the incoming CTS field equals that zero RTS field.
  */
 enter_s [r13-r14,blink]
 ld_s r0,[r15]
 and r0,r0,0xfffffcff
 lsr r13,r0,8
 bmsk r13,r13,3
 mov r14,rts_group_native
 add r14,r14,r19
 stb r13,[r14]
 ldb r14,[rts_policy]
 breq r14,0,.Lrts_group_ctor_store
 or r0,r0,0x100
 and r13,r13,12
 brne r13,0,.Lrts_group_ctor_store
 or r0,r0,0x400
.Lrts_group_ctor_store:
 st_s r0,[r15]
 leave_s [r13-r14,blink,pcl]
.size rts_group_constructor,.-rts_group_constructor

.balign 4
.global rts_channel_commit
.type rts_channel_commit,@function
rts_channel_commit:
 enter_s [r13-r24,blink]
 mov r13,rts_policy
 ldb r14,[r13]
 ldb r15,[r13,1]
 or r0,r14,r15
 cmp r0,0
 beq .Lrts_commit_done
 /* These exact native helpers give this CPU's VAP start and count.
  * They run before local interrupt exclusion; no blocking native owner is
  * called while the bounded table publication holds that exclusion.
  */
 bl native_vap_first
 mov r16,r0
 bl native_vap_count
 add r17,r16,r0
 clri r18
 mov r19,0
 breq r15,RTS_DISABLE,.Lrts_commit_disable
 breq r15,0,.Lrts_commit_replay
 ldh r20,[r13,4]
 sth r20,[r13,2]
 brne r14,0,.Lrts_commit_enable
 /* First enable only: preserve the unmodified selector and methods. */
 ld r0,[RTS_SELECTOR]
 bmsk r0,r0,3
 stb r0,[r13,6]
 mov r19,1
.Lrts_commit_enable:
 mov r14,1
 stb r14,[r13]
.Lrts_commit_replay:
 ldh r20,[r13,2]
 ld r0,[RTS_SELECTOR]
 and r0,r0,0xfffffff0
 or r0,r0,10
 st r0,[RTS_SELECTOR]
 b .Lrts_commit_vaps
.Lrts_commit_disable:
 cmp r14,0
 beq .Lrts_commit_publish
 mov r14,0
 stb r14,[r13]
 sth r14,[r13,2]
 ld r0,[RTS_SELECTOR]
 and r0,r0,0xfffffff0
 ldb r1,[r13,6]
 or r0,r0,r1
 st r0,[RTS_SELECTOR]
 mov r20,RTS_NATIVE_LENGTH
.Lrts_commit_vaps:
 mov r21,r16
 b .Lrts_vap_check
.Lrts_vap_next:
 ldb r0,[native_vap_states,r21]
 brhs r0,4,.Lrts_vap_skip
 mpy r22,r21,12
 add r22,r22,RTS_LENGTH_TABLE
 ld r0,[r22]
 bmskn r0,r0,19
 or r0,r0,r20
 st r0,[r22]
 ld r0,[r22,4]
 bmskn r0,r0,19
 or r0,r0,r20
 st r0,[r22,4]
.Lrts_vap_skip:
 add r21,r21,1
.Lrts_vap_check:
 brlo r21,r17,.Lrts_vap_next
 mov r21,0
.Lrts_sta_next:
 ldb r0,[native_sta_states,r21]
 brhs r0,3,.Lrts_sta_skip
 mpyuw r22,r21,220
 add r22,r22,RTS_STA_TABLE
 ld r23,[r22]
 breq r19,0,.Lrts_sta_shadow
 lsr r24,r23,20
 bmsk r24,r24,3
 add r1,r21,rts_sta_native
 stb r24,[r1]
.Lrts_sta_shadow:
 ldb r0,[rts_sta_native,r21]
 bl rts_effective_nibble
 asl r0,r0,20
 and r23,r23,0xff0fffff
 or r23,r23,r0
 st r23,[r22]
.Lrts_sta_skip:
 add r21,r21,1
 brlo r21,256,.Lrts_sta_next
 mov r21,0
.Lrts_group_next:
 mpy r0,r21,RTS_VHT_STRIDE
 ldb r0,[r0,RTS_VHT_ACTIVE]
 breq r0,0,.Lrts_group_skip
 mpyuw r22,r21,100
 add r22,r22,RTS_GROUP_TABLE
 ld r23,[r22]
 breq r19,0,.Lrts_group_shadow
 lsr r24,r23,8
 bmsk r24,r24,3
 add r1,r21,rts_group_native
 stb r24,[r1]
.Lrts_group_shadow:
 ldb r0,[rts_group_native,r21]
 bl rts_effective_nibble
 asl r0,r0,8
 and r23,r23,0xfffff0ff
 or r23,r23,r0
 st r23,[r22]
.Lrts_group_skip:
 add r21,r21,1
 brlo r21,16,.Lrts_group_next
.Lrts_commit_publish:
 sync
 mov r0,0
 stb r0,[r13,1]
 seti r18
.Lrts_commit_done:
 /* Reproduce all three displaced argument instructions; the caller's
  * unchanged BL.d and r3=fc send the real native45 completion.
  */
 mov_s r0,0x45
 mov_s r1,0x11
 mov_s r2,0
 leave_s [r13-r24,blink,pcl]
.size rts_channel_commit,.-rts_channel_commit

.balign 4
.type rts_effective_nibble,@function
rts_effective_nibble:
 /* r0=raw nibble; r14=applied enabled. Change only r0/r1. */
 breq r14,0,.Lrts_nibble_done
 and r1,r0,3
 brne r1,0,.Lrts_nibble_cts
 or r0,r0,1
.Lrts_nibble_cts:
 and r1,r0,12
 brne r1,0,.Lrts_nibble_done
 or r0,r0,4
.Lrts_nibble_done:
 j_s [blink]
.size rts_effective_nibble,.-rts_effective_nibble
