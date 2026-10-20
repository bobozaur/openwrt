/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Complete WAVE600 06.00.08.00.87 stream-template setup through its native
 * lower owner. Replace only the existing ADD_STREAM handler's28-byte span.
 * r0 is the persistent original request; wrapper byte12 supplies VAP/radio
 * routing. The native25b template owner and f6 ->1448 confirmation retain
 * that request. No new request allocation, logger counter or global is added.
 *
 * Reproduce the span embedded in fix-firmware.py with GNU ARC binutils:
 * arc-linux-gnu-as -mcpu=archs -o add-stream.o add-stream.S
 * arc-linux-gnu-ld -T add-stream.ld -o add-stream.elf add-stream.o
 * arc-linux-gnu-objcopy -O binary --only-section=.text \
 *     add-stream.elf add-stream.bin
 */
.text
.global _start
_start:
 mov_s r2,r0
 ldb r4,[r2,0xc]
 mov r0,0x25b
 mov_s r1,4
 b.d wave_message_send
 mov_s r3,0
 .rept 5
 nop_s
 .endr
