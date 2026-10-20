/* Exact lower-copy owned instruction spans. */
INCLUDE fixed-power-symbols-lm1.ld
SECTIONS
{
 .fixed_su_entry 0x133724 : { *(.fixed_su_entry) }
 .fixed_su_log_return 0x1337a8 : { *(.fixed_su_log_return) }
 .fixed_recalc_gate 0x1337b0 : { *(.fixed_recalc_gate) }
 .fixed_peer_bounds_log_return 0x13baf6 : { *(.fixed_peer_bounds_log_return) }
 .fixed_reset_guard 0x13baf8 : { *(.fixed_reset_guard) }
 .fixed_clamp_log_return 0x13bba0 : { *(.fixed_clamp_log_return) }
 .fixed_su_dispatch 0x13bba4 : { *(.fixed_su_dispatch) }
 .fixed_native_bounds 0x13bbb4 : { *(.fixed_native_bounds) }
 .fixed_mask_value 0x13bcb0 : { *(.fixed_mask_value) }
 .fixed_calibration_log_skip 0x13fad6 : { *(.fixed_calibration_log_skip) }
 .fixed_cal_automatic 0x13fadc : { *(.fixed_cal_automatic) }
 .fixed_vap_callback_pointer 0x13fc60 : { *(.fixed_vap_callback_pointer) }
 .fixed_vap_context 0x13fc6e : { *(.fixed_vap_context) }
 .fixed_vap_template_align 0x13fc8a : { *(.fixed_vap_template_align) }
 .fixed_vap_templates 0x13fc8c : { *(.fixed_vap_templates) }
 .fixed_query_error 0x13fcc8 : { *(.fixed_query_error) }
 .fixed_reset_hook 0x150e12 : { *(.fixed_reset_hook) }
 .fixed_calibration_hook 0x1512ec : { *(.fixed_calibration_hook) }
 .fixed_completion_hook 0x151318 : { *(.fixed_completion_hook) }
 .fixed_lower_entry 0x151434 : { *(.fixed_lower_entry) }
 .fixed_recalc_hook 0x152582 : { *(.fixed_recalc_hook) }
 .fixed_feedback_log_skip 0x1525aa : { *(.fixed_feedback_log_skip) }
 .fixed_group_native 0x1525b0 : { *(.fixed_group_native) }
 .fixed_group_entry 0x157744 : { *(.fixed_group_entry) }
 .fixed_lower_complete 0x157748 : { *(.fixed_lower_complete) }
 .fixed_group_mgmt_copy 0x157770 : { *(.fixed_group_mgmt_copy) }
 .fixed_group_refresh 0x157790 : { *(.fixed_group_refresh) }
 .fixed_group_aux_copy 0x157882 : { *(.fixed_group_aux_copy) }
 .fixed_vap_value 0x1578a0 : { *(.fixed_vap_value) }
 .fixed_su_mode 0x157900 : { *(.fixed_su_mode) }
 .fixed_dl_dump_retire 0x157dda : { *(.fixed_dl_dump_retire) }
 .fixed_lower_stage 0x157ddc : { *(.fixed_lower_stage) }
 .fixed_vap_callback 0x157e8c : { *(.fixed_vap_callback) }
 .fixed_query 0x158ad4 : { *(.fixed_query) }
 .fixed_su_value 0x158b1c : { *(.fixed_su_value) }
 .fixed_group_dispatch 0x158b58 : { *(.fixed_group_dispatch) }
 .fixed_group_rate_log_a 0x15a652 : { *(.fixed_group_rate_log_a) }
 .fixed_group_active 0x15a658 : { *(.fixed_group_active) }
 .fixed_group_rate_log_b 0x15a68a : { *(.fixed_group_rate_log_b) }
 .fixed_cal_unrepresentable 0x15a690 : { *(.fixed_cal_unrepresentable) }
 .fixed_group_rate_log_c 0x15a6c0 : { *(.fixed_group_rate_log_c) }
 .fixed_lower_dispatch 0x15a6c4 : { *(.fixed_lower_dispatch) }
 .fixed_group_rate_log_d 0x15a716 : { *(.fixed_group_rate_log_d) }
 .fixed_channel_complete 0x15a71c : { *(.fixed_channel_complete) }
 .fixed_ul_dump_retire 0x15a89e : { *(.fixed_ul_dump_retire) }
 .fixed_after_calibration 0x15a8a0 : { *(.fixed_after_calibration) }
 .fixed_cap 0x15a920 : { *(.fixed_cap) }
 .fixed_lower_version 0x15ed24 : { *(.fixed_lower_version) }
 .fixed_group_value 0x15ed58 : { *(.fixed_group_value) }
 .fixed_copy_vap_tcr_fields 0x15eda8 : { *(.fixed_copy_vap_tcr_fields) }
 /DISCARD/ : { *(.text) *(.data) *(.bss) *(.ARC.attributes) }
}
ASSERT(SIZEOF(.fixed_lower_entry) > 0 && SIZEOF(.fixed_lower_entry) <= 6, "fixed_lower_entry exceeds its owned span")
ASSERT(ADDR(.fixed_lower_entry) + SIZEOF(.fixed_lower_entry) <= 0x15143a, "fixed_lower_entry end crossed")
ASSERT(SIZEOF(.fixed_su_entry) > 0 && SIZEOF(.fixed_su_entry) <= 4, "fixed_su_entry exceeds its owned span")
ASSERT(ADDR(.fixed_su_entry) + SIZEOF(.fixed_su_entry) <= 0x133728, "fixed_su_entry end crossed")
ASSERT(SIZEOF(.fixed_group_entry) > 0 && SIZEOF(.fixed_group_entry) <= 4, "fixed_group_entry exceeds its owned span")
ASSERT(ADDR(.fixed_group_entry) + SIZEOF(.fixed_group_entry) <= 0x157748, "fixed_group_entry end crossed")
ASSERT(SIZEOF(.fixed_calibration_hook) > 0 && SIZEOF(.fixed_calibration_hook) <= 6, "fixed_calibration_hook exceeds its owned span")
ASSERT(ADDR(.fixed_calibration_hook) + SIZEOF(.fixed_calibration_hook) <= 0x1512f2, "fixed_calibration_hook end crossed")
ASSERT(SIZEOF(.fixed_completion_hook) > 0 && SIZEOF(.fixed_completion_hook) <= 6, "fixed_completion_hook exceeds its owned span")
ASSERT(ADDR(.fixed_completion_hook) + SIZEOF(.fixed_completion_hook) <= 0x15131e, "fixed_completion_hook end crossed")
ASSERT(SIZEOF(.fixed_vap_context) > 0 && SIZEOF(.fixed_vap_context) <= 4, "fixed_vap_context exceeds its owned span")
ASSERT(ADDR(.fixed_vap_context) + SIZEOF(.fixed_vap_context) <= 0x13fc72, "fixed_vap_context end crossed")
ASSERT(SIZEOF(.fixed_vap_callback_pointer) > 0 && SIZEOF(.fixed_vap_callback_pointer) <= 6, "fixed_vap_callback_pointer exceeds its owned span")
ASSERT(ADDR(.fixed_vap_callback_pointer) + SIZEOF(.fixed_vap_callback_pointer) <= 0x13fc66, "fixed_vap_callback_pointer end crossed")
ASSERT(SIZEOF(.fixed_reset_hook) > 0 && SIZEOF(.fixed_reset_hook) <= 6, "fixed_reset_hook exceeds its owned span")
ASSERT(ADDR(.fixed_reset_hook) + SIZEOF(.fixed_reset_hook) <= 0x150e18, "fixed_reset_hook end crossed")
ASSERT(SIZEOF(.fixed_recalc_hook) > 0 && SIZEOF(.fixed_recalc_hook) <= 4, "fixed_recalc_hook exceeds its owned span")
ASSERT(ADDR(.fixed_recalc_hook) + SIZEOF(.fixed_recalc_hook) <= 0x152586, "fixed_recalc_hook end crossed")
ASSERT(SIZEOF(.fixed_native_bounds) > 0 && SIZEOF(.fixed_native_bounds) <= 252, "fixed_native_bounds exceeds its owned span")
ASSERT(ADDR(.fixed_native_bounds) + SIZEOF(.fixed_native_bounds) <= 0x13bcb0, "fixed_native_bounds end crossed")
ASSERT(SIZEOF(.fixed_mask_value) > 0 && SIZEOF(.fixed_mask_value) <= 44, "fixed_mask_value exceeds its owned span")
ASSERT(ADDR(.fixed_mask_value) + SIZEOF(.fixed_mask_value) <= 0x13bcdc, "fixed_mask_value end crossed")
ASSERT(SIZEOF(.fixed_lower_version) > 0 && SIZEOF(.fixed_lower_version) <= 52, "fixed_lower_version exceeds its owned span")
ASSERT(ADDR(.fixed_lower_version) + SIZEOF(.fixed_lower_version) <= 0x15ed58, "fixed_lower_version end crossed")
ASSERT(SIZEOF(.fixed_group_value) > 0 && SIZEOF(.fixed_group_value) <= 80, "fixed_group_value exceeds its owned span")
ASSERT(ADDR(.fixed_group_value) + SIZEOF(.fixed_group_value) <= 0x15eda8, "fixed_group_value end crossed")
ASSERT(SIZEOF(.fixed_copy_vap_tcr_fields) > 0 && SIZEOF(.fixed_copy_vap_tcr_fields) <= 39, "fixed_copy_vap_tcr_fields exceeds its owned span")
ASSERT(ADDR(.fixed_copy_vap_tcr_fields) + SIZEOF(.fixed_copy_vap_tcr_fields) <= 0x15edcf, "fixed_copy_vap_tcr_fields end crossed")
ASSERT(SIZEOF(.fixed_dl_dump_retire) > 0 && SIZEOF(.fixed_dl_dump_retire) <= 2, "fixed_dl_dump_retire exceeds its owned span")
ASSERT(ADDR(.fixed_dl_dump_retire) + SIZEOF(.fixed_dl_dump_retire) <= 0x157ddc, "fixed_dl_dump_retire end crossed")
ASSERT(SIZEOF(.fixed_lower_stage) > 0 && SIZEOF(.fixed_lower_stage) <= 176, "fixed_lower_stage exceeds its owned span")
ASSERT(ADDR(.fixed_lower_stage) + SIZEOF(.fixed_lower_stage) <= 0x157e8c, "fixed_lower_stage end crossed")
ASSERT(SIZEOF(.fixed_vap_callback) > 0 && SIZEOF(.fixed_vap_callback) <= 88, "fixed_vap_callback exceeds its owned span")
ASSERT(ADDR(.fixed_vap_callback) + SIZEOF(.fixed_vap_callback) <= 0x157ee4, "fixed_vap_callback end crossed")
ASSERT(SIZEOF(.fixed_ul_dump_retire) > 0 && SIZEOF(.fixed_ul_dump_retire) <= 2, "fixed_ul_dump_retire exceeds its owned span")
ASSERT(ADDR(.fixed_ul_dump_retire) + SIZEOF(.fixed_ul_dump_retire) <= 0x15a8a0, "fixed_ul_dump_retire end crossed")
ASSERT(SIZEOF(.fixed_after_calibration) > 0 && SIZEOF(.fixed_after_calibration) <= 128, "fixed_after_calibration exceeds its owned span")
ASSERT(ADDR(.fixed_after_calibration) + SIZEOF(.fixed_after_calibration) <= 0x15a920, "fixed_after_calibration end crossed")
ASSERT(SIZEOF(.fixed_cap) > 0 && SIZEOF(.fixed_cap) <= 128, "fixed_cap exceeds its owned span")
ASSERT(ADDR(.fixed_cap) + SIZEOF(.fixed_cap) <= 0x15a9a0, "fixed_cap end crossed")
ASSERT(SIZEOF(.fixed_query) > 0 && SIZEOF(.fixed_query) <= 72, "fixed_query exceeds its owned span")
ASSERT(ADDR(.fixed_query) + SIZEOF(.fixed_query) <= 0x158b1c, "fixed_query end crossed")
ASSERT(SIZEOF(.fixed_su_value) > 0 && SIZEOF(.fixed_su_value) <= 60, "fixed_su_value exceeds its owned span")
ASSERT(ADDR(.fixed_su_value) + SIZEOF(.fixed_su_value) <= 0x158b58, "fixed_su_value end crossed")
ASSERT(SIZEOF(.fixed_group_dispatch) > 0 && SIZEOF(.fixed_group_dispatch) <= 24, "fixed_group_dispatch exceeds its owned span")
ASSERT(ADDR(.fixed_group_dispatch) + SIZEOF(.fixed_group_dispatch) <= 0x158b70, "fixed_group_dispatch end crossed")
ASSERT(SIZEOF(.fixed_group_mgmt_copy) > 0 && SIZEOF(.fixed_group_mgmt_copy) <= 32, "fixed_group_mgmt_copy exceeds its owned span")
ASSERT(ADDR(.fixed_group_mgmt_copy) + SIZEOF(.fixed_group_mgmt_copy) <= 0x157790, "fixed_group_mgmt_copy end crossed")
ASSERT(SIZEOF(.fixed_group_refresh) > 0 && SIZEOF(.fixed_group_refresh) <= 116, "fixed_group_refresh exceeds its owned span")
ASSERT(ADDR(.fixed_group_refresh) + SIZEOF(.fixed_group_refresh) <= 0x157804, "fixed_group_refresh end crossed")
ASSERT(SIZEOF(.fixed_group_aux_copy) > 0 && SIZEOF(.fixed_group_aux_copy) <= 30, "fixed_group_aux_copy exceeds its owned span")
ASSERT(ADDR(.fixed_group_aux_copy) + SIZEOF(.fixed_group_aux_copy) <= 0x1578a0, "fixed_group_aux_copy end crossed")
ASSERT(SIZEOF(.fixed_vap_value) > 0 && SIZEOF(.fixed_vap_value) <= 96, "fixed_vap_value exceeds its owned span")
ASSERT(ADDR(.fixed_vap_value) + SIZEOF(.fixed_vap_value) <= 0x157900, "fixed_vap_value end crossed")
ASSERT(SIZEOF(.fixed_su_mode) > 0 && SIZEOF(.fixed_su_mode) <= 22, "fixed_su_mode exceeds its owned span")
ASSERT(ADDR(.fixed_su_mode) + SIZEOF(.fixed_su_mode) <= 0x157916, "fixed_su_mode end crossed")
ASSERT(SIZEOF(.fixed_vap_templates) > 0 && SIZEOF(.fixed_vap_templates) <= 60, "fixed_vap_templates exceeds its owned span")
ASSERT(ADDR(.fixed_vap_templates) + SIZEOF(.fixed_vap_templates) <= 0x13fcc8, "fixed_vap_templates end crossed")
ASSERT(SIZEOF(.fixed_query_error) > 0 && SIZEOF(.fixed_query_error) <= 10, "fixed_query_error exceeds its owned span")
ASSERT(ADDR(.fixed_query_error) + SIZEOF(.fixed_query_error) <= 0x13fcd2, "fixed_query_error end crossed")
ASSERT(SIZEOF(.fixed_su_log_return) > 0 && SIZEOF(.fixed_su_log_return) <= 8, "fixed_su_log_return exceeds its owned span")
ASSERT(ADDR(.fixed_su_log_return) + SIZEOF(.fixed_su_log_return) <= 0x1337b0, "fixed_su_log_return end crossed")
ASSERT(SIZEOF(.fixed_recalc_gate) > 0 && SIZEOF(.fixed_recalc_gate) <= 34, "fixed_recalc_gate exceeds its owned span")
ASSERT(ADDR(.fixed_recalc_gate) + SIZEOF(.fixed_recalc_gate) <= 0x1337d2, "fixed_recalc_gate end crossed")
ASSERT(SIZEOF(.fixed_clamp_log_return) > 0 && SIZEOF(.fixed_clamp_log_return) <= 4, "fixed_clamp_log_return exceeds its owned span")
ASSERT(ADDR(.fixed_clamp_log_return) + SIZEOF(.fixed_clamp_log_return) <= 0x13bba4, "fixed_clamp_log_return end crossed")
ASSERT(SIZEOF(.fixed_su_dispatch) > 0 && SIZEOF(.fixed_su_dispatch) <= 16, "fixed_su_dispatch exceeds its owned span")
ASSERT(ADDR(.fixed_su_dispatch) + SIZEOF(.fixed_su_dispatch) <= 0x13bbb4, "fixed_su_dispatch end crossed")
ASSERT(SIZEOF(.fixed_calibration_log_skip) > 0 && SIZEOF(.fixed_calibration_log_skip) <= 6, "fixed_calibration_log_skip exceeds its owned span")
ASSERT(ADDR(.fixed_calibration_log_skip) + SIZEOF(.fixed_calibration_log_skip) <= 0x13fadc, "fixed_calibration_log_skip end crossed")
ASSERT(SIZEOF(.fixed_cal_automatic) > 0 && SIZEOF(.fixed_cal_automatic) <= 16, "fixed_cal_automatic exceeds its owned span")
ASSERT(ADDR(.fixed_cal_automatic) + SIZEOF(.fixed_cal_automatic) <= 0x13faec, "fixed_cal_automatic end crossed")
ASSERT(SIZEOF(.fixed_peer_bounds_log_return) > 0 && SIZEOF(.fixed_peer_bounds_log_return) <= 2, "fixed_peer_bounds_log_return exceeds its owned span")
ASSERT(ADDR(.fixed_peer_bounds_log_return) + SIZEOF(.fixed_peer_bounds_log_return) <= 0x13baf8, "fixed_peer_bounds_log_return end crossed")
ASSERT(SIZEOF(.fixed_reset_guard) > 0 && SIZEOF(.fixed_reset_guard) <= 44, "fixed_reset_guard exceeds its owned span")
ASSERT(ADDR(.fixed_reset_guard) + SIZEOF(.fixed_reset_guard) <= 0x13bb24, "fixed_reset_guard end crossed")
ASSERT(SIZEOF(.fixed_group_rate_log_a) > 0 && SIZEOF(.fixed_group_rate_log_a) <= 6, "fixed_group_rate_log_a exceeds its owned span")
ASSERT(ADDR(.fixed_group_rate_log_a) + SIZEOF(.fixed_group_rate_log_a) <= 0x15a658, "fixed_group_rate_log_a end crossed")
ASSERT(SIZEOF(.fixed_group_active) > 0 && SIZEOF(.fixed_group_active) <= 12, "fixed_group_active exceeds its owned span")
ASSERT(ADDR(.fixed_group_active) + SIZEOF(.fixed_group_active) <= 0x15a664, "fixed_group_active end crossed")
ASSERT(SIZEOF(.fixed_group_rate_log_b) > 0 && SIZEOF(.fixed_group_rate_log_b) <= 6, "fixed_group_rate_log_b exceeds its owned span")
ASSERT(ADDR(.fixed_group_rate_log_b) + SIZEOF(.fixed_group_rate_log_b) <= 0x15a690, "fixed_group_rate_log_b end crossed")
ASSERT(SIZEOF(.fixed_cal_unrepresentable) > 0 && SIZEOF(.fixed_cal_unrepresentable) <= 12, "fixed_cal_unrepresentable exceeds its owned span")
ASSERT(ADDR(.fixed_cal_unrepresentable) + SIZEOF(.fixed_cal_unrepresentable) <= 0x15a69c, "fixed_cal_unrepresentable end crossed")
ASSERT(SIZEOF(.fixed_group_rate_log_c) > 0 && SIZEOF(.fixed_group_rate_log_c) <= 4, "fixed_group_rate_log_c exceeds its owned span")
ASSERT(ADDR(.fixed_group_rate_log_c) + SIZEOF(.fixed_group_rate_log_c) <= 0x15a6c4, "fixed_group_rate_log_c end crossed")
ASSERT(SIZEOF(.fixed_lower_dispatch) > 0 && SIZEOF(.fixed_lower_dispatch) <= 20, "fixed_lower_dispatch exceeds its owned span")
ASSERT(ADDR(.fixed_lower_dispatch) + SIZEOF(.fixed_lower_dispatch) <= 0x15a6d8, "fixed_lower_dispatch end crossed")
ASSERT(SIZEOF(.fixed_group_rate_log_d) > 0 && SIZEOF(.fixed_group_rate_log_d) <= 6, "fixed_group_rate_log_d exceeds its owned span")
ASSERT(ADDR(.fixed_group_rate_log_d) + SIZEOF(.fixed_group_rate_log_d) <= 0x15a71c, "fixed_group_rate_log_d end crossed")
ASSERT(SIZEOF(.fixed_channel_complete) > 0 && SIZEOF(.fixed_channel_complete) <= 12, "fixed_channel_complete exceeds its owned span")
ASSERT(ADDR(.fixed_channel_complete) + SIZEOF(.fixed_channel_complete) <= 0x15a728, "fixed_channel_complete end crossed")
ASSERT(SIZEOF(.fixed_lower_complete) > 0 && SIZEOF(.fixed_lower_complete) <= 20, "fixed_lower_complete exceeds its owned span")
ASSERT(ADDR(.fixed_lower_complete) + SIZEOF(.fixed_lower_complete) <= 0x15775c, "fixed_lower_complete end crossed")
ASSERT(SIZEOF(.fixed_feedback_log_skip) > 0 && SIZEOF(.fixed_feedback_log_skip) <= 6, "fixed_feedback_log_skip exceeds its owned span")
ASSERT(ADDR(.fixed_feedback_log_skip) + SIZEOF(.fixed_feedback_log_skip) <= 0x1525b0, "fixed_feedback_log_skip end crossed")
ASSERT(SIZEOF(.fixed_group_native) > 0 && SIZEOF(.fixed_group_native) <= 10, "fixed_group_native exceeds its owned span")
ASSERT(ADDR(.fixed_group_native) + SIZEOF(.fixed_group_native) <= 0x1525ba, "fixed_group_native end crossed")
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
