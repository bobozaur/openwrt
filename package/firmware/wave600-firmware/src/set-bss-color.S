/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * WAVE600 06.00.08.00.87 per-VAP BSS color correction.
 *
 * Replace only the SET_BSS handler and existing six-bit color-slot helper in
 * both lower-MAC copies. Preserve the UMI ABI and original completion.
 * The driver rejects own-VAP color changes with peers before sending SET_BSS;
 * firmware retains its invariant assertion for an invalid internal request.
 *
 * Reproduce the spans embedded in fix-firmware.py with GNU ARC binutils:
 * arc-linux-gnu-as -mcpu=archs -o set-bss-color.o set-bss-color.S
 * arc-linux-gnu-ld --defsym=wave_lmac=0 -T set-bss-color.ld \
 *     -o set-bss-color-0.elf set-bss-color.o
 * arc-linux-gnu-objcopy -O binary --only-section=.set_bss \
 *     set-bss-color-0.elf set-bss-color-0.bin
 * arc-linux-gnu-objcopy -O binary --only-section=.color_helpers \
 *     set-bss-color-0.elf color-helpers-0.bin
 * Repeat linking/extraction with wave_lmac=1 for the second copy.
 * Absolute call targets are linker symbols, not numeric branch operands.
 */

.section .set_bss,"ax",@progbits
.global set_bss
.type set_bss,@function
set_bss:
 enter_s [r13-r15,blink]
 ld_s r14,[r0,0x10]       /* original UMI message, behind dispatch wrapper */
 ldb_s r13,[r14,0x14]    /* vapId */
 mpyuw r15,r13,0x26
 add_s r15,r15,vap_valid_base
 ldb r1,[r14,0x52]
 bl.d own_vap_color_check
 mov_s r0,r13
 /* r0 is the pre-existing VAP-valid byte; r1 is the requested color. */
 stb_s r1,[r15,1]
 brne r0,0,.Linitialized
 bl.d init_vap_record
 add_s r0,r14,0x10
 bl.d init_vap_protection
 mov_s r0,r13
 bl.d init_vap_tx
 mov_s r0,r13
 bl init_vap_barrier
.Linitialized:
 bl.d set_protection
 add_s r0,r14,0x10
 ldb r1,[r14,0x52]
 /* Refresh the VAP TX color even for an already initialized, empty VAP.
  * This is precisely bits0..5 of the field populated by init_vap_tx. */
 bmsk_s r1,r1,5
 mpyuw r0,r13,0x18
 asl_s r0,r0,4
 add_s r0,r0,0xa033b9b0
 ld_s r2,[r0]
 bmskn r2,r2,5
 or_s r2,r2,r1
 st_s r2,[r0]
 bl.d color_slot_write
 mov_s r0,r13
 ldb r1,[r14,0x54]
 setne r1,r1,1
 bl.d set_preamble
 mov_s r0,r13
 /* Original lower-MAC VAP completion, unchanged. */
 bl.d message_alloc
 mov_s r0,3
 mov_s r2,r0
 stb_s r13,[r0,0x11]
 stb 2,[r0,0x12]
 stb 8,[r0,0x10]
 mov_s r0,0xb7
 mov_s r1,0x15
 mov_s r3,0
 bl.d message_send
 mov_s r4,r13
 leave_s [r13-r15,blink,pcl]
.size set_bss,.-set_bss

.section .color_helpers,"ax",@progbits
.global color_slot_write
.type color_slot_write,@function
color_slot_write:
 /* Original helper's exact RMW semantics, without its stack/call wrappers.
  * Original read/write wrappers are just one load/store plus return.
  * Keep this entry address stable for the untouched REMOVE_VAP caller. */
 bmskn r2,r0,1
 add_s r2,r2,0xa0b001e0
 bmsk_s r0,r0,1
 asl_s r0,r0,3
 bmsk_s r1,r1,5
 mov_s r3,0x3f
 asl r3,r3,r0
 asl r1,r1,r0
 ld_s r12,[r2]
 bic r12,r12,r3
 or_s r1,r1,r12
 j_s.d [blink]
 st_s r1,[r2]
.size color_slot_write,.-color_slot_write

/* ARC BL targets must be word aligned, even though many instructions are16-bit. */
.balign 4
.type own_vap_color_check,@function
own_vap_color_check:
 /* Private call: r0=vapId, r1=requested color, r15=&valid/color record.
  * Preserve r1 and all callee-saved registers. Return prior valid byte.
  * Every nonzero station-list node is still owned by its recorded VAP;
  * stopping traffic does not remove ownership. Scan all256 fixed slots so
  * the predicate is bounded even if a linked-list pointer is damaged. */
 ldb_s r2,[r15]
 breq r2,0,.Lscan
 ldb_s r3,[r15,1]
 breq r3,r1,.Laccepted
.Lscan:
 mov_s r2,station_nodes
 mov r3,256
.Lpeer:
 ldb_s r12,[r2,8]
 breq_s r12,0,.Lnext
 ldb_s r12,[r2,9]
 breq r12,r0,.Lillegal
.Lnext:
 add_s r2,r2,10
 sub_s r3,r3,1
 brne_s r3,0,.Lpeer
.Laccepted:
 j_s.d [blink]
 ldb_s r0,[r15]
.Lillegal:
 mov_s r0,39
 mov_s r1,9
 bl.d firmware_fatal
 mov r2,1880
 /* A debug-returning fatal hook must not resume an illegal transaction. */
.Lassert_returned:
 b_s .Lassert_returned
.size own_vap_color_check,.-own_vap_color_check
