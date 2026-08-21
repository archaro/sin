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
     "language.token.tstringlit,language.operator.boolean,language.literal.string,language.statement.assignment,language.statement.expression,language.statement.return,language.statement.while,language.statement.do-while,language.statement.if,language.statement.break,language.statement.continue,language.expression.binary,language.expression.unary,language.expression.local,language.expression.call,language.expression.item,bytecode.ir.ir_op_halt,bytecode.ir.ir_op_add,bytecode.ir.ir_op_sub,bytecode.ir.ir_op_mul,bytecode.ir.ir_op_div,bytecode.ir.ir_op_mod,bytecode.ir.ir_op_neg,bytecode.ir.ir_op_eq,bytecode.ir.ir_op_neq,bytecode.ir.ir_op_lt,bytecode.ir.ir_op_gt,bytecode.ir.ir_op_le,bytecode.ir.ir_op_ge,bytecode.ir.ir_op_not,bytecode.ir.ir_op_and,bytecode.ir.ir_op_or,bytecode.ir.ir_op_store_local,bytecode.opcode.halt,bytecode.opcode.add,bytecode.opcode.sub,bytecode.opcode.mul,bytecode.opcode.div,bytecode.opcode.mod,bytecode.opcode.neg,bytecode.opcode.eq,bytecode.opcode.neq,bytecode.opcode.lt,bytecode.opcode.gt,bytecode.opcode.le,bytecode.opcode.ge,bytecode.opcode.not,bytecode.opcode.and,bytecode.opcode.or,bytecode.opcode.store_local,bytecode.encoding.string,bytecode.runtime.dispatch,bytecode.runtime.stack,bytecode.runtime.locals,bytecode.runtime.control-flow,bytecode.runtime.item-ops,bytecode.runtime.libcall,bytecode.runtime.errors,bytecode.runtime.cleanup,api.runtime.interpreter,api.runtime.opcode-handlers"},
    {"rewrite.runtime.test_interpret_result_semantics", test_interpret_result_semantics, "exclusive", 30000,
     "language.token.tor,language.token.tand,language.token.tnot,api.runtime.interpreter"},
    {"rewrite.runtime.test_runtime_build_list_allocation_failure_consumes_inputs", test_runtime_build_list_allocation_failure_consumes_inputs, "exclusive", 30000,
     "api.common.memory,api.runtime.opcode-handlers"},
    {"rewrite.runtime.test_interpreter_string_literal_allocation_failure_aborts_frame", test_interpreter_string_literal_allocation_failure_aborts_frame, "exclusive", 30000,
     "api.common.memory,api.runtime.opcode-handlers"},
    {"rewrite.runtime.test_interpret_rejects_malformed_bytecode_before_execution", test_interpret_rejects_malformed_bytecode_before_execution, "exclusive", 30000,
     "bytecode.runtime.errors,api.runtime.interpreter,api.runtime.opcode-handlers"},
    {"rewrite.runtime.test_runtime_verification_cache_reuses_fetch_transfer", test_runtime_verification_cache_reuses_fetch_transfer, "exclusive", 30000,
     "api.runtime.interpreter"},
    {"rewrite.runtime.test_runtime_verification_cache_revision_and_failure_contract", test_runtime_verification_cache_revision_and_failure_contract, "exclusive", 30000,
     "api.runtime.interpreter"},
    {"rewrite.runtime.test_runtime_verification_cache_revision_wrap_invalidates", test_runtime_verification_cache_revision_wrap_invalidates, "exclusive", 30000,
     "api.runtime.interpreter"},
    {"rewrite.runtime.test_runtime_verification_cache_revision_token_saturation_bypasses", test_runtime_verification_cache_revision_token_saturation_bypasses, "exclusive", 30000,
     "api.runtime.interpreter"},
    {"rewrite.runtime.test_runtime_verification_cache_topology_token_wrap_and_saturation", test_runtime_verification_cache_topology_token_wrap_and_saturation, "exclusive", 30000,
     "api.runtime.interpreter"},
    {"rewrite.runtime.test_runtime_verification_cache_ownerless_items_bypass", test_runtime_verification_cache_ownerless_items_bypass, "exclusive", 30000,
     "api.runtime.interpreter"},
    {"rewrite.runtime.test_runtime_verification_cache_isolates_live_itemstores", test_runtime_verification_cache_isolates_live_itemstores, "exclusive", 30000,
     "api.runtime.interpreter,executable.sin.command-line,executable.sin.input-output,executable.sin.exit-status,executable.sin.persistence,executable.sin.errors"},
    {"rewrite.runtime.test_runtime_verification_cache_eviction_is_bounded", test_runtime_verification_cache_eviction_is_bounded, "exclusive", 30000,
     "api.runtime.interpreter"},
    {"rewrite.runtime.test_interpret_baseline_bytecode_safety_in_default_and_strict_modes", test_interpret_baseline_bytecode_safety_in_default_and_strict_modes, "exclusive", 30000,
     "bytecode.runtime.errors,api.runtime.interpreter"},
    {"rewrite.runtime.test_interpret_legacy_and_v1_headers_execute_equivalently", test_interpret_legacy_and_v1_headers_execute_equivalently, "exclusive", 30000,
     "api.runtime.interpreter"},
    {"rewrite.runtime.test_interpret_legacy_conversion_semantics", test_interpret_legacy_conversion_semantics, "exclusive", 30000,
     "api.runtime.interpreter"},
    {"rewrite.runtime.test_interpret_embedded_code_boundary_lengths", test_interpret_embedded_code_boundary_lengths, "exclusive", 30000,
     "bytecode.encoding.embedded-code,api.runtime.interpreter"},
    {"rewrite.runtime.test_runtime_jump_diagnostic_uses_absolute_header_offset", test_runtime_jump_diagnostic_uses_absolute_header_offset, "exclusive", 30000,
     "api.runtime.opcode-handlers"},
    {"rewrite.runtime.test_runtime_opcode_schema_witnesses", test_runtime_opcode_schema_witnesses, "exclusive", 30000,
     "bytecode.opcode.push_int,bytecode.opcode.push_float,bytecode.opcode.push_bool,bytecode.opcode.push_string,bytecode.opcode.push_nil,bytecode.opcode.add,bytecode.opcode.sub,bytecode.opcode.mul,bytecode.opcode.div,bytecode.opcode.mod,bytecode.opcode.neg,bytecode.opcode.eq,bytecode.opcode.neq,bytecode.opcode.lt,bytecode.opcode.gt,bytecode.opcode.le,bytecode.opcode.ge,bytecode.opcode.not,bytecode.opcode.and,bytecode.opcode.or,bytecode.opcode.discard,bytecode.opcode.load_local,bytecode.opcode.store_local,bytecode.opcode.inc_local,bytecode.opcode.dec_local,bytecode.opcode.jump,bytecode.opcode.jump_if_false,bytecode.opcode.item_begin,bytecode.opcode.item_begin_rel,bytecode.opcode.item_deref,bytecode.opcode.item_save,bytecode.opcode.libcall,bytecode.opcode.item_save_code,bytecode.opcode.build_list,bytecode.opcode.make_itemref,bytecode.runtime.dispatch,api.bytecode.schema,api.runtime.opcode-handlers"},
};

int main(int argc, char **argv) {
  return tf_main(argc, argv, tests, sizeof tests / sizeof tests[0]);
}
