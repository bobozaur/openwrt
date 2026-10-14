/* SPDX-License-Identifier: GPL-2.0-only */
/* Shared by all three links. Only UMAC reserves this physical IRAM range;
 * all accesses use the peripheral alias, including its initialization.
 */
twt_limits_storage_begin = 0xcc000;
twt_limits_storage_size = 0x1404;
wave_twt_limits_shared = 0xa0000000 + twt_limits_storage_begin;
SECTIONS
{
 .twt_limit_policy_hook native_twt_limit_site : {
  *(.twt_limit_policy_hook)
 }
}
ASSERT(SIZEOF(.twt_limit_policy_hook) == 0 ||
       SIZEOF(.twt_limit_policy_hook) == 4,
       "native limit entry hook extent")
