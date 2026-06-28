#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "ir.h"
#include "test_assert.h"
#include "test_helpers.h"

typedef void (*test_fn_t)(void);

typedef struct {
  const char *name;
  test_fn_t fn;
} test_case_t;

typedef struct {
  const char *name;
  size_t count;
  double elapsed_ms;
} test_suite_summary_t;

static const char *current_suite_name = "<startup>";
static const char *current_test_name = "<startup>";

const char *test_harness_current_suite(void) { return current_suite_name; }
const char *test_harness_current_test(void) { return current_test_name; }

static double elapsed_ms_since(clock_t start) {
  return ((double)(clock() - start) * 1000.0) / (double)CLOCKS_PER_SEC;
}

/* Shared/core component tests. */
void test_absyn_nested_binary_expressions(void);
void test_absyn_stmtlist_multiple_statements(void);
void test_absyn_if_elsif_else_chain(void);
void test_absyn_item_deref_chains(void);
void test_absyn_float_value_preserves_bits(void);
void test_absyn_malformed_float_valnode_returns_null(void);
void test_sem_check_locals_reusable_context(void);
void test_sem_duplicate_local_keeps_original_index(void);
void test_sem_code_params_are_treated_as_defined_locals(void);
void test_sem_seed_params_duplicate_name_only_marks_target_symbol(void);
void test_sem_code_params_duplicate_after_unrelated_locals_no_param_corruption(void);
void test_sem_embedded_scope_error_detail_includes_provenance(void);
void test_sem_many_locals_deterministic_indices(void);
void test_sem_local_limit_255_is_accepted(void);
void test_sem_local_limit_over_255_fails_deterministically(void);
void test_ir_validate(void);
void test_opcode_schema_consistency(void);
void test_parser_input_api(void);
void test_parser_float_literals_decimal_forms(void);
void test_parser_float_literals_integer_still_int(void);
void test_parser_float_literals_item_layers_unchanged(void);
void test_parser_float_literals_malformed_rejected(void);
void test_floatconv_binary64_edge_cases(void);
void test_floatconv_binary64_formatting(void);
void test_floatconv_binary64_format_roundtrip(void);
void test_fixture_policy_declared_goldens_exist(void);
void test_find_item_cached_hit_and_negative_cache(void);
void test_find_item_cached_invalidation_on_delete_and_reinsert(void);
void test_itemstore_value_and_code_roundtrip(void);
void test_itemstore_nested_depth_roundtrip(void);
void test_itemstore_loads_generated_v1_wire_fixture(void);
void test_load_itemstore_rejects_bad_headers(void);
void test_load_itemstore_rejects_invalid_wire_tags(void);
void test_load_itemstore_rejects_structural_corruption(void);
void test_load_itemstore_rejects_resource_limit_violations(void);
void test_save_itemstore_preserves_existing_file_on_failure(void);
void test_libcall_registry_roundtrip(void);
void test_libcall_registry_init_failure_has_no_partial_state(void);
void test_libcall_registry_lifecycle_reinit_sequence(void);
void test_libcall_registry_repeated_teardown_is_safe(void);
void test_libcall_name_duplicate_detection(void);
void test_missing_libcall_is_null_and_interpret_deterministic(void);
void test_libcall_registry_self_check_invalid_entries(void);
void test_libcall_invalid_arg_branches_return_contracts(void);
void test_net_write_ignores_non_writable_lines(void);
void test_libcall_float_integer_only_arguments_rejected(void);
void test_net_write_formats_float_output(void);
void test_str_libcalls_float_returns_nil_without_error(void);
void test_relative_item_leading_dot_parse_accepts_deref_chain(void);
void test_relative_item_leading_dot_nested_relative_deref_layers(void);
void test_relative_item_leading_dot_nested_deref_nil_or_empty_leading_allowed(void);
void test_relative_item_leading_dot_nil_or_empty_non_leading_rejected(void);
void test_relative_item_leading_dot_boundary_max_name_after_prefix_expansion_compiles(void);
void test_relative_item_leading_dot_existing_absolute_item_unchanged(void);
void test_float_item_literal_layer_rejected_at_compile_time(void);
void test_float_local_deref_layer_returns_nil_and_does_not_save_item(void);
void test_value_ieee754_environment_contract(void);
void test_value_integer_arithmetic_helpers(void);
void test_value_arithmetic_invalid_and_nil_operands(void);
void test_value_push_int_interprets_i64_immediates(void);
void test_value_push_float_interprets_binary64_payloads(void);
void test_value_float_arithmetic_helpers(void);
void test_value_float_arithmetic_interpreter_bytecode(void);
void test_value_string_concat_helpers(void);
void test_value_bool_nil_truthiness_helpers(void);
void test_value_float_construction_copy_truthiness_cleanup(void);
void test_value_string_local_load_store_clones(void);
void test_value_float_item_fetch_preserves_bits(void);
void test_value_comparison_int_helpers(void);
void test_value_comparison_bool_helpers(void);
void test_value_comparison_float_ieee754_helpers(void);
void test_value_comparison_string_helpers(void);
void test_value_comparison_mismatched_type_equality_quirk(void);
void test_value_comparison_unsupported_ordering_is_false(void);
void test_interpreter_truncated_single_byte_operands(void);

