#include "test_framework.h"

void test_error_message_table_defines_active_errors(void);
void test_error_item_preserves_compiler_diagnostic_fields(void);
void test_value_ieee754_environment_contract(void);
void test_value_integer_arithmetic_helpers(void);
void test_value_arithmetic_invalid_and_nil_operands(void);
void test_value_integer_overflow_contract(void);
void test_value_local_increment_decrement_boundaries(void);
void test_value_push_int_interprets_i64_immediates(void);
void test_value_push_float_interprets_binary64_payloads(void);
void test_value_float_arithmetic_helpers(void);
void test_value_float_arithmetic_interpreter_bytecode(void);
void test_value_string_concat_helpers(void);
void test_value_string_tracker_releases_through_value_free(void);
void test_value_string_tracker_probe_counts_linear_scans(void);
void test_value_string_tracker_metadata_failures_and_untracked_ownership(void);
void test_value_string_tracker_collision_tombstones_and_growth(void);
void test_value_string_tracker_releases_through_stack_discard(void);
void test_value_string_tracker_forgets_before_reallocation(void);
void test_value_plain_text_formats_nonowning(void);
void test_value_itemref_lifecycle_and_contract(void);
void test_value_string_tracker_itemname_cleanup(void);
void test_value_string_concat_enforces_string_limit(void);
void test_value_string_boundaries_enforce_string_limit(void);
void test_value_bool_nil_truthiness_helpers(void);
void test_stack_peek_returns_top_pointer_without_popping(void);
void test_value_float_construction_copy_truthiness_cleanup(void);
void test_value_float_item_fetch_preserves_bits(void);
void test_value_string_local_load_store_clones(void);
void test_value_comparison_int_helpers(void);
void test_value_comparison_bool_helpers(void);
void test_value_comparison_float_ieee754_helpers(void);
void test_value_comparison_string_helpers(void);
void test_value_comparison_mismatched_type_equality_quirk(void);
void test_value_comparison_unsupported_ordering_is_false(void);
void test_runtime_decode_requires_frame_bounds(void);
void test_interpreter_truncated_single_byte_operands(void);
void test_interpreter_truncated_libcall_pair_preserves_vm_frames(void);
void test_assigncodeitem_rejects_malformed_source_block_with_runtime_bytecode_error(void);
void test_assigncodeitem_rejects_invalid_target_name_type_with_runtime_item_error(void);
void test_strict_runtime_contracts_default_preserves_fetch_argument_drops(void);
void test_value_item_fetch_discards_arguments_and_reports_strict_contract(void);
void test_strict_validation_alone_preserves_fetch_argument_drops(void);
void test_strict_runtime_contracts_reports_too_many_item_arguments(void);
void test_strict_runtime_contracts_reports_multiple_excess_fetch_arguments(void);
void test_strict_runtime_contracts_reports_invalid_item_name_arguments(void);
void test_strict_runtime_contracts_reports_missing_item_arguments(void);
void test_strict_runtime_contracts_uses_context_itemroot(void);
void test_runtime_bytecode_safety_is_mandatory(void);
void test_runtime_bytecode_safety_rejects_null_bytecode(void);
void test_error_item_oom_preserves_existing_diagnostic(void);
void test_compiler_error_item_oom_preserves_existing_diagnostic(void);
void test_error_item_oom_without_previous_diagnostic(void);
void test_compiler_error_item_oom_without_previous_diagnostic(void);
void test_error_item_oom_normalizes_incomplete_diagnostic(void);
void test_compiler_error_item_oom_normalizes_incomplete_diagnostic(void);
void test_clear_error_item_is_allocation_free_and_atomic(void);
void test_error_item_null_inputs_provenance_and_pins(void);

