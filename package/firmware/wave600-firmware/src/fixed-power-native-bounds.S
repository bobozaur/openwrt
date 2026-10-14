/* SPDX-License-Identifier: GPL-2.0-only */
/* Private/unselected. Exact ebd14 native bounds calculation, compacted only
 * by retiring its three SCD prints1990/2019/2058. The adaptive internal
 * floor, override, antenna/calibration operations and both cached external
 * bounds retain their original computation and publication. This is an
 * owned function extent, not a padding/code-cave assumption.
 */
.section .fixed_native_bounds,"ax",@progbits
.global fixed_native_bounds
.type fixed_native_bounds,@function
fixed_native_bounds:
 enter_s [r13-r22,blink]
 mov_s r18,r0
 mov r0,0x154
 mov_s r19,r2
 mpyuw r2,r18,r0
 mov_s r13,0x3464
 mov_s r15,r1
 mov_s r20,r3
 ld_s r0,[r2,r13]
 ldb r1,[r0,0x2c]
 breq r1,0xff,.Ldefault_internal
 add_s r13,r13,r2
 stb_s r1,[r13,0x10]
 b_s .Lexternal_lower
.Ldefault_internal:
 mov r17,r18
 bmsk r21,r18,4
 mov_s r16,native_power_probe_bitmap
 xbfu r17,r17,0x45
 mov_s r0,1
 asl r14,r0,r21
 add2 r0,r16,r17
 ld_s r0,[r0,0x60]
 tst_s r0,r14
 beq_s .Lnew_internal
 add_s r13,r13,r2
 ldb.x r1,[r13,0x10]
 b_s .Lexternal_lower
.Lnew_internal:
 mpyuw r0,r18,10
 ldb r22,[r0,native_station_vap]
 bl.d native_internal_power_floor
 mov_s r0,r19
 ld.as r1,[r16,r17]
 and_s r14,r14,r1
 ldb.x r2,[r22,native_vap_power_floor]
 lsr r1,r14,r21
 extb_s r1,r1
 mpyw_s r2,r2,r1
 extb_s r1,r0
 mov_s r0,r18
 mpyuw r0,r0,0x154
 add_s r0,r0,r13
 add_s r1,r1,r2
 stb_s r1,[r0,0x10]
.Lexternal_lower:
 add r13,r15,native_peer_upper
 bl.d native_peer_antenna_count
 mov_s r0,r18
 add r14,r0,gp
 bl.d native_antenna_calibration
 mov_s r0,r19
 ldb r1,[r14,0xab]
 add_s r0,r0,r1
 add r1,r15,gp
 ldb r1,[r1,0xb0]
 sub_s r0,r1,r0
 sexb r4,r0
 max r4,r4,-12
 stb r4,[r13,4]
 bl.d native_antenna_count
 mov_s r0,r19
 add_s r0,r0,gp
 ldb r14,[r0,0xab]
 bl.d native_antenna_calibration
 mov_s r0,r19
 mov_s r1,native_normal_limit
 add_s r2,r1,r15
 breq r20,0,.Lnormal_upper
 ldb.x r2,[r2,8]
 ldb.x r1,[r15,r1]
 min r1,r1,r2
 b_s .Lupper_value
.Lnormal_upper:
 ldb_s r1,[r15,r1]
.Lupper_value:
 add_s r0,r0,r14
 sub_s r0,r1,r0
 sexb_s r0,r0
 min r0,r0,63
 stb_s r0,[r13]
 leave_s [r13-r22,blink,pcl]
.size fixed_native_bounds,.-fixed_native_bounds
