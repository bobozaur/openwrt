/* Exact lower-copy owned instruction spans. */
INCLUDE fixed-power-symbols-lm0.ld
SECTIONS
{
 .fixed_su_entry 0xe3748 : { *(.fixed_su_entry) }
 .fixed_su_log_return 0xe37cc : { *(.fixed_su_log_return) }
 .fixed_recalc_gate 0xe37d4 : { *(.fixed_recalc_gate) }
 .fixed_peer_bounds_log_return 0xebc56 : { *(.fixed_peer_bounds_log_return) }
 .fixed_reset_guard 0xebc58 : { *(.fixed_reset_guard) }
 .fixed_clamp_log_return 0xebd00 : { *(.fixed_clamp_log_return) }
 .fixed_su_dispatch 0xebd04 : { *(.fixed_su_dispatch) }
 .fixed_native_bounds 0xebd14 : { *(.fixed_native_bounds) }
 .fixed_mask_value 0xebe10 : { *(.fixed_mask_value) }
 .fixed_calibration_log_skip 0xefc42 : { *(.fixed_calibration_log_skip) }
 .fixed_cal_automatic 0xefc48 : { *(.fixed_cal_automatic) }
 .fixed_vap_callback_pointer 0xefdcc : { *(.fixed_vap_callback_pointer) }
 .fixed_vap_context 0xefdda : { *(.fixed_vap_context) }
 .fixed_vap_template_align 0xefdf6 : { *(.fixed_vap_template_align) }
 .fixed_vap_templates 0xefdf8 : { *(.fixed_vap_templates) }
 .fixed_query_error 0xefe34 : { *(.fixed_query_error) }
 .fixed_reset_hook 0x101066 : { *(.fixed_reset_hook) }
 .fixed_calibration_hook 0x101540 : { *(.fixed_calibration_hook) }
 .fixed_completion_hook 0x10156c : { *(.fixed_completion_hook) }
 .fixed_lower_entry 0x101688 : { *(.fixed_lower_entry) }
 .fixed_recalc_hook 0x1027f2 : { *(.fixed_recalc_hook) }
 .fixed_feedback_log_skip 0x10281a : { *(.fixed_feedback_log_skip) }
 .fixed_group_native 0x102820 : { *(.fixed_group_native) }
 .fixed_group_entry 0x1079b4 : { *(.fixed_group_entry) }
 .fixed_lower_complete 0x1079b8 : { *(.fixed_lower_complete) }
 .fixed_group_mgmt_copy 0x1079e0 : { *(.fixed_group_mgmt_copy) }
 .fixed_group_refresh 0x107a00 : { *(.fixed_group_refresh) }
 .fixed_group_aux_copy 0x107af2 : { *(.fixed_group_aux_copy) }
 .fixed_vap_value 0x107b10 : { *(.fixed_vap_value) }
 .fixed_su_mode 0x107b70 : { *(.fixed_su_mode) }
 .fixed_dl_dump_retire 0x10804a : { *(.fixed_dl_dump_retire) }
 .fixed_lower_stage 0x10804c : { *(.fixed_lower_stage) }
 .fixed_vap_callback 0x1080fc : { *(.fixed_vap_callback) }
 .fixed_query 0x108d44 : { *(.fixed_query) }
 .fixed_su_value 0x108d8c : { *(.fixed_su_value) }
 .fixed_group_dispatch 0x108dc8 : { *(.fixed_group_dispatch) }
 .fixed_group_rate_log_a 0x10a8c2 : { *(.fixed_group_rate_log_a) }
 .fixed_group_active 0x10a8c8 : { *(.fixed_group_active) }
 .fixed_group_rate_log_b 0x10a8fa : { *(.fixed_group_rate_log_b) }
 .fixed_cal_unrepresentable 0x10a900 : { *(.fixed_cal_unrepresentable) }
 .fixed_group_rate_log_c 0x10a930 : { *(.fixed_group_rate_log_c) }
 .fixed_lower_dispatch 0x10a934 : { *(.fixed_lower_dispatch) }
 .fixed_group_rate_log_d 0x10a986 : { *(.fixed_group_rate_log_d) }
 .fixed_channel_complete 0x10a98c : { *(.fixed_channel_complete) }
 .fixed_ul_dump_retire 0x10ab0e : { *(.fixed_ul_dump_retire) }
 .fixed_after_calibration 0x10ab10 : { *(.fixed_after_calibration) }
 .fixed_cap 0x10ab90 : { *(.fixed_cap) }
 .fixed_lower_version 0x10f0b0 : { *(.fixed_lower_version) }
 .fixed_group_value 0x10f0e4 : { *(.fixed_group_value) }
 .fixed_copy_vap_tcr_fields 0x10f134 : { *(.fixed_copy_vap_tcr_fields) }
 /DISCARD/ : { *(.text) *(.data) *(.bss) *(.ARC.attributes) }
}
ASSERT(SIZEOF(.fixed_lower_entry) > 0 && SIZEOF(.fixed_lower_entry) <= 6, "fixed_lower_entry exceeds its owned span")
ASSERT(ADDR(.fixed_lower_entry) + SIZEOF(.fixed_lower_entry) <= 0x10168e, "fixed_lower_entry end crossed")
ASSERT(SIZEOF(.fixed_su_entry) > 0 && SIZEOF(.fixed_su_entry) <= 4, "fixed_su_entry exceeds its owned span")
ASSERT(ADDR(.fixed_su_entry) + SIZEOF(.fixed_su_entry) <= 0xe374c, "fixed_su_entry end crossed")
ASSERT(SIZEOF(.fixed_group_entry) > 0 && SIZEOF(.fixed_group_entry) <= 4, "fixed_group_entry exceeds its owned span")
ASSERT(ADDR(.fixed_group_entry) + SIZEOF(.fixed_group_entry) <= 0x1079b8, "fixed_group_entry end crossed")
ASSERT(SIZEOF(.fixed_calibration_hook) > 0 && SIZEOF(.fixed_calibration_hook) <= 6, "fixed_calibration_hook exceeds its owned span")
ASSERT(ADDR(.fixed_calibration_hook) + SIZEOF(.fixed_calibration_hook) <= 0x101546, "fixed_calibration_hook end crossed")
ASSERT(SIZEOF(.fixed_completion_hook) > 0 && SIZEOF(.fixed_completion_hook) <= 6, "fixed_completion_hook exceeds its owned span")
ASSERT(ADDR(.fixed_completion_hook) + SIZEOF(.fixed_completion_hook) <= 0x101572, "fixed_completion_hook end crossed")
ASSERT(SIZEOF(.fixed_vap_context) > 0 && SIZEOF(.fixed_vap_context) <= 4, "fixed_vap_context exceeds its owned span")
ASSERT(ADDR(.fixed_vap_context) + SIZEOF(.fixed_vap_context) <= 0xefdde, "fixed_vap_context end crossed")
ASSERT(SIZEOF(.fixed_vap_callback_pointer) > 0 && SIZEOF(.fixed_vap_callback_pointer) <= 6, "fixed_vap_callback_pointer exceeds its owned span")
ASSERT(ADDR(.fixed_vap_callback_pointer) + SIZEOF(.fixed_vap_callback_pointer) <= 0xefdd2, "fixed_vap_callback_pointer end crossed")
ASSERT(SIZEOF(.fixed_reset_hook) > 0 && SIZEOF(.fixed_reset_hook) <= 6, "fixed_reset_hook exceeds its owned span")
ASSERT(ADDR(.fixed_reset_hook) + SIZEOF(.fixed_reset_hook) <= 0x10106c, "fixed_reset_hook end crossed")
ASSERT(SIZEOF(.fixed_recalc_hook) > 0 && SIZEOF(.fixed_recalc_hook) <= 4, "fixed_recalc_hook exceeds its owned span")
ASSERT(ADDR(.fixed_recalc_hook) + SIZEOF(.fixed_recalc_hook) <= 0x1027f6, "fixed_recalc_hook end crossed")
ASSERT(SIZEOF(.fixed_native_bounds) > 0 && SIZEOF(.fixed_native_bounds) <= 252, "fixed_native_bounds exceeds its owned span")
ASSERT(ADDR(.fixed_native_bounds) + SIZEOF(.fixed_native_bounds) <= 0xebe10, "fixed_native_bounds end crossed")
ASSERT(SIZEOF(.fixed_mask_value) > 0 && SIZEOF(.fixed_mask_value) <= 44, "fixed_mask_value exceeds its owned span")
ASSERT(ADDR(.fixed_mask_value) + SIZEOF(.fixed_mask_value) <= 0xebe3c, "fixed_mask_value end crossed")
ASSERT(SIZEOF(.fixed_lower_version) > 0 && SIZEOF(.fixed_lower_version) <= 52, "fixed_lower_version exceeds its owned span")
ASSERT(ADDR(.fixed_lower_version) + SIZEOF(.fixed_lower_version) <= 0x10f0e4, "fixed_lower_version end crossed")
ASSERT(SIZEOF(.fixed_group_value) > 0 && SIZEOF(.fixed_group_value) <= 80, "fixed_group_value exceeds its owned span")
ASSERT(ADDR(.fixed_group_value) + SIZEOF(.fixed_group_value) <= 0x10f134, "fixed_group_value end crossed")
ASSERT(SIZEOF(.fixed_copy_vap_tcr_fields) > 0 && SIZEOF(.fixed_copy_vap_tcr_fields) <= 39, "fixed_copy_vap_tcr_fields exceeds its owned span")
ASSERT(ADDR(.fixed_copy_vap_tcr_fields) + SIZEOF(.fixed_copy_vap_tcr_fields) <= 0x10f15b, "fixed_copy_vap_tcr_fields end crossed")
ASSERT(SIZEOF(.fixed_dl_dump_retire) > 0 && SIZEOF(.fixed_dl_dump_retire) <= 2, "fixed_dl_dump_retire exceeds its owned span")
ASSERT(ADDR(.fixed_dl_dump_retire) + SIZEOF(.fixed_dl_dump_retire) <= 0x10804c, "fixed_dl_dump_retire end crossed")
ASSERT(SIZEOF(.fixed_lower_stage) > 0 && SIZEOF(.fixed_lower_stage) <= 176, "fixed_lower_stage exceeds its owned span")
ASSERT(ADDR(.fixed_lower_stage) + SIZEOF(.fixed_lower_stage) <= 0x1080fc, "fixed_lower_stage end crossed")
ASSERT(SIZEOF(.fixed_vap_callback) > 0 && SIZEOF(.fixed_vap_callback) <= 88, "fixed_vap_callback exceeds its owned span")
ASSERT(ADDR(.fixed_vap_callback) + SIZEOF(.fixed_vap_callback) <= 0x108154, "fixed_vap_callback end crossed")
ASSERT(SIZEOF(.fixed_ul_dump_retire) > 0 && SIZEOF(.fixed_ul_dump_retire) <= 2, "fixed_ul_dump_retire exceeds its owned span")
ASSERT(ADDR(.fixed_ul_dump_retire) + SIZEOF(.fixed_ul_dump_retire) <= 0x10ab10, "fixed_ul_dump_retire end crossed")
ASSERT(SIZEOF(.fixed_after_calibration) > 0 && SIZEOF(.fixed_after_calibration) <= 128, "fixed_after_calibration exceeds its owned span")
ASSERT(ADDR(.fixed_after_calibration) + SIZEOF(.fixed_after_calibration) <= 0x10ab90, "fixed_after_calibration end crossed")
ASSERT(SIZEOF(.fixed_cap) > 0 && SIZEOF(.fixed_cap) <= 128, "fixed_cap exceeds its owned span")
ASSERT(ADDR(.fixed_cap) + SIZEOF(.fixed_cap) <= 0x10ac10, "fixed_cap end crossed")
ASSERT(SIZEOF(.fixed_query) > 0 && SIZEOF(.fixed_query) <= 72, "fixed_query exceeds its owned span")
ASSERT(ADDR(.fixed_query) + SIZEOF(.fixed_query) <= 0x108d8c, "fixed_query end crossed")
ASSERT(SIZEOF(.fixed_su_value) > 0 && SIZEOF(.fixed_su_value) <= 60, "fixed_su_value exceeds its owned span")
ASSERT(ADDR(.fixed_su_value) + SIZEOF(.fixed_su_value) <= 0x108dc8, "fixed_su_value end crossed")
ASSERT(SIZEOF(.fixed_group_dispatch) > 0 && SIZEOF(.fixed_group_dispatch) <= 24, "fixed_group_dispatch exceeds its owned span")
ASSERT(ADDR(.fixed_group_dispatch) + SIZEOF(.fixed_group_dispatch) <= 0x108de0, "fixed_group_dispatch end crossed")
ASSERT(SIZEOF(.fixed_group_mgmt_copy) > 0 && SIZEOF(.fixed_group_mgmt_copy) <= 32, "fixed_group_mgmt_copy exceeds its owned span")
ASSERT(ADDR(.fixed_group_mgmt_copy) + SIZEOF(.fixed_group_mgmt_copy) <= 0x107a00, "fixed_group_mgmt_copy end crossed")
ASSERT(SIZEOF(.fixed_group_refresh) > 0 && SIZEOF(.fixed_group_refresh) <= 116, "fixed_group_refresh exceeds its owned span")
ASSERT(ADDR(.fixed_group_refresh) + SIZEOF(.fixed_group_refresh) <= 0x107a74, "fixed_group_refresh end crossed")
ASSERT(SIZEOF(.fixed_group_aux_copy) > 0 && SIZEOF(.fixed_group_aux_copy) <= 30, "fixed_group_aux_copy exceeds its owned span")
ASSERT(ADDR(.fixed_group_aux_copy) + SIZEOF(.fixed_group_aux_copy) <= 0x107b10, "fixed_group_aux_copy end crossed")
ASSERT(SIZEOF(.fixed_vap_value) > 0 && SIZEOF(.fixed_vap_value) <= 96, "fixed_vap_value exceeds its owned span")
ASSERT(ADDR(.fixed_vap_value) + SIZEOF(.fixed_vap_value) <= 0x107b70, "fixed_vap_value end crossed")
ASSERT(SIZEOF(.fixed_su_mode) > 0 && SIZEOF(.fixed_su_mode) <= 22, "fixed_su_mode exceeds its owned span")
ASSERT(ADDR(.fixed_su_mode) + SIZEOF(.fixed_su_mode) <= 0x107b86, "fixed_su_mode end crossed")
ASSERT(SIZEOF(.fixed_vap_templates) > 0 && SIZEOF(.fixed_vap_templates) <= 60, "fixed_vap_templates exceeds its owned span")
ASSERT(ADDR(.fixed_vap_templates) + SIZEOF(.fixed_vap_templates) <= 0xefe34, "fixed_vap_templates end crossed")
ASSERT(SIZEOF(.fixed_query_error) > 0 && SIZEOF(.fixed_query_error) <= 10, "fixed_query_error exceeds its owned span")
ASSERT(ADDR(.fixed_query_error) + SIZEOF(.fixed_query_error) <= 0xefe3e, "fixed_query_error end crossed")
ASSERT(SIZEOF(.fixed_su_log_return) > 0 && SIZEOF(.fixed_su_log_return) <= 8, "fixed_su_log_return exceeds its owned span")
ASSERT(ADDR(.fixed_su_log_return) + SIZEOF(.fixed_su_log_return) <= 0xe37d4, "fixed_su_log_return end crossed")
ASSERT(SIZEOF(.fixed_recalc_gate) > 0 && SIZEOF(.fixed_recalc_gate) <= 34, "fixed_recalc_gate exceeds its owned span")
ASSERT(ADDR(.fixed_recalc_gate) + SIZEOF(.fixed_recalc_gate) <= 0xe37f6, "fixed_recalc_gate end crossed")
ASSERT(SIZEOF(.fixed_clamp_log_return) > 0 && SIZEOF(.fixed_clamp_log_return) <= 4, "fixed_clamp_log_return exceeds its owned span")
ASSERT(ADDR(.fixed_clamp_log_return) + SIZEOF(.fixed_clamp_log_return) <= 0xebd04, "fixed_clamp_log_return end crossed")
ASSERT(SIZEOF(.fixed_su_dispatch) > 0 && SIZEOF(.fixed_su_dispatch) <= 16, "fixed_su_dispatch exceeds its owned span")
ASSERT(ADDR(.fixed_su_dispatch) + SIZEOF(.fixed_su_dispatch) <= 0xebd14, "fixed_su_dispatch end crossed")
ASSERT(SIZEOF(.fixed_calibration_log_skip) > 0 && SIZEOF(.fixed_calibration_log_skip) <= 6, "fixed_calibration_log_skip exceeds its owned span")
ASSERT(ADDR(.fixed_calibration_log_skip) + SIZEOF(.fixed_calibration_log_skip) <= 0xefc48, "fixed_calibration_log_skip end crossed")
ASSERT(SIZEOF(.fixed_cal_automatic) > 0 && SIZEOF(.fixed_cal_automatic) <= 16, "fixed_cal_automatic exceeds its owned span")
ASSERT(ADDR(.fixed_cal_automatic) + SIZEOF(.fixed_cal_automatic) <= 0xefc58, "fixed_cal_automatic end crossed")
ASSERT(SIZEOF(.fixed_peer_bounds_log_return) > 0 && SIZEOF(.fixed_peer_bounds_log_return) <= 2, "fixed_peer_bounds_log_return exceeds its owned span")
ASSERT(ADDR(.fixed_peer_bounds_log_return) + SIZEOF(.fixed_peer_bounds_log_return) <= 0xebc58, "fixed_peer_bounds_log_return end crossed")
ASSERT(SIZEOF(.fixed_reset_guard) > 0 && SIZEOF(.fixed_reset_guard) <= 44, "fixed_reset_guard exceeds its owned span")
ASSERT(ADDR(.fixed_reset_guard) + SIZEOF(.fixed_reset_guard) <= 0xebc84, "fixed_reset_guard end crossed")
ASSERT(SIZEOF(.fixed_group_rate_log_a) > 0 && SIZEOF(.fixed_group_rate_log_a) <= 6, "fixed_group_rate_log_a exceeds its owned span")
ASSERT(ADDR(.fixed_group_rate_log_a) + SIZEOF(.fixed_group_rate_log_a) <= 0x10a8c8, "fixed_group_rate_log_a end crossed")
ASSERT(SIZEOF(.fixed_group_active) > 0 && SIZEOF(.fixed_group_active) <= 12, "fixed_group_active exceeds its owned span")
ASSERT(ADDR(.fixed_group_active) + SIZEOF(.fixed_group_active) <= 0x10a8d4, "fixed_group_active end crossed")
ASSERT(SIZEOF(.fixed_group_rate_log_b) > 0 && SIZEOF(.fixed_group_rate_log_b) <= 6, "fixed_group_rate_log_b exceeds its owned span")
ASSERT(ADDR(.fixed_group_rate_log_b) + SIZEOF(.fixed_group_rate_log_b) <= 0x10a900, "fixed_group_rate_log_b end crossed")
ASSERT(SIZEOF(.fixed_cal_unrepresentable) > 0 && SIZEOF(.fixed_cal_unrepresentable) <= 12, "fixed_cal_unrepresentable exceeds its owned span")
ASSERT(ADDR(.fixed_cal_unrepresentable) + SIZEOF(.fixed_cal_unrepresentable) <= 0x10a90c, "fixed_cal_unrepresentable end crossed")
ASSERT(SIZEOF(.fixed_group_rate_log_c) > 0 && SIZEOF(.fixed_group_rate_log_c) <= 4, "fixed_group_rate_log_c exceeds its owned span")
ASSERT(ADDR(.fixed_group_rate_log_c) + SIZEOF(.fixed_group_rate_log_c) <= 0x10a934, "fixed_group_rate_log_c end crossed")
ASSERT(SIZEOF(.fixed_lower_dispatch) > 0 && SIZEOF(.fixed_lower_dispatch) <= 20, "fixed_lower_dispatch exceeds its owned span")
ASSERT(ADDR(.fixed_lower_dispatch) + SIZEOF(.fixed_lower_dispatch) <= 0x10a948, "fixed_lower_dispatch end crossed")
ASSERT(SIZEOF(.fixed_group_rate_log_d) > 0 && SIZEOF(.fixed_group_rate_log_d) <= 6, "fixed_group_rate_log_d exceeds its owned span")
ASSERT(ADDR(.fixed_group_rate_log_d) + SIZEOF(.fixed_group_rate_log_d) <= 0x10a98c, "fixed_group_rate_log_d end crossed")
ASSERT(SIZEOF(.fixed_channel_complete) > 0 && SIZEOF(.fixed_channel_complete) <= 12, "fixed_channel_complete exceeds its owned span")
ASSERT(ADDR(.fixed_channel_complete) + SIZEOF(.fixed_channel_complete) <= 0x10a998, "fixed_channel_complete end crossed")
ASSERT(SIZEOF(.fixed_lower_complete) > 0 && SIZEOF(.fixed_lower_complete) <= 20, "fixed_lower_complete exceeds its owned span")
ASSERT(ADDR(.fixed_lower_complete) + SIZEOF(.fixed_lower_complete) <= 0x1079cc, "fixed_lower_complete end crossed")
ASSERT(SIZEOF(.fixed_feedback_log_skip) > 0 && SIZEOF(.fixed_feedback_log_skip) <= 6, "fixed_feedback_log_skip exceeds its owned span")
ASSERT(ADDR(.fixed_feedback_log_skip) + SIZEOF(.fixed_feedback_log_skip) <= 0x102820, "fixed_feedback_log_skip end crossed")
ASSERT(SIZEOF(.fixed_group_native) > 0 && SIZEOF(.fixed_group_native) <= 10, "fixed_group_native exceeds its owned span")
ASSERT(ADDR(.fixed_group_native) + SIZEOF(.fixed_group_native) <= 0x10282a, "fixed_group_native end crossed")
ASSERT((fixed_current_cap & 3) == 0, "fixed_current_cap call target is not word aligned")
ASSERT((fixed_policy_query & 3) == 0, "fixed_policy_query call target is not word aligned")
ASSERT((fixed_su_value & 3) == 0, "fixed_su_value call target is not word aligned")
ASSERT((fixed_mask_value & 3) == 0, "fixed_mask_value call target is not word aligned")
ASSERT((fixed_group_value & 3) == 0, "fixed_group_value call target is not word aligned")
ASSERT((fixed_refresh_groups & 3) == 0, "fixed_refresh_groups call target is not word aligned")
ASSERT((fixed_reset_guard & 3) == 0, "fixed_reset_guard call target is not word aligned")
ASSERT((fixed_recalc_gate & 3) == 0, "fixed_recalc_gate call target is not word aligned")
ASSERT((fixed_vap_callback & 3) == 0, "fixed_vap_callback call target is not word aligned")
ASSERT((fixed_vap_value & 3) == 0, "fixed_vap_value call target is not word aligned")
ASSERT((fixed_after_calibration & 3) == 0, "fixed_after_calibration call target is not word aligned")
ASSERT((fixed_channel_complete & 3) == 0, "fixed_channel_complete call target is not word aligned")
ASSERT((fixed_copy_vap_tcr_fields & 3) == 0, "fixed_copy_vap_tcr_fields call target is not word aligned")
ASSERT(SIZEOF(.fixed_vap_template_align) == 2, "native VAP fallthrough padding must be2B")
ASSERT((ADDR(.fixed_vap_templates) & 3) == 0, "local VAP branches require word alignment")
