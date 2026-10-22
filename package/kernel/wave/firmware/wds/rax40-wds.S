/* Fixed-cohort NETGEAR RAX40 AP-firmware correction.
 * ARCv2 HS, little endian; linked at the original flat-image addresses.
 * No new storage: replacement code uses only original function extents.
 */

    /* One section keeps the native 16-bit interior branches local. The
     * incbin ranges retain every unmodified instruction byte exactly.
     */
    .section .hif,"ax",@progbits
    .incbin "ap_ram_gen6_wrx_600_real_phy.bin",0x5a88c,0x14
    bnc.d   hif_raw_return
    mov_s   r0,4
    .incbin "ap_ram_gen6_wrx_600_real_phy.bin",0x5a8a6,0x144
    b_s     hif_raw_assert
    .incbin "ap_ram_gen6_wrx_600_real_phy.bin",0x5a9ec,0x7c
    b_s     hif_raw_next

    .global hif_raw
hif_raw:
    /* HOST_DSC was copied to sp[0..16]. Word1 high bits are paired only
     * with the new host RAW_EAPOL kind: bit31 marker, bit30 CCMP intent.
     * All caller-saved operands are rebuilt before native publication.
     */
    ld_s    r0,[sp,4]
    bbit0   r0,31,.Llegacy_raw
    bbit0   r0,30,.Lplain_raw
    b_s     .Lprotected_raw

.Llegacy_raw:
    /* Original unmarked robust-management admission, including both
     * native SID-policy predicates and the actual subtype/action table.
     */
    tst     r3,0x6000
    bne_s   .Lplain_raw
    bl.d    native_policy_valid
    extb_s  r0,r3
    breq_s  r0,0,.Lplain_raw
    bl.d    native_policy_mfp
    ldb_s   r0,[sp,0]
    breq_s  r0,0,.Lplain_raw
    ld_s    r0,[sp,0]
    mov_s   r1,r0
    xbfu    r0,r0,122
    bl.d    native_policy_action
    xbfu    r1,r1,209
    breq_s  r0,0,.Lplain_raw
.Lprotected_raw:
    mov_s   r0,11
    b_s     .Lset_raw_type
.Lplain_raw:
    mov_s   r0,9
.Lset_raw_type:
    ld_s    r1,[r13,12]
    and     r1,r1,0xe0ffffff
    asl_s   r0,r0,24
    or_s    r0,r0,r1
    mov_s   r5,r0
    st_s    r0,[r13,12]

    .global hif_raw_assert
hif_raw_assert:
    /* The unchanged native null-data PD13 branch enters here with r5. */
    bmsk    r0,r5,3
    breq.d  r0,15,.Lclass_ok
    mov_s   r0,32
    mov_s   r1,6
    bl.d    native_assert
    mov     r2,404
.Lclass_ok:
    ld_s    r0,[sp,0]
    xbfu    r1,r0,13
    asl_s   r1,r1,30
    ld_s    r0,[r13,8]
    bclr_s  r0,r0,30
    or_s    r0,r0,r1
    st_s    r0,[r13,8]
    bl.d    native_raw_enqueue
    mov_s   r0,r13

    .global hif_raw_next
hif_raw_next:
    add_s   r16,r16,1
    b.d     native_hif_loop
    add_s   r14,r14,20

    .global hif_raw_return
hif_raw_return:
    ld_s    r1,[sp,20]
    ld_s    r0,[gp,-128]
    cmp_s   r0,r1
    blne    native_stack_fail
    leave_s [r13-r17,fp,blink,pcl]

    /* Key installation retains its original single computed-word commit.
     * The caller has already saved blink; the guarded leaf returns to the
     * original key-slot continuation without an additional stack frame.
     */
    .global key_commit_wrapper
key_commit_wrapper:
    bset_s  r0,r0,26              /* replay the native DATA-valid operation */
    mov_s   r1,r15                /* native SID */
    mov_s   r2,r13                /* native STA_DB pointer */
    b       guarded_commit
    .org 0x278

/* Both LMAC copies retain their setter and init entry addresses. The
 * simple leaf has exactly the original fullword MMIO effects and ABI.
 * The STA_ADD continuation is in space freed from those two functions.
 */
    .macro LMAC_BODY section_name, base, resume
    .section \section_name,"ax",@progbits
    .global lmac_role_\base
