/* SPDX-License-Identifier: GPL-2.0-only */
SECTIONS
{
 .rts_dispatch_hook rts_dispatch_site : { *(.rts_dispatch_hook) }
 .rts_completion_hook rts_completion_site : { *(.rts_completion_hook) }
 .rts_vap_hook rts_vap_site : { *(.rts_vap_hook) }
 .rts_sta_hook rts_sta_site : { *(.rts_sta_hook) }
 .rts_group_hook rts_group_site : { *(.rts_group_hook) }
 .rts_group_constructor_hook rts_group_constructor_site : {
  *(.rts_group_constructor_hook)
 }
}
ASSERT(SIZEOF(.rts_dispatch_hook) == 4, "replace only the20f callback")
ASSERT(SIZEOF(.rts_completion_hook) == 6, "replace only the45 argument MOVs")
ASSERT(SIZEOF(.rts_vap_hook) == 6, "replace only the native length immediate")
ASSERT(SIZEOF(.rts_sta_hook) == 6, "replace only the STA writer entry")
ASSERT(SIZEOF(.rts_group_hook) == 6, "replace only the group writer entry")
ASSERT(SIZEOF(.rts_group_constructor_hook) == 8,
       "replace only the group method constructor")