static const TF_TestDescriptor tests[] = {
    {"rewrite.core.test_error_message_table_defines_active_errors",
     test_error_message_table_defines_active_errors, "exclusive", 30000,
     "api.common.errors,baseline.legacy.unified.core.test_error_message_table_defines_active_errors"},
    {"rewrite.core.test_error_item_preserves_compiler_diagnostic_fields",
     test_error_item_preserves_compiler_diagnostic_fields, "exclusive", 30000,
     "api.common.errors,api.compiler.diagnostics,baseline.legacy.unified.core.test_error_item_preserves_compiler_diagnostic_fields"},
    {"rewrite.core.test_value_ieee754_environment_contract", test_value_ieee754_environment_contract, "", 30000,
     "baseline.legacy.unified.core.test_value_ieee754_environment_contract"},
    {"rewrite.core.test_value_integer_arithmetic_helpers", test_value_integer_arithmetic_helpers, "", 30000,
     "baseline.legacy.unified.core.test_value_integer_arithmetic_helpers"},
    {"rewrite.core.test_value_arithmetic_invalid_and_nil_operands", test_value_arithmetic_invalid_and_nil_operands, "", 30000,
     "baseline.legacy.unified.core.test_value_arithmetic_invalid_and_nil_operands"},
    {"rewrite.core.test_value_integer_overflow_contract", test_value_integer_overflow_contract, "", 30000,
     "baseline.legacy.unified.core.test_value_integer_overflow_contract"},
    {"rewrite.core.test_value_local_increment_decrement_boundaries", test_value_local_increment_decrement_boundaries, "", 30000,
     "baseline.legacy.unified.core.test_value_local_increment_decrement_boundaries"},
    {"rewrite.core.test_value_push_int_interprets_i64_immediates", test_value_push_int_interprets_i64_immediates, "", 30000,
     "baseline.legacy.unified.core.test_value_push_int_interprets_i64_immediates"},
    {"rewrite.core.test_value_push_float_interprets_binary64_payloads", test_value_push_float_interprets_binary64_payloads, "", 30000,
     "baseline.legacy.unified.core.test_value_push_float_interprets_binary64_payloads"},
    {"rewrite.core.test_value_float_arithmetic_helpers", test_value_float_arithmetic_helpers, "", 30000,
     "baseline.legacy.unified.core.test_value_float_arithmetic_helpers"},
    {"rewrite.core.test_value_float_arithmetic_interpreter_bytecode", test_value_float_arithmetic_interpreter_bytecode, "exclusive", 30000,
     "baseline.legacy.unified.core.test_value_float_arithmetic_interpreter_bytecode"},
    {"rewrite.core.test_value_string_concat_helpers", test_value_string_concat_helpers, "", 30000,
     "baseline.legacy.unified.core.test_value_string_concat_helpers"},
    {"rewrite.core.test_value_string_tracker_releases_through_value_free", test_value_string_tracker_releases_through_value_free, "exclusive", 30000,
     "baseline.legacy.unified.core.test_value_string_tracker_releases_through_value_free"},
    {"rewrite.core.test_value_string_tracker_probe_counts_linear_scans", test_value_string_tracker_probe_counts_linear_scans, "exclusive", 30000,
     "baseline.legacy.unified.core.test_value_string_tracker_probe_counts_linear_scans"},
    {"rewrite.core.test_value_string_tracker_metadata_failures_and_untracked_ownership", test_value_string_tracker_metadata_failures_and_untracked_ownership, "exclusive", 30000,
     "baseline.legacy.unified.core.test_value_string_tracker_metadata_failures_and_untracked_ownership"},
    {"rewrite.core.test_value_string_tracker_collision_tombstones_and_growth", test_value_string_tracker_collision_tombstones_and_growth, "exclusive", 30000,
     "baseline.legacy.unified.core.test_value_string_tracker_collision_tombstones_and_growth"},
    {"rewrite.core.test_value_string_tracker_releases_through_stack_discard", test_value_string_tracker_releases_through_stack_discard, "exclusive", 30000,
     "baseline.legacy.unified.core.test_value_string_tracker_releases_through_stack_discard"},
    {"rewrite.core.test_value_string_tracker_forgets_before_reallocation", test_value_string_tracker_forgets_before_reallocation, "exclusive", 30000,
     "baseline.legacy.unified.core.test_value_string_tracker_forgets_before_reallocation"},
    {"rewrite.core.test_value_plain_text_formats_nonowning", test_value_plain_text_formats_nonowning, "", 30000,
     "baseline.legacy.unified.core.test_value_plain_text_formats_nonowning"},
    {"rewrite.core.test_value_itemref_lifecycle_and_contract", test_value_itemref_lifecycle_and_contract, "exclusive", 30000,
     "baseline.legacy.unified.core.test_value_itemref_lifecycle_and_contract"},
    {"rewrite.core.test_value_string_tracker_itemname_cleanup", test_value_string_tracker_itemname_cleanup, "exclusive", 30000,
     "baseline.legacy.unified.core.test_value_string_tracker_itemname_cleanup"},
    {"rewrite.core.test_value_string_concat_enforces_string_limit", test_value_string_concat_enforces_string_limit, "", 30000,
     "baseline.legacy.unified.core.test_value_string_concat_enforces_string_limit"},
    {"rewrite.core.test_value_string_boundaries_enforce_string_limit", test_value_string_boundaries_enforce_string_limit, "", 30000,
     "baseline.legacy.unified.core.test_value_string_boundaries_enforce_string_limit"},
    {"rewrite.core.test_value_bool_nil_truthiness_helpers", test_value_bool_nil_truthiness_helpers, "", 30000,
     "baseline.legacy.unified.core.test_value_bool_nil_truthiness_helpers"},
    {"rewrite.core.test_stack_peek_returns_top_pointer_without_popping", test_stack_peek_returns_top_pointer_without_popping, "", 30000,
     "baseline.legacy.unified.core.test_stack_peek_returns_top_pointer_without_popping"},
    {"rewrite.core.test_value_float_construction_copy_truthiness_cleanup", test_value_float_construction_copy_truthiness_cleanup, "", 30000,
     "baseline.legacy.unified.core.test_value_float_construction_copy_truthiness_cleanup"},
    {"rewrite.core.test_value_float_item_fetch_preserves_bits", test_value_float_item_fetch_preserves_bits, "exclusive", 30000,
     "baseline.legacy.unified.core.test_value_float_item_fetch_preserves_bits"},
    {"rewrite.core.test_value_string_local_load_store_clones", test_value_string_local_load_store_clones, "exclusive", 30000,
     "baseline.legacy.unified.core.test_value_string_local_load_store_clones"},
    {"rewrite.core.test_value_comparison_int_helpers", test_value_comparison_int_helpers, "", 30000,
     "baseline.legacy.unified.core.test_value_comparison_int_helpers"},
    {"rewrite.core.test_value_comparison_bool_helpers", test_value_comparison_bool_helpers, "", 30000,
     "baseline.legacy.unified.core.test_value_comparison_bool_helpers"},
    {"rewrite.core.test_value_comparison_float_ieee754_helpers", test_value_comparison_float_ieee754_helpers, "", 30000,
     "baseline.legacy.unified.core.test_value_comparison_float_ieee754_helpers"},
    {"rewrite.core.test_value_comparison_string_helpers", test_value_comparison_string_helpers, "", 30000,
     "baseline.legacy.unified.core.test_value_comparison_string_helpers"},
    {"rewrite.core.test_value_comparison_mismatched_type_equality_quirk", test_value_comparison_mismatched_type_equality_quirk, "", 30000,
     "baseline.legacy.unified.core.test_value_comparison_mismatched_type_equality_quirk"},
    {"rewrite.core.test_value_comparison_unsupported_ordering_is_false", test_value_comparison_unsupported_ordering_is_false, "", 30000,
     "baseline.legacy.unified.core.test_value_comparison_unsupported_ordering_is_false"},
    {"rewrite.runtime.test_runtime_decode_requires_frame_bounds", test_runtime_decode_requires_frame_bounds, "exclusive", 30000,
     "api.runtime.runtime-decode,baseline.legacy.unified.runtime.test_runtime_decode_requires_frame_bounds"},
    {"rewrite.runtime.test_interpreter_truncated_single_byte_operands", test_interpreter_truncated_single_byte_operands, "exclusive", 30000,
     "baseline.legacy.unified.runtime.test_interpreter_truncated_single_byte_operands"},
    {"rewrite.runtime.test_interpreter_truncated_libcall_pair_preserves_vm_frames", test_interpreter_truncated_libcall_pair_preserves_vm_frames, "exclusive", 30000,
     "baseline.legacy.unified.runtime.test_interpreter_truncated_libcall_pair_preserves_vm_frames"},
    {"rewrite.runtime.test_assigncodeitem_rejects_malformed_source_block_with_runtime_bytecode_error", test_assigncodeitem_rejects_malformed_source_block_with_runtime_bytecode_error, "exclusive", 30000,
     "baseline.legacy.unified.runtime.test_assigncodeitem_rejects_malformed_source_block_with_runtime_bytecode_error"},
    {"rewrite.runtime.test_assigncodeitem_rejects_invalid_target_name_type_with_runtime_item_error", test_assigncodeitem_rejects_invalid_target_name_type_with_runtime_item_error, "exclusive", 30000,
     "baseline.legacy.unified.runtime.test_assigncodeitem_rejects_invalid_target_name_type_with_runtime_item_error"},
    {"rewrite.runtime.test_strict_runtime_contracts_default_preserves_fetch_argument_drops", test_strict_runtime_contracts_default_preserves_fetch_argument_drops, "exclusive", 30000,
     "baseline.legacy.unified.runtime.test_strict_runtime_contracts_default_preserves_fetch_argument_drops"},
    {"rewrite.runtime.test_value_item_fetch_discards_arguments_and_reports_strict_contract", test_value_item_fetch_discards_arguments_and_reports_strict_contract, "exclusive", 30000,
     "baseline.legacy.unified.runtime.test_value_item_fetch_discards_arguments_and_reports_strict_contract"},
    {"rewrite.runtime.test_strict_validation_alone_preserves_fetch_argument_drops", test_strict_validation_alone_preserves_fetch_argument_drops, "exclusive", 30000,
     "baseline.legacy.unified.runtime.test_strict_validation_alone_preserves_fetch_argument_drops"},
    {"rewrite.runtime.test_strict_runtime_contracts_reports_too_many_item_arguments", test_strict_runtime_contracts_reports_too_many_item_arguments, "exclusive", 30000,
     "baseline.legacy.unified.runtime.test_strict_runtime_contracts_reports_too_many_item_arguments"},
    {"rewrite.runtime.test_strict_runtime_contracts_reports_multiple_excess_fetch_arguments", test_strict_runtime_contracts_reports_multiple_excess_fetch_arguments, "exclusive", 30000,
     "baseline.legacy.unified.runtime.test_strict_runtime_contracts_reports_multiple_excess_fetch_arguments"},
    {"rewrite.runtime.test_strict_runtime_contracts_reports_invalid_item_name_arguments", test_strict_runtime_contracts_reports_invalid_item_name_arguments, "exclusive", 30000,
     "baseline.legacy.unified.runtime.test_strict_runtime_contracts_reports_invalid_item_name_arguments"},
    {"rewrite.runtime.test_strict_runtime_contracts_reports_missing_item_arguments", test_strict_runtime_contracts_reports_missing_item_arguments, "exclusive", 30000,
     "baseline.legacy.unified.runtime.test_strict_runtime_contracts_reports_missing_item_arguments"},
    {"rewrite.runtime.test_strict_runtime_contracts_uses_context_itemroot", test_strict_runtime_contracts_uses_context_itemroot, "exclusive", 30000,
     "baseline.legacy.unified.runtime.test_strict_runtime_contracts_uses_context_itemroot"},
    {"rewrite.runtime.test_runtime_bytecode_safety_is_mandatory", test_runtime_bytecode_safety_is_mandatory, "exclusive", 30000,
     "api.runtime.opcode-handlers,baseline.legacy.unified.runtime.test_runtime_bytecode_safety_is_mandatory"},
    {"rewrite.runtime.test_runtime_bytecode_safety_rejects_null_bytecode", test_runtime_bytecode_safety_rejects_null_bytecode, "exclusive", 30000,
     "baseline.legacy.unified.runtime.test_runtime_bytecode_safety_rejects_null_bytecode"},
    {"rewrite.runtime.test_error_item_oom_preserves_existing_diagnostic", test_error_item_oom_preserves_existing_diagnostic, "exclusive", 30000,
     "baseline.legacy.unified.runtime.test_error_item_oom_preserves_existing_diagnostic"},
    {"rewrite.runtime.test_compiler_error_item_oom_preserves_existing_diagnostic", test_compiler_error_item_oom_preserves_existing_diagnostic, "exclusive", 30000,
     "baseline.legacy.unified.runtime.test_compiler_error_item_oom_preserves_existing_diagnostic"},
    {"rewrite.runtime.test_error_item_oom_without_previous_diagnostic", test_error_item_oom_without_previous_diagnostic, "exclusive", 30000,
     "baseline.legacy.unified.runtime.test_error_item_oom_without_previous_diagnostic"},
    {"rewrite.runtime.test_compiler_error_item_oom_without_previous_diagnostic", test_compiler_error_item_oom_without_previous_diagnostic, "exclusive", 30000,
     "baseline.legacy.unified.runtime.test_compiler_error_item_oom_without_previous_diagnostic"},
    {"rewrite.runtime.test_error_item_oom_normalizes_incomplete_diagnostic", test_error_item_oom_normalizes_incomplete_diagnostic, "exclusive", 30000,
     "baseline.legacy.unified.runtime.test_error_item_oom_normalizes_incomplete_diagnostic"},
    {"rewrite.runtime.test_compiler_error_item_oom_normalizes_incomplete_diagnostic", test_compiler_error_item_oom_normalizes_incomplete_diagnostic, "exclusive", 30000,
     "baseline.legacy.unified.runtime.test_compiler_error_item_oom_normalizes_incomplete_diagnostic"},
    {"rewrite.runtime.test_clear_error_item_is_allocation_free_and_atomic", test_clear_error_item_is_allocation_free_and_atomic, "exclusive", 30000,
     "baseline.legacy.unified.runtime.test_clear_error_item_is_allocation_free_and_atomic"},
    {"rewrite.runtime.test_error_item_null_inputs_provenance_and_pins", test_error_item_null_inputs_provenance_and_pins, "exclusive", 30000,
     "baseline.legacy.unified.runtime.test_error_item_null_inputs_provenance_and_pins"},
};

int main(int argc, char **argv) {
  return tf_main(argc, argv, tests, sizeof tests / sizeof tests[0]);
}
