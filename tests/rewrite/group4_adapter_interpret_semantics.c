#include "test_framework.h"

void test_interpret_semantics_golden(void);
void test_interpret_result_semantics(void);
void test_runtime_build_list_allocation_failure_consumes_inputs(void);
void test_interpreter_string_literal_allocation_failure_aborts_frame(void);
void test_interpret_rejects_malformed_bytecode_before_execution(void);
void test_runtime_verification_cache_reuses_fetch_transfer(void);
void test_runtime_verification_cache_revision_and_failure_contract(void);
void test_runtime_verification_cache_revision_wrap_invalidates(void);
void test_runtime_verification_cache_revision_token_saturation_bypasses(void);
void test_runtime_verification_cache_topology_token_wrap_and_saturation(void);
void test_runtime_verification_cache_ownerless_items_bypass(void);
void test_runtime_verification_cache_isolates_live_itemstores(void);
void test_runtime_verification_cache_eviction_is_bounded(void);
void test_interpret_baseline_bytecode_safety_in_default_and_strict_modes(void);
void test_interpret_legacy_and_v1_headers_execute_equivalently(void);
void test_interpret_legacy_conversion_semantics(void);
void test_interpret_embedded_code_boundary_lengths(void);
void test_runtime_jump_diagnostic_uses_absolute_header_offset(void);
void test_runtime_opcode_schema_witnesses(void);

