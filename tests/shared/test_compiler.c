#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
} test_suite_summary_t;

/* Shared/core component tests. */
void test_absyn_nested_binary_expressions(void);
void test_absyn_stmtlist_multiple_statements(void);
void test_absyn_if_elsif_else_chain(void);
void test_absyn_item_deref_chains(void);
void test_sem_check_locals_reusable_context(void);
void test_sem_duplicate_local_keeps_original_index(void);
void test_sem_code_params_are_treated_as_defined_locals(void);
void test_sem_seed_params_duplicate_name_only_marks_target_symbol(void);
void test_sem_code_params_duplicate_after_unrelated_locals_no_param_corruption(void);
void test_sem_parent_scope_error_detail_format(void);
void test_sem_embedded_scope_error_detail_includes_provenance(void);
void test_sem_many_locals_deterministic_indices(void);
void test_sem_local_limit_255_is_accepted(void);
void test_sem_local_limit_over_255_fails_deterministically(void);
void test_ir_validate(void);
void test_opcode_schema_consistency(void);
void test_parser_input_api(void);
void test_fixture_policy_declared_goldens_exist(void);
void test_find_item_cached_hit_and_negative_cache(void);
void test_find_item_cached_invalidation_on_delete_and_reinsert(void);
void test_libcall_registry_roundtrip(void);
void test_libcall_registry_init_failure_has_no_partial_state(void);
void test_libcall_registry_lifecycle_reinit_sequence(void);
void test_libcall_registry_repeated_teardown_is_safe(void);
void test_libcall_name_duplicate_detection(void);
void test_missing_libcall_is_null_and_interpret_deterministic(void);
void test_libcall_registry_self_check_invalid_entries(void);
void test_libcall_invalid_arg_branches_return_contracts(void);
void test_net_write_ignores_disconnected_lines(void);
void test_net_write_ignores_non_writable_line_states(void);
void test_relative_item_leading_dot_parse_accepts_deref_chain(void);
void test_relative_item_leading_dot_nested_relative_deref_layers(void);
void test_relative_item_leading_dot_nested_deref_nil_or_empty_leading_allowed(void);
void test_relative_item_leading_dot_nil_or_empty_non_leading_rejected(void);
void test_relative_item_leading_dot_boundary_max_name_after_prefix_expansion_compiles(void);
void test_relative_item_leading_dot_existing_absolute_item_unchanged(void);
void test_relative_item_leading_dot_parse_still_rejects_bad_double_dot(void);
void test_value_integer_arithmetic_helpers(void);
void test_value_string_concat_helpers(void);
void test_value_bool_nil_truthiness_helpers(void);
void test_value_string_local_load_store_clones(void);

/* Compiler component tests. */
void test_emitbc_header(void);
void test_emitbc_opcode_map(void);
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
void test_libcall_lookup_precomputed(void);
void test_libcall_dispatch_microbench(void);

/* Runtime component tests. */
void test_interpret_semantics_golden(void);
void test_interpret_stress(void);
void test_runtime_benchmark_optin(void);

static void test_absyn_helpers(void) {
  AS_NODE *lhs = t_int(1);
  AS_NODE *rhs = t_int(2);
  AS_NODE *sum = t_node(N_ADD, lhs, rhs);
  AS_NODE *stmt = t_node(N_EXPRSTMT, sum, NULL);
  AS_NODE *list = t_stmtlist_with_one(stmt);

  ASSERT_NOT_NULL(list);
  ASSERT_EQ_INT(N_STMTLIST, list->nodetype);
  ASSERT_TRUE(((AS_STMTLIST *)list->lhs)->count == 1);

  as_delete(list);
}

static void test_ir_and_emitbc_helpers(void) {
  IR_Unit *unit = t_new_unit();
  ASSERT_NOT_NULL(unit);

  int32_t done = ir_new_label(unit);
  t_emit(unit, (IR_Inst){.op = IR_OP_PUSH_INT, .imm = 7});
  t_emit(unit, (IR_Inst){.op = IR_OP_JUMP, .a = done});
  t_bind(unit, done);
  t_emit(unit, (IR_Inst){.op = IR_OP_LABEL, .a = done});
  t_emit(unit, (IR_Inst){.op = IR_OP_HALT});

  OUTPUT_t out = {0};
  out.maxsize = 64;
  out.bytecode = malloc(out.maxsize);
  out.nextbyte = out.bytecode;
  ASSERT_NOT_NULL(out.bytecode);

  char *errdetail = NULL;
  int8_t rc = t_emit_bytecode(unit, 0, 0, &out, &errdetail);
  ASSERT_EQ_INT(0, rc);
  ASSERT_TRUE(errdetail == NULL);
  ASSERT_TRUE((size_t)(out.nextbyte - out.bytecode) > 0);

  free(out.bytecode);
  ir_destroy_unit(unit);
}