lmac_role_\base:
    mov_s   r2,0xa094783c
    ld_s    r3,[r2,0]
    bclr    r3,r3,r0
    breq_s  r1,0,.Lwrite_role_\base
    bset    r3,r3,r0
.Lwrite_role_\base:
    j_s.d   [blink]
    st_s    r3,[r2,0]

    .global lmac_sta_tail_\base
lmac_sta_tail_\base:
    /* Logical role is retained in shared VAP state. Re-derive at every
     * STA_ADD: class1 uses HC AP address selection; other STA classes
     * restore native STA selection. Ordinary AP is not changed.
     */
    ldb     r0,[r17,2]
    mpyuw   r1,r0,384
    ldb     r1,[r1,0xa033beff]
    bbit0   r1,0,.Lsta_replay_\base
    ldb     r1,[r17,122]
    cmp_s   r1,1
    mov_s   r1,1
    mov.eq  r1,0
    /* The enclosing STA_ADD enter_s saved its return address. Its tail
     * never reads live blink before leave_s restores that saved value.
     */
    bl      lmac_role_\base
    b_s     .Lsta_replay_\base

    /* Stable entry of the original native HC init function. */
    .org 64
    .global lmac_init_\base
lmac_init_\base:
    mov_s   r0,0xa0947844
    mov_s   r1,0x1d
    st_s    r1,[r0,0]
    mov_s   r1,0x7f0300
    j_s.d   [blink]
    st_s    r1,[r0,12]
.Lsta_replay_\base:
    ld_s    r1,[sp,8]
    ld_s    r0,[gp,356]
    b       \resume
    .org 100
    .endm

    LMAC_BODY .lmac1, one, native_sta_resume_one
    LMAC_BODY .lmac2, two, native_sta_resume_two

    .section .sta1_hook,"ax",@progbits
    b       lmac_sta_tail_one
    .section .sta2_hook,"ax",@progbits
    b       lmac_sta_tail_two

/* Stable native default-key entry. Its original body is relocated only
 * within the existing key-slot case extent below. No new RAM is used.
 */
    .section .default_owner,"ax",@progbits
    .global default_owner_entry
default_owner_entry:
    b       relocated_default

    .global guarded_commit
guarded_commit:
    /* r0: fully computed STA_DB+18 word; r1: SID; r2: STA_DB pointer.
     * Preserve r2 and blink. r1/r3 are dead native caller-saved operands.
     * Project only valid DATA CCMP and a logical STA. This does not read
     * or alter the packet-number/key storage or the native key index.
     */
    bbit0   r0,26,.Lplain_commit
    lsr     r3,r0,20
    bmsk_s  r3,r3,2               /* DATA cipher bits20..22 */
    brne    r3,2,.Lplain_commit
    mpyuw   r3,r1,12
    ldb     r3,[r3,0xb3841]       /* native security FSM byte9: VAP */
    mpyuw   r3,r3,384
    ld      r1,[0xb354c]          /* native logical VAP_DB base pointer */
    add_s   r3,r3,r1
    ldb_s   r3,[r3,31]            /* VAP+1f logical VSTA bit0 */
    btst_s  r3,0
    bne     native_projection_tail
.Lplain_commit:
    b       native_plain_commit

    .section .key_commit_hook,"ax",@progbits
    bl      key_commit_wrapper

/* Exact four-case key-slot update, compacted inside its own old extent.
 * Existing admitted GEN6 SIDs are below256, so128+4*SID+index is12 bits.
 * Index0/1 select STA_DB+0 low/high12; index2/3 select+4 low/high12.
 * Every bit outside that one12-bit selector is retained.
 */
    .section .keyslot_owner,"ax",@progbits
    /* Keep the section origin word-aligned for ARC PCL-relative local
     * branches. These original two bytes are the untouched trailing half
     * of the preceding add2 instruction; code still starts at4d266.
     */
    .incbin "ap_ram_gen6_wrx_600_real_phy.bin",0x4d264,2
    .global keyslot_owner_entry
