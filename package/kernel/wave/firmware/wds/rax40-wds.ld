OUTPUT_FORMAT("elf32-littlearc")
OUTPUT_ARCH(arc)
/* Native branch destinations must be linker symbols. ARC GAS treats a bare
 * numeric branch operand as a displacement, not a firmware address.
 */
native_projection_tail = 0x4d154;
native_plain_commit = 0x4d16c;
native_keyslot_done = 0x4d2e6;
native_policy_valid = 0x5b620;
native_policy_mfp = 0x5b630;
native_policy_action = 0x5b5f4;
native_assert = 0x44450;
native_raw_enqueue = 0x513ac;
native_hif_loop = 0x5a89a;
native_stack_fail = 0x53b40;
native_sta_resume_one = 0xfe25e;
native_sta_resume_two = 0x14df12;
SECTIONS
{
  .default_owner 0x4d0c8 : { *(.default_owner) }
  .key_commit_hook 0x4d25e : { *(.key_commit_hook) }
  .keyslot_owner 0x4d264 : { *(.keyslot_owner) }
  .hif 0x5a88c : { *(.hif) }
  .lmac1 0xf377c : { *(.lmac1) }
  .sta1_hook 0xfe25a : { *(.sta1_hook) }
  .lmac2 0x143424 : { *(.lmac2) }
  .sta2_hook 0x14df0e : { *(.sta2_hook) }
  /DISCARD/ : { *(.comment) *(.note*) *(.ARC.attributes) }
}
ASSERT(SIZEOF(.hif) == 0x278, "HIF original function extent")
ASSERT(hif_raw == 0x5aa6a, "HIF raw classifier entry")
ASSERT(SIZEOF(.lmac1) == 100, "LMAC1 original setter/init extent")
ASSERT(SIZEOF(.lmac2) == 100, "LMAC2 original setter/init extent")
ASSERT(lmac_init_one == 0xf37bc, "LMAC1 init ABI entry")
ASSERT(lmac_init_two == 0x143464, "LMAC2 init ABI entry")
ASSERT(SIZEOF(.sta1_hook) == 4, "LMAC1 STA_ADD tail span")
ASSERT(SIZEOF(.sta2_hook) == 4, "LMAC2 STA_ADD tail span")

ASSERT(SIZEOF(.default_owner) == 56, "Native default-key original extent")
ASSERT(default_owner_entry == 0x4d0c8, "Native default-key ABI entry")
ASSERT(guarded_commit == 0x4d0cc, "Guarded computed-word commit entry")
ASSERT(SIZEOF(.key_commit_hook) == 4, "Native key commit hook span")
ASSERT(SIZEOF(.keyslot_owner) == 130, "Original four-case extent plus unchanged two-byte prefix")
ASSERT(keyslot_owner_entry == 0x4d266, "Original four-case key-slot entry")
ASSERT(relocated_default == 0x4d298, "Default-key relocated body")
ASSERT(relocated_default_end == 0x4d2d6, "Default-key relocated body bound")
ASSERT(keyslot_bad_index_assert == 0x4d2da, "Native invalid-key assertion PC")
ASSERT(key_commit_wrapper == 0x5aaf8, "Word-aligned key commit wrapper in original HIF tail")