static const test_case_t core_tests[] = {
    {"test_absyn_helpers", test_absyn_helpers},
    {"test_ir_and_emitbc_helpers", test_ir_and_emitbc_helpers},
    {"test_absyn_nested_binary_expressions", test_absyn_nested_binary_expressions},
    {"test_absyn_stmtlist_multiple_statements", test_absyn_stmtlist_multiple_statements},
    {"test_absyn_if_elsif_else_chain", test_absyn_if_elsif_else_chain},
    {"test_absyn_item_deref_chains", test_absyn_item_deref_chains},
    {"test_sem_check_locals_reusable_context", test_sem_check_locals_reusable_context},
    {"test_sem_duplicate_local_keeps_original_index", test_sem_duplicate_local_keeps_original_index},
    {"test_sem_code_params_are_treated_as_defined_locals", test_sem_code_params_are_treated_as_defined_locals},
    {"test_sem_seed_params_duplicate_name_only_marks_target_symbol", test_sem_seed_params_duplicate_name_only_marks_target_symbol},
    {"test_sem_code_params_duplicate_after_unrelated_locals_no_param_corruption", test_sem_code_params_duplicate_after_unrelated_locals_no_param_corruption},
    {"test_sem_parent_scope_error_detail_format", test_sem_parent_scope_error_detail_format},
    {"test_sem_embedded_scope_error_detail_includes_provenance", test_sem_embedded_scope_error_detail_includes_provenance},
    {"test_sem_many_locals_deterministic_indices", test_sem_many_locals_deterministic_indices},
    {"test_sem_local_limit_255_is_accepted", test_sem_local_limit_255_is_accepted},
    {"test_sem_local_limit_over_255_fails_deterministically", test_sem_local_limit_over_255_fails_deterministically},
    {"test_ir_validate", test_ir_validate},
    {"test_opcode_schema_consistency", test_opcode_schema_consistency},
    {"test_parser_input_api", test_parser_input_api},
    {"test_fixture_policy_declared_goldens_exist", test_fixture_policy_declared_goldens_exist},
    {"test_find_item_cached_hit_and_negative_cache", test_find_item_cached_hit_and_negative_cache},
    {"test_find_item_cached_invalidation_on_delete_and_reinsert", test_find_item_cached_invalidation_on_delete_and_reinsert},
    {"test_relative_item_leading_dot_parse_accepts_deref_chain", test_relative_item_leading_dot_parse_accepts_deref_chain},
    {"test_relative_item_leading_dot_nested_relative_deref_layers", test_relative_item_leading_dot_nested_relative_deref_layers},
    {"test_relative_item_leading_dot_nested_deref_nil_or_empty_leading_allowed", test_relative_item_leading_dot_nested_deref_nil_or_empty_leading_allowed},
    {"test_relative_item_leading_dot_nil_or_empty_non_leading_rejected", test_relative_item_leading_dot_nil_or_empty_non_leading_rejected},
    {"test_relative_item_leading_dot_boundary_max_name_after_prefix_expansion_compiles", test_relative_item_leading_dot_boundary_max_name_after_prefix_expansion_compiles},
    {"test_relative_item_leading_dot_existing_absolute_item_unchanged", test_relative_item_leading_dot_existing_absolute_item_unchanged},
    {"test_relative_item_leading_dot_parse_still_rejects_bad_double_dot", test_relative_item_leading_dot_parse_still_rejects_bad_double_dot},
    {"test_value_integer_arithmetic_helpers", test_value_integer_arithmetic_helpers},
    {"test_value_string_concat_helpers", test_value_string_concat_helpers},
    {"test_value_bool_nil_truthiness_helpers", test_value_bool_nil_truthiness_helpers},
    {"test_value_string_local_load_store_clones", test_value_string_local_load_store_clones},
};

static const test_case_t compiler_tests[] = {
    {"test_emitbc_header", test_emitbc_header},
    {"test_emitbc_opcode_map", test_emitbc_opcode_map},
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
    {"test_net_write_ignores_disconnected_lines", test_net_write_ignores_disconnected_lines},
    {"test_net_write_ignores_non_writable_line_states", test_net_write_ignores_non_writable_line_states},
    {"test_sys_compile_libcall_runtime", test_sys_compile_libcall_runtime},
    {"test_libcall_lookup_precomputed", test_libcall_lookup_precomputed},
    {"test_libcall_dispatch_microbench", test_libcall_dispatch_microbench},
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

static size_t run_suite(const char *suite_name, const test_case_t *cases, size_t count) {
  size_t suite_ran = 0;
  harness_printf("\n[test-harness][%s][START] total=%zu\n", suite_name, count);
  for (size_t i = 0; i < count; ++i) {
    harness_printf("[test-harness][%s][START] index=%zu/%zu test=%s\n", suite_name, i + 1,
                   count, cases[i].name);
    cases[i].fn();
    harness_printf("[test-harness][%s][PASS] index=%zu/%zu test=%s\n", suite_name, i + 1,
                   count, cases[i].name);
    suite_ran++;
  }
  harness_printf("[test-harness][%s][COMPLETE] executed=%zu expected=%zu status=PASS\n",
                 suite_name, suite_ran, count);
  return suite_ran;
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

  suites[0].count = run_suite("core", core_tests, sizeof(core_tests) / sizeof(core_tests[0]));
  suites[1].count = run_suite("compiler", compiler_tests,
                              sizeof(compiler_tests) / sizeof(compiler_tests[0]));
  suites[2].count = run_suite("runtime", runtime_tests,
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
    harness_printf(" %s=%zu", suites[i].name, suites[i].count);
  }
  harness_printf("\n");
  return 0;
}
