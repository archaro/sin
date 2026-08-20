#include "test_framework.h"

void test_libcall_output_formats_values(void);
void test_libcall_registry_roundtrip(void);
void test_runtime_init_validates_libcalls_once(void);
void test_interpret_lazy_init_failure_is_transactional(void);
void test_libcall_registry_init_failure_has_no_partial_state(void);
void test_libcall_registry_lifecycle_reinit_sequence(void);
void test_libcall_registry_repeated_teardown_is_safe(void);
void test_missing_libcall_is_null_and_interpret_deterministic(void);
void test_default_libcall_wrappers_lazy_init_after_reset(void);
void test_libcall_registry_self_check_invalid_entries(void);
void test_libcall_invalid_arg_branches_return_contracts(void);
void test_libcall_float_integer_only_arguments_rejected(void);

static const TF_TestDescriptor tests[] = {
    {"rewrite.runtime.test_libcall_registry_roundtrip",
     test_libcall_registry_roundtrip, "exclusive", 30000,
     "api.libcall.registry,api.libcall.table,baseline.legacy.unified.runtime.test_libcall_registry_roundtrip,bytecode.encoding.libcall,libcall.sys.abort,libcall.sys.shutdown"},
    {"rewrite.runtime.test_runtime_init_validates_libcalls_once",
     test_runtime_init_validates_libcalls_once, "exclusive", 30000,
     "api.runtime.runtime-opcode,baseline.legacy.unified.runtime.test_runtime_init_validates_libcalls_once"},
    {"rewrite.runtime.test_interpret_lazy_init_failure_is_transactional",
     test_interpret_lazy_init_failure_is_transactional, "exclusive", 30000,
     "api.runtime.interpreter,api.runtime.runtime-opcode,baseline.legacy.unified.runtime.test_interpret_lazy_init_failure_is_transactional"},
    {"rewrite.runtime.test_libcall_registry_init_failure_has_no_partial_state",
     test_libcall_registry_init_failure_has_no_partial_state, "exclusive", 30000,
     "api.libcall.registry,baseline.legacy.unified.runtime.test_libcall_registry_init_failure_has_no_partial_state"},
    {"rewrite.runtime.test_libcall_registry_lifecycle_reinit_sequence",
     test_libcall_registry_lifecycle_reinit_sequence, "exclusive", 30000,
     "baseline.legacy.unified.runtime.test_libcall_registry_lifecycle_reinit_sequence"},
    {"rewrite.runtime.test_libcall_registry_repeated_teardown_is_safe",
     test_libcall_registry_repeated_teardown_is_safe, "exclusive", 30000,
     "baseline.legacy.unified.runtime.test_libcall_registry_repeated_teardown_is_safe"},
    {"rewrite.runtime.test_missing_libcall_is_null_and_interpret_deterministic",
     test_missing_libcall_is_null_and_interpret_deterministic, "exclusive", 30000,
     "baseline.legacy.unified.runtime.test_missing_libcall_is_null_and_interpret_deterministic"},
    {"rewrite.runtime.test_default_libcall_wrappers_lazy_init_after_reset",
     test_default_libcall_wrappers_lazy_init_after_reset, "exclusive", 30000,
     "baseline.legacy.unified.runtime.test_default_libcall_wrappers_lazy_init_after_reset"},
    {"rewrite.runtime.test_libcall_registry_self_check_invalid_entries",
     test_libcall_registry_self_check_invalid_entries, "exclusive", 30000,
     "api.libcall.registry,api.libcall.table,baseline.legacy.unified.runtime.test_libcall_registry_self_check_invalid_entries"},
    {"rewrite.runtime.test_libcall_invalid_arg_branches_return_contracts",
     test_libcall_invalid_arg_branches_return_contracts, "exclusive", 30000,
     "baseline.legacy.unified.runtime.test_libcall_invalid_arg_branches_return_contracts,language.semantic-rule.libcall-resolution"},
    {"rewrite.runtime.test_libcall_float_integer_only_arguments_rejected",
     test_libcall_float_integer_only_arguments_rejected, "exclusive", 30000,
     "baseline.legacy.unified.runtime.test_libcall_float_integer_only_arguments_rejected"},
    {"rewrite.runtime.test_libcall_output_formats_values",
     test_libcall_output_formats_values, "exclusive", 30000,
     "api.common.logging,baseline.legacy.unified.runtime.test_libcall_output_formats_values,libcall.sys.log"},
};

int main(int argc, char **argv) {
  return tf_main(argc, argv, tests, sizeof tests / sizeof tests[0]);
}
