/* SPDX-License-Identifier: GPL-2.0-only */
/* Both actual VAP DATA publications are covered:
 * the native PROT_DB callback and its four following DATA template copies.
 * The MGMT half of the protected record keeps the original input array.
 */
.equ FIXED_TAG, 0x5846
.equ FIXED_STAGE, 0x80

.section .fixed_vap_context,"ax",@progbits
.global fixed_vap_context
fixed_vap_context:
 /* At efdda the old callback's unused byte0 held r17. The complete original
  * callback108d44 never reads it. Our callback's explicitly owned byte0
  * carries the actual VAP r18; all native offsets4/8/9/a are unchanged.
  */
 stb r18,[sp]

.section .fixed_vap_callback,"ax",@progbits
.global fixed_vap_callback
.type fixed_vap_callback,@function
fixed_vap_callback:
 enter_s [r13-r17,blink]
 mov_s r13,r0                 /* actual protected shadow */
 mov_s r14,r1                 /* native callback context */
 ldb_s r15,[r14,9]
 ldb r16,[r14,8]
 ldb r17,[r14,0xa]
.Lvap_width:
 brhi r15,3,.Lvap_done
 brhi r15,r16,.Lvap_done
 sub r0,r17,1
 brhs r0,2,.Lvap_management
 ldb_s r0,[r14]               /* actual VAP from the owning caller */
 mov_s r1,r15
 ld_s r2,[r14,4]
 ldb r2,[r2,r15]              /* original automatic value */
 bl fixed_vap_value
 add2 r1,r13,1
 add1 r1,r1,r15
 stb_s r0,[r1]
.Lvap_management:
 or r0,r17,2
 brne r0,3,.Lvap_next
 ld_s r0,[r14,4]
 ldb r0,[r0,r15]
 add r1,r13,0x14
 add1 r1,r1,r15
 stb_s r0,[r1]
.Lvap_next:
 add_s r15,r15,1
 b .Lvap_width
.Lvap_done:
 leave_s [r13-r17,blink,pcl]
.size fixed_vap_callback,.-fixed_vap_callback

.section .fixed_vap_value,"ax",@progbits
.global fixed_vap_value
.type fixed_vap_value,@function
fixed_vap_value:
 /* r0=VAP,r1=width,r2=original byte. The firmware's VAP/TCR antenna owner
  * e1604 itself reads the current full native radio mask, independently of
  * its nominal argument. Use that SAME mask for the aggregate conversion.
  */
 ldb r12,[fixed_policy+5]
 brne r12,FIXED_STAGE,.Lvap_original
 enter_s [r13-r17,blink]
 mov_s r13,r0
 mov_s r14,r1
 bl native_radio_antenna_mask
 bl fixed_mask_value
 mov_s r17,r0
 mov_s r16,r1
 mov_s r0,r13
 mov_s r1,2
 bl.d native_vap_rate_index
 mov_s r2,r14
 mpyuw r0,r0,0x1c
 ldb r1,[native_vap_rate_mode]
 mov_s r2,native_vap_rates_low
 brls r1,3,.Lvap_rate_table
 mov_s r2,native_vap_rates_high
.Lvap_rate_table:
 ldb r0,[r2,r0]
 brne r0,1,.Lvap_result
 add r17,r17,r16
.Lvap_result:
 /* The common cap already includes every current width's native upper
  * and11b on its admitted2.4G band. The exact same chain/cal subtraction,
  * with native11b add-back, cannot exceed any of those bounds. */
 extb r0,r17
 leave_s [r13-r17,blink,pcl]
.Lvap_original:
 j_s.d [blink]
 mov_s r0,r2
.size fixed_vap_value,.-fixed_vap_value

.section .fixed_vap_callback_pointer,"ax",@progbits
.global fixed_vap_callback_pointer
fixed_vap_callback_pointer:
 /* Original efdcc literal owner; the whole callback is now placed in an
  * owned reclaimed span, leaving its old extent available to another
  * complete helper. The actual PROT_DB callback ABI is unchanged.
  */
 mov_s r3,fixed_vap_callback

.section .fixed_vap_template_align,"ax",@progbits
.global fixed_vap_template_align
fixed_vap_template_align:
 /* The native fallthrough is at a halfword address. ARC local branches
  * use aligned PC, so place the locally branching body at the next word.
  * This NOP plus its60-byte body fits the same original62-byte span. */
 nop_s

.section .fixed_vap_templates,"ax",@progbits
.global fixed_vap_templates
.type fixed_vap_templates,@function
fixed_vap_templates:
 /* Original efdf6 falls through the2-byte alignment NOP to efdf8.
  * The existing selector and publication owner remain unchanged.
  */
 /* Existing efd8c frame is live: r18=VAP,r14=input array,r13=min,r16=max.
  * The only completion is its original leave; this is not a second owner.
  */
.Ltemplate_width:
 extb_s r1,r13
 brhi r1,r16,.Ltemplate_done
 mov_s r0,r18
 ldb r2,[r14,r1]
 bl fixed_vap_value
 mpyuw r1,r18,0x180
 add r1,r1,native_vap_template_base
 add r1,r1,r13
 stb r0,[r1]
 stb r0,[r1,0x34]
 stb r0,[r1,0x68]
 stb r0,[r1,0x9c]
 add_s r13,r13,1
 b .Ltemplate_width
.Ltemplate_done:
 leave_s [r13-r18,fp,blink,pcl]
.size fixed_vap_templates,.-fixed_vap_templates
