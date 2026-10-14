/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * WAVE600 protected action RX, replacement at 0x5deb6.
 * Entry: r2 = frame, r3 = original length in [41, 16383].
 * Copy the body over the CCMP header using the existing overlap-safe primitive.
 * The caller has already removed CCMP header/MIC lengths from the RX descriptor.
 *
 * Reproduce the 16 bytes embedded in fix-firmware.py with GNU ARC binutils:
 * arc-linux-gnu-as -mcpu=archs -o pmf-rx-move.o pmf-rx-move.S
 * arc-linux-gnu-ld -Ttext=0x5deb6 --defsym=wave_memmove=0x59554 \
 *     -o pmf-rx-move.elf pmf-rx-move.o
 * arc-linux-gnu-objcopy -O binary --only-section=.text \
 *     pmf-rx-move.elf pmf-rx-move.bin
 *
 * Resolve the call through a linker symbol: a numeric ARC branch operand is
 * a relative displacement, not the destination address.
 */
.text
.global _start
_start:
 sub r3,r3,0x28
 exth_s r3,r3
 add_s r0,r2,0x18
 add_s r1,r0,0x8
 bl.d wave_memmove
 mov_s r2,r3