/* Compiler component tests. */
void test_emitbc_header(void);
void test_emitbc_opcode_map(void);
void test_emitbc_push_int_immediate_layout(void);
void test_emitbc_push_float_immediate_layout(void);
void test_emitbc_opcode_map_unsupported_ir_op(void);
void test_emitbc_opcode_map_call_item_deref_alias_layout(void);
void test_emitbc_all_ir_ops_accounted_for(void);
void test_emitbc_jumps(void);
void test_emitbc_invariants(void);
void test_pipeline_golden(void);
void test_pipeline_large_local_lookup_duplicate(void);
void test_pipeline_source_golden(void);
void test_pipeline_negative_matrix(void);
void test_parser_examples_obj_golden(void);
void test_sdiss_fixture_basic(void);
void test_sdiss_reads_compiler_operand_widths(void);
void test_compiler_context_failures(void);
void test_compiler_diag_pipeline(void);
void test_sys_compile_libcall_runtime(void);

/* Runtime component tests. */
void test_interpret_semantics_golden(void);
void test_interpret_stress(void);
void test_runtime_benchmark_optin(void);

static const test_case_t core_tests[] = {
    {"test_absyn_nested_binary_expressions", test_absyn_nested_binary_expressions},
    {"test_absyn_stmtlist_multiple_statements", test_absyn_stmtlist_multiple_statements},
    {"test_absyn_if_elsif_else_chain", test_absyn_if_elsif_else_chain},
    {"test_absyn_item_deref_chains", test_absyn_item_deref_chains},
    {"test_absyn_float_value_preserves_bits", test_absyn_float_value_preserves_bits},
    {"test_absyn_malformed_float_valnode_returns_null", test_absyn_malformed_float_valnode_returns_null},
    {"test_sem_check_locals_reusable_context", test_sem_check_locals_reusable_context},
    {"test_sem_duplicate_local_keeps_original_index", test_sem_duplicate_local_keeps_original_index},
    {"test_sem_code_params_are_treated_as_defined_locals", test_sem_code_params_are_treated_as_defined_locals},
    {"test_sem_seed_params_duplicate_name_only_marks_target_symbol", test_sem_seed_params_duplicate_name_only_marks_target_symbol},
    {"test_sem_code_params_duplicate_after_unrelated_locals_no_param_corruption", test_sem_code_params_duplicate_after_unrelated_locals_no_param_corruption},
    {"test_sem_embedded_scope_error_detail_includes_provenance", test_sem_embedded_scope_error_detail_includes_provenance},
    {"test_sem_many_locals_deterministic_indices", test_sem_many_locals_deterministic_indices},
    {"test_sem_local_limit_255_is_accepted", test_sem_local_limit_255_is_accepted},
    {"test_sem_local_limit_over_255_fails_deterministically", test_sem_local_limit_over_255_fails_deterministically},
    {"test_ir_validate", test_ir_validate},
    {"test_opcode_schema_consistency", test_opcode_schema_consistency},
    {"test_parser_input_api", test_parser_input_api},
    {"test_parser_float_literals_decimal_forms", test_parser_float_literals_decimal_forms},
    {"test_parser_float_literals_integer_still_int", test_parser_float_literals_integer_still_int},
    {"test_parser_float_literals_item_layers_unchanged", test_parser_float_literals_item_layers_unchanged},
    {"test_parser_float_literals_malformed_rejected", test_parser_float_literals_malformed_rejected},
    {"test_floatconv_binary64_edge_cases", test_floatconv_binary64_edge_cases},
    {"test_floatconv_binary64_formatting", test_floatconv_binary64_formatting},
    {"test_floatconv_binary64_format_roundtrip", test_floatconv_binary64_format_roundtrip},
    {"test_fixture_policy_declared_goldens_exist", test_fixture_policy_declared_goldens_exist},
    {"test_find_item_cached_hit_and_negative_cache", test_find_item_cached_hit_and_negative_cache},
    {"test_find_item_cached_invalidation_on_delete_and_reinsert", test_find_item_cached_invalidation_on_delete_and_reinsert},
    {"test_itemstore_value_and_code_roundtrip", test_itemstore_value_and_code_roundtrip},
    {"test_itemstore_nested_depth_roundtrip", test_itemstore_nested_depth_roundtrip},
    {"test_itemstore_loads_generated_v1_wire_fixture", test_itemstore_loads_generated_v1_wire_fixture},
    {"test_load_itemstore_rejects_bad_headers", test_load_itemstore_rejects_bad_headers},
    {"test_load_itemstore_rejects_invalid_wire_tags", test_load_itemstore_rejects_invalid_wire_tags},
    {"test_load_itemstore_rejects_structural_corruption", test_load_itemstore_rejects_structural_corruption},
    {"test_load_itemstore_rejects_resource_limit_violations", test_load_itemstore_rejects_resource_limit_violations},
    {"test_save_itemstore_preserves_existing_file_on_failure", test_save_itemstore_preserves_existing_file_on_failure},
    {"test_relative_item_leading_dot_parse_accepts_deref_chain", test_relative_item_leading_dot_parse_accepts_deref_chain},
    {"test_relative_item_leading_dot_nested_relative_deref_layers", test_relative_item_leading_dot_nested_relative_deref_layers},
    {"test_relative_item_leading_dot_nested_deref_nil_or_empty_leading_allowed", test_relative_item_leading_dot_nested_deref_nil_or_empty_leading_allowed},
    {"test_relative_item_leading_dot_nil_or_empty_non_leading_rejected", test_relative_item_leading_dot_nil_or_empty_non_leading_rejected},
    {"test_relative_item_leading_dot_boundary_max_name_after_prefix_expansion_compiles", test_relative_item_leading_dot_boundary_max_name_after_prefix_expansion_compiles},
    {"test_relative_item_leading_dot_existing_absolute_item_unchanged", test_relative_item_leading_dot_existing_absolute_item_unchanged},
    {"test_float_item_literal_layer_rejected_at_compile_time", test_float_item_literal_layer_rejected_at_compile_time},
    {"test_float_local_deref_layer_returns_nil_and_does_not_save_item", test_float_local_deref_layer_returns_nil_and_does_not_save_item},
    {"test_value_ieee754_environment_contract", test_value_ieee754_environment_contract},
    {"test_value_integer_arithmetic_helpers", test_value_integer_arithmetic_helpers},
    {"test_value_arithmetic_invalid_and_nil_operands", test_value_arithmetic_invalid_and_nil_operands},
    {"test_value_push_int_interprets_i64_immediates", test_value_push_int_interprets_i64_immediates},
    {"test_value_push_float_interprets_binary64_payloads", test_value_push_float_interprets_binary64_payloads},
    {"test_value_float_arithmetic_helpers", test_value_float_arithmetic_helpers},
    {"test_value_float_arithmetic_interpreter_bytecode", test_value_float_arithmetic_interpreter_bytecode},
    {"test_value_string_concat_helpers", test_value_string_concat_helpers},
    {"test_value_bool_nil_truthiness_helpers", test_value_bool_nil_truthiness_helpers},
    {"test_value_float_construction_copy_truthiness_cleanup", test_value_float_construction_copy_truthiness_cleanup},
    {"test_value_string_local_load_store_clones", test_value_string_local_load_store_clones},
    {"test_value_float_item_fetch_preserves_bits", test_value_float_item_fetch_preserves_bits},
    {"test_value_comparison_int_helpers", test_value_comparison_int_helpers},
    {"test_value_comparison_bool_helpers", test_value_comparison_bool_helpers},
    {"test_value_comparison_float_ieee754_helpers", test_value_comparison_float_ieee754_helpers},
    {"test_value_comparison_string_helpers", test_value_comparison_string_helpers},
    {"test_value_comparison_mismatched_type_equality_quirk", test_value_comparison_mismatched_type_equality_quirk},
    {"test_value_comparison_unsupported_ordering_is_false", test_value_comparison_unsupported_ordering_is_false},
    {"test_interpreter_truncated_single_byte_operands", test_interpreter_truncated_single_byte_operands},
};