keyslot_owner_entry:
    add2    r19,r0,32
    brhs    r16,4,keyslot_bad_index_assert
    lsr     r1,r16,1
    add2    r3,r13,r1
    and     r1,r16,1
    asl_s   r1,r1,4
    mov_s   r0,0xfff
    ld_s    r2,[r3,0]
    asl     r0,r0,r1
    bic     r2,r2,r0
    asl     r0,r19,r1
    or_s    r0,r0,r2
    st_s    r0,[r3,0]
    b       native_keyslot_done

    .global relocated_default
relocated_default:
    /* Native ABI: SID r0, key index r1, force flag r2. r12 is dead at all
     * three native direct call sites and retains the SID across arithmetic.
     * Preserve the exact original conditional no-store return.
     */
    mov_s   r12,r0
    mov_s   r3,0x96c68
    mpyuw   r0,r0,220
    ld_s    r3,[r3,0]
    breq.d  r2,1,.Ldefault_update
    add_s   r3,r3,r0
    ld_s    r0,[r3,0x1c]
    and     r0,r0,0x01000200
    cmp     r0,512
    jeq_s   [blink]
.Ldefault_update:
    ld_s    r0,[r3,0x18]
    and     r2,r0,0xcfffffff
    bmsk    r0,r1,1
    asl_s   r0,r0,28
    or_s    r0,r0,r2
    mov_s   r1,r12
    mov_s   r2,r3
    b       guarded_commit
    .global relocated_default_end
relocated_default_end:
    .org 0x76
    .global keyslot_bad_index_assert
keyslot_bad_index_assert:
    .incbin "ap_ram_gen6_wrx_600_real_phy.bin",0x4d2da,12


    /* Recipient BA setup only. Native window values are 0/1 or the
     * power-of-two table 2..256, so control bit16 identifies window1.
     * At the observed equal boundary, set the requested SN while that
     * one-entry window still owns the transition, then enlarge it.
     * Other native states retain their original command order/arguments.
     */
    .section .ba_owner,"ax",@progbits
    .global ba_owner_entry
ba_owner_entry:
    /* Eight saved words retain the native call-stack alignment. */
    enter_s [r13-r19,blink]
    mov_s   r16,r3
    mov_s   r17,r2
    mov_s   r13,r1
    bl.d    native_rxpp_halt
    mov_s   r14,r0
    mov_s   r0,0x72968
    asl_s   r1,r14,7
    ld_s    r0,[r0,0]
    add_s   r0,r0,r1
    asl_s   r15,r13,4
    add_s   r15,r15,r0
    bmsk.f  0,r15,1
    beq_s   .Lba_aligned
    mov_s   r0,60
    mov_s   r1,1
    bl.d    native_assert
    mov     r2,816
.Lba_aligned:
    ld_s    r0,[r15,0]
    mov_s   r18,0
    bbit0   r0,29,.Lba_window
    breq    r16,0,.Lba_exact
    bbit0   r0,16,.Lba_advance
    bmsk_s  r0,r0,11
    brne    r0,r16,.Lba_advance
.Lba_exact:
    mov_s   r18,1
    mov_s   r2,r16
    b_s     .Lba_pre_send
.Lba_advance:
    /* The native SN writer masks to12 bits, including wrap. */
    add_s   r2,r0,1
.Lba_pre_send:
    mov_s   r0,r14
    mov_s   r1,r13
    bl.d    native_rxpp_sn
    mov_s   r3,1
.Lba_window:
    mov_s   r0,r14
    mov_s   r1,r13
    bl.d    native_rxpp_window
    mov_s   r2,r17
    brne    r18,0,.Lba_shadow
    mov_s   r0,r14
    mov_s   r1,r13
    mov_s   r2,r16
    bl.d    native_rxpp_sn
    mov_s   r3,1
.Lba_shadow:
    asl_s   r0,r14,4
    ld_s    r1,[r15,0]
    add1_s  r0,r0,r13
    bmsk_s  r1,r1,11
    add_s   r0,r0,0x93858
    bl.d    native_rxpp_release
    sth_s   r1,[r0,0]
    leave_s [r13-r19,blink,pcl]
    .global ba_owner_code_end
ba_owner_code_end:
    .org 140,0