static const TF_TestDescriptor tests[] = {
    {"rewrite.runtime.test_interpret_semantics_golden", test_interpret_semantics_golden, "exclusive", 30000,
     "api.runtime.interpreter,api.runtime.opcode-handlers,baseline.legacy.unified.runtime.test_interpret_semantics_golden,bytecode.runtime.cleanup,bytecode.runtime.control-flow,bytecode.runtime.dispatch,bytecode.runtime.errors,bytecode.runtime.item-ops,bytecode.runtime.libcall,bytecode.runtime.locals,bytecode.runtime.stack"},
    {"rewrite.runtime.test_interpret_result_semantics", test_interpret_result_semantics, "exclusive", 30000,
     "api.runtime.interpreter,baseline.legacy.unified.runtime.test_interpret_result_semantics"},
    {"rewrite.runtime.test_runtime_build_list_allocation_failure_consumes_inputs", test_runtime_build_list_allocation_failure_consumes_inputs, "exclusive", 30000,
     "api.common.memory,api.runtime.opcode-handlers,baseline.legacy.unified.runtime.test_runtime_build_list_allocation_failure_consumes_inputs"},
    {"rewrite.runtime.test_interpreter_string_literal_allocation_failure_aborts_frame", test_interpreter_string_literal_allocation_failure_aborts_frame, "exclusive", 30000,
     "api.common.memory,api.runtime.opcode-handlers,baseline.legacy.unified.runtime.test_interpreter_string_literal_allocation_failure_aborts_frame"},
    {"rewrite.runtime.test_interpret_rejects_malformed_bytecode_before_execution", test_interpret_rejects_malformed_bytecode_before_execution, "exclusive", 30000,
     "api.runtime.interpreter,api.runtime.opcode-handlers,baseline.legacy.unified.runtime.test_interpret_rejects_malformed_bytecode_before_execution,bytecode.runtime.errors"},
    {"rewrite.runtime.test_runtime_verification_cache_reuses_fetch_transfer", test_runtime_verification_cache_reuses_fetch_transfer, "exclusive", 30000,
     "api.runtime.interpreter,baseline.legacy.unified.runtime.test_runtime_verification_cache_reuses_fetch_transfer"},
    {"rewrite.runtime.test_runtime_verification_cache_revision_and_failure_contract", test_runtime_verification_cache_revision_and_failure_contract, "exclusive", 30000,
     "api.runtime.interpreter,baseline.legacy.unified.runtime.test_runtime_verification_cache_revision_and_failure_contract"},
    {"rewrite.runtime.test_runtime_verification_cache_revision_wrap_invalidates", test_runtime_verification_cache_revision_wrap_invalidates, "exclusive", 30000,
     "api.runtime.interpreter,baseline.legacy.unified.runtime.test_runtime_verification_cache_revision_wrap_invalidates"},
    {"rewrite.runtime.test_runtime_verification_cache_revision_token_saturation_bypasses", test_runtime_verification_cache_revision_token_saturation_bypasses, "exclusive", 30000,
     "api.runtime.interpreter,baseline.legacy.unified.runtime.test_runtime_verification_cache_revision_token_saturation_bypasses"},
    {"rewrite.runtime.test_runtime_verification_cache_topology_token_wrap_and_saturation", test_runtime_verification_cache_topology_token_wrap_and_saturation, "exclusive", 30000,
     "api.runtime.interpreter,baseline.legacy.unified.runtime.test_runtime_verification_cache_topology_token_wrap_and_saturation"},
    {"rewrite.runtime.test_runtime_verification_cache_ownerless_items_bypass", test_runtime_verification_cache_ownerless_items_bypass, "exclusive", 30000,
     "api.runtime.interpreter,baseline.legacy.unified.runtime.test_runtime_verification_cache_ownerless_items_bypass"},
    {"rewrite.runtime.test_runtime_verification_cache_isolates_live_itemstores", test_runtime_verification_cache_isolates_live_itemstores, "exclusive", 30000,
     "api.runtime.interpreter,baseline.legacy.unified.runtime.test_runtime_verification_cache_isolates_live_itemstores"},
    {"rewrite.runtime.test_runtime_verification_cache_eviction_is_bounded", test_runtime_verification_cache_eviction_is_bounded, "exclusive", 30000,
     "api.runtime.interpreter,baseline.legacy.unified.runtime.test_runtime_verification_cache_eviction_is_bounded"},
    {"rewrite.runtime.test_interpret_baseline_bytecode_safety_in_default_and_strict_modes", test_interpret_baseline_bytecode_safety_in_default_and_strict_modes, "exclusive", 30000,
     "api.runtime.interpreter,baseline.legacy.unified.runtime.test_interpret_baseline_bytecode_safety_in_default_and_strict_modes,bytecode.runtime.errors"},
    {"rewrite.runtime.test_interpret_legacy_and_v1_headers_execute_equivalently", test_interpret_legacy_and_v1_headers_execute_equivalently, "exclusive", 30000,
     "api.runtime.interpreter,baseline.legacy.unified.runtime.test_interpret_legacy_and_v1_headers_execute_equivalently"},
    {"rewrite.runtime.test_interpret_legacy_conversion_semantics", test_interpret_legacy_conversion_semantics, "exclusive", 30000,
     "api.runtime.interpreter,baseline.legacy.unified.runtime.test_interpret_legacy_conversion_semantics"},
    {"rewrite.runtime.test_interpret_embedded_code_boundary_lengths", test_interpret_embedded_code_boundary_lengths, "exclusive", 30000,
     "api.runtime.interpreter,baseline.legacy.unified.runtime.test_interpret_embedded_code_boundary_lengths"},
    {"rewrite.runtime.test_runtime_jump_diagnostic_uses_absolute_header_offset", test_runtime_jump_diagnostic_uses_absolute_header_offset, "exclusive", 30000,
     "api.runtime.opcode-handlers,baseline.legacy.unified.runtime.test_runtime_jump_diagnostic_uses_absolute_header_offset"},
    {"rewrite.runtime.test_runtime_opcode_schema_witnesses", test_runtime_opcode_schema_witnesses, "exclusive", 30000,
     "api.bytecode.schema,api.runtime.opcode-handlers,bytecode.opcode.add,bytecode.opcode.and,bytecode.opcode.build_list,bytecode.opcode.dec_local,bytecode.opcode.discard,bytecode.opcode.div,bytecode.opcode.eq,bytecode.opcode.ge,bytecode.opcode.gt,bytecode.opcode.inc_local,bytecode.opcode.item_begin,bytecode.opcode.item_begin_rel,bytecode.opcode.item_deref,bytecode.opcode.item_save,bytecode.opcode.item_save_code,bytecode.opcode.jump,bytecode.opcode.jump_if_false,bytecode.opcode.le,bytecode.opcode.libcall,bytecode.opcode.load_local,bytecode.opcode.lt,bytecode.opcode.make_itemref,bytecode.opcode.mod,bytecode.opcode.mul,bytecode.opcode.neg,bytecode.opcode.neq,bytecode.opcode.not,bytecode.opcode.or,bytecode.opcode.push_bool,bytecode.opcode.push_float,bytecode.opcode.push_int,bytecode.opcode.push_nil,bytecode.opcode.push_string,bytecode.opcode.store_local,bytecode.opcode.sub,bytecode.runtime.dispatch"},
};

int main(int argc, char **argv) {
  return tf_main(argc, argv, tests, sizeof tests / sizeof tests[0]);
}