static const test_case_t compiler_tests[] = {
    {"test_emitbc_header", test_emitbc_header},
    {"test_emitbc_opcode_map", test_emitbc_opcode_map},
    {"test_emitbc_push_int_immediate_layout", test_emitbc_push_int_immediate_layout},
    {"test_emitbc_push_float_immediate_layout", test_emitbc_push_float_immediate_layout},
    {"test_emitbc_all_ir_ops_accounted_for", test_emitbc_all_ir_ops_accounted_for},
    {"test_emitbc_opcode_map_call_item_deref_alias_layout", test_emitbc_opcode_map_call_item_deref_alias_layout},
    {"test_emitbc_opcode_map_unsupported_ir_op", test_emitbc_opcode_map_unsupported_ir_op},
    {"test_emitbc_jumps", test_emitbc_jumps},
    {"test_emitbc_invariants", test_emitbc_invariants},
    {"test_pipeline_golden", test_pipeline_golden},
    {"test_pipeline_large_local_lookup_duplicate", test_pipeline_large_local_lookup_duplicate},
    {"test_pipeline_source_golden", test_pipeline_source_golden},
    {"test_pipeline_negative_matrix", test_pipeline_negative_matrix},
    {"test_parser_examples_obj_golden", test_parser_examples_obj_golden},
    {"test_sdiss_fixture_basic", test_sdiss_fixture_basic},
    {"test_sdiss_reads_compiler_operand_widths", test_sdiss_reads_compiler_operand_widths},
    {"test_compiler_context_failures", test_compiler_context_failures},
    {"test_compiler_diag_pipeline", test_compiler_diag_pipeline},
};

