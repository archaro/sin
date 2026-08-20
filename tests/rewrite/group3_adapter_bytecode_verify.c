#include "test_framework.h"

void test_bytecode_verify_executable_and_disassembly_profiles(void);
void test_bytecode_verify_analysis_storage_is_profile_scoped(void);
void test_bytecode_verify_dense_budget_and_growth_failures(void);
void test_bytecode_verify_constrained_address_space(void);
void test_bytecode_verify_minimal_and_header_errors(void);
void test_bytecode_format_header_variants(void);
void test_bytecode_verify_opcode_terminators_and_complete_buffer(void);
void test_bytecode_verify_truncated_operand_widths(void);
void test_bytecode_verify_list_operations(void);
void test_bytecode_verify_local_indexes_and_items(void);
void test_bytecode_verify_jumps_and_stack_flow(void);
void test_bytecode_verify_nesting_and_vm_stack_limits(void);
void test_bytecode_verify_pipeline_fixture_bytecode(void);
void test_bytecode_verify_compiler_emitted_bytecode(void);

static const TF_TestDescriptor tests[] = {
    {"rewrite.compiler.test_bytecode_verify_executable_and_disassembly_profiles", test_bytecode_verify_executable_and_disassembly_profiles, "exclusive", 30000,
     "api.bytecode.verification,baseline.legacy.unified.compiler.test_bytecode_verify_executable_and_disassembly_profiles,bytecode.disassembly.header,bytecode.disassembly.item-expression,bytecode.disassembly.malformed,bytecode.disassembly.mnemonic,bytecode.disassembly.operand,bytecode.disassembly.options,bytecode.verifier.call-target,bytecode.verifier.opcode-boundary,bytecode.verifier.operand-width"},
    {"rewrite.compiler.test_bytecode_verify_analysis_storage_is_profile_scoped", test_bytecode_verify_analysis_storage_is_profile_scoped, "exclusive", 30000,
     "baseline.legacy.unified.compiler.test_bytecode_verify_analysis_storage_is_profile_scoped"},
    {"rewrite.compiler.test_bytecode_verify_dense_budget_and_growth_failures", test_bytecode_verify_dense_budget_and_growth_failures, "exclusive", 30000,
     "baseline.legacy.unified.compiler.test_bytecode_verify_dense_budget_and_growth_failures"},
    {"rewrite.compiler.test_bytecode_verify_constrained_address_space", test_bytecode_verify_constrained_address_space, "exclusive", 30000,
     "baseline.legacy.unified.compiler.test_bytecode_verify_constrained_address_space"},
    {"rewrite.compiler.test_bytecode_verify_minimal_and_header_errors", test_bytecode_verify_minimal_and_header_errors, "exclusive", 30000,
     "api.bytecode.verification,baseline.legacy.unified.compiler.test_bytecode_verify_minimal_and_header_errors,bytecode.verifier.stack-underflow"},
    {"rewrite.compiler.test_bytecode_format_header_variants", test_bytecode_format_header_variants, "", 30000,
     "api.bytecode.wire-format,baseline.legacy.unified.compiler.test_bytecode_format_header_variants,bytecode.encoding.header"},
    {"rewrite.compiler.test_bytecode_verify_opcode_terminators_and_complete_buffer", test_bytecode_verify_opcode_terminators_and_complete_buffer, "exclusive", 30000,
     "baseline.legacy.unified.compiler.test_bytecode_verify_opcode_terminators_and_complete_buffer"},
    {"rewrite.compiler.test_bytecode_verify_truncated_operand_widths", test_bytecode_verify_truncated_operand_widths, "exclusive", 30000,
     "baseline.legacy.unified.compiler.test_bytecode_verify_truncated_operand_widths"},
    {"rewrite.compiler.test_bytecode_verify_list_operations", test_bytecode_verify_list_operations, "exclusive", 30000,
     "baseline.legacy.unified.compiler.test_bytecode_verify_list_operations"},
    {"rewrite.compiler.test_bytecode_verify_local_indexes_and_items", test_bytecode_verify_local_indexes_and_items, "exclusive", 30000,
     "baseline.legacy.unified.compiler.test_bytecode_verify_local_indexes_and_items"},
    {"rewrite.compiler.test_bytecode_verify_jumps_and_stack_flow", test_bytecode_verify_jumps_and_stack_flow, "exclusive", 30000,
     "api.bytecode.verification,baseline.legacy.unified.compiler.test_bytecode_verify_jumps_and_stack_flow,bytecode.encoding.label,bytecode.verifier.jump-target"},
    {"rewrite.compiler.test_bytecode_verify_nesting_and_vm_stack_limits", test_bytecode_verify_nesting_and_vm_stack_limits, "exclusive", 30000,
     "baseline.legacy.unified.compiler.test_bytecode_verify_nesting_and_vm_stack_limits,bytecode.verifier.stack-overflow"},
    {"rewrite.compiler.test_bytecode_verify_pipeline_fixture_bytecode", test_bytecode_verify_pipeline_fixture_bytecode, "exclusive", 30000,
     "baseline.legacy.unified.compiler.test_bytecode_verify_pipeline_fixture_bytecode"},
    {"rewrite.compiler.test_bytecode_verify_compiler_emitted_bytecode", test_bytecode_verify_compiler_emitted_bytecode, "exclusive", 30000,
     "baseline.legacy.unified.compiler.test_bytecode_verify_compiler_emitted_bytecode"},
};

int main(int argc, char **argv) {
  return tf_main(argc, argv, tests, sizeof tests / sizeof tests[0]);
}