static const test_case_t runtime_tests[] = {
    {"test_interpret_semantics_golden", test_interpret_semantics_golden},
    {"test_interpret_stress", test_interpret_stress},
    {"test_libcall_registry_roundtrip", test_libcall_registry_roundtrip},
    {"test_libcall_registry_init_failure_has_no_partial_state", test_libcall_registry_init_failure_has_no_partial_state},
    {"test_libcall_registry_lifecycle_reinit_sequence", test_libcall_registry_lifecycle_reinit_sequence},
    {"test_libcall_registry_repeated_teardown_is_safe", test_libcall_registry_repeated_teardown_is_safe},
    {"test_libcall_name_duplicate_detection", test_libcall_name_duplicate_detection},
    {"test_missing_libcall_is_null_and_interpret_deterministic", test_missing_libcall_is_null_and_interpret_deterministic},
    {"test_libcall_registry_self_check_invalid_entries", test_libcall_registry_self_check_invalid_entries},
    {"test_libcall_invalid_arg_branches_return_contracts", test_libcall_invalid_arg_branches_return_contracts},
    {"test_net_write_ignores_non_writable_lines", test_net_write_ignores_non_writable_lines},
    {"test_libcall_float_integer_only_arguments_rejected", test_libcall_float_integer_only_arguments_rejected},
    {"test_str_libcalls_float_returns_nil_without_error", test_str_libcalls_float_returns_nil_without_error},
    {"test_net_write_formats_float_output", test_net_write_formats_float_output},
    {"test_sys_compile_libcall_runtime", test_sys_compile_libcall_runtime},
    {"test_runtime_benchmark_optin", test_runtime_benchmark_optin},
};

static void harness_printf(const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  vprintf(fmt, args);
  va_end(args);
  fflush(stdout);
}

static int env_flag_enabled(const char *name) {
  const char *value = getenv(name);
  if (value == NULL) {
    return 0;
  }
  return strcmp(value, "1") == 0 || strcmp(value, "true") == 0 || strcmp(value, "TRUE") == 0;
}

static test_suite_summary_t run_suite(const char *suite_name, const test_case_t *cases, size_t count) {
  test_suite_summary_t summary = {suite_name, 0, 0.0};
  clock_t suite_start = clock();
  harness_printf("\n[test-harness][%s][START] total=%zu\n", suite_name, count);
  for (size_t i = 0; i < count; ++i) {
    current_suite_name = suite_name;
    current_test_name = cases[i].name;
    clock_t test_start = clock();
    harness_printf("[test-harness][%s][START] index=%zu/%zu test=%s\n", suite_name, i + 1,
                   count, cases[i].name);
    cases[i].fn();
    harness_printf("[test-harness][%s][PASS] index=%zu/%zu test=%s elapsed_ms=%.2f\n", suite_name, i + 1,
                   count, cases[i].name, elapsed_ms_since(test_start));
    summary.count++;
  }
  summary.elapsed_ms = elapsed_ms_since(suite_start);
  current_suite_name = "<idle>";
  current_test_name = "<idle>";
  harness_printf("[test-harness][%s][COMPLETE] executed=%zu expected=%zu status=PASS elapsed_ms=%.2f\n",
                 suite_name, summary.count, count, summary.elapsed_ms);
  return summary;
}

int main(void) {
  test_suite_summary_t suites[] = {
      {"core", sizeof(core_tests) / sizeof(core_tests[0])},
      {"compiler", sizeof(compiler_tests) / sizeof(compiler_tests[0])},
      {"runtime", sizeof(runtime_tests) / sizeof(runtime_tests[0])},
  };
  const size_t suite_count = sizeof(suites) / sizeof(suites[0]);
  const int strict_bench = env_flag_enabled("SIN_STRICT_BENCH");

  size_t total_expected = 0;
  for (size_t i = 0; i < suite_count; ++i) {
    total_expected += suites[i].count;
  }

  harness_printf("[test-harness] mode=%s strict_bench=%s suites=%zu expected_tests=%zu\n",
                 strict_bench ? "strict" : "standard",
                 strict_bench ? "enabled" : "disabled", suite_count, total_expected);

  suites[0] = run_suite("core", core_tests, sizeof(core_tests) / sizeof(core_tests[0]));
  suites[1] = run_suite("compiler", compiler_tests,
                        sizeof(compiler_tests) / sizeof(compiler_tests[0]));
  suites[2] = run_suite("runtime", runtime_tests,
                        sizeof(runtime_tests) / sizeof(runtime_tests[0]));

  size_t total_ran = 0;
  for (size_t i = 0; i < suite_count; ++i) {
    total_ran += suites[i].count;
  }

  harness_printf("\n[test-harness] summary: mode=%s strict_bench=%s status=PASS suites=%zu "
                 "tests=%zu/%zu\n",
                 strict_bench ? "strict" : "standard",
                 strict_bench ? "enabled" : "disabled", suite_count, total_ran,
                 total_expected);
  harness_printf("[test-harness] suite summary:");
  for (size_t i = 0; i < suite_count; ++i) {
    harness_printf(" %s=%zu(%.2fms)", suites[i].name, suites[i].count, suites[i].elapsed_ms);
  }
  harness_printf("\n");
  return 0;
}
