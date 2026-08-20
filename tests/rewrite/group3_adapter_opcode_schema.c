#include "test_framework.h"

void test_opcode_schema_consistency(void);
void test_bytecode_verify_local_index_bounds(void);
void test_bytecode_verify_item_expression_streams(void);
void test_bytecode_verify_jump_targets(void);
void test_bytecode_verify_stack_flow(void);

static const TF_TestDescriptor tests[] = {
    {"rewrite.core.test_opcode_schema_consistency", test_opcode_schema_consistency, "", 30000,
     "api.bytecode.schema,baseline.legacy.unified.core.test_opcode_schema_consistency"},
    {"rewrite.core.test_bytecode_verify_local_index_bounds", test_bytecode_verify_local_index_bounds, "exclusive", 30000,
     "baseline.legacy.unified.core.test_bytecode_verify_local_index_bounds"},
    {"rewrite.core.test_bytecode_verify_item_expression_streams", test_bytecode_verify_item_expression_streams, "exclusive", 30000,
     "baseline.legacy.unified.core.test_bytecode_verify_item_expression_streams,bytecode.verifier.item-expression"},
    {"rewrite.core.test_bytecode_verify_jump_targets", test_bytecode_verify_jump_targets, "exclusive", 30000,
     "baseline.legacy.unified.core.test_bytecode_verify_jump_targets"},
    {"rewrite.core.test_bytecode_verify_stack_flow", test_bytecode_verify_stack_flow, "exclusive", 30000,
     "baseline.legacy.unified.core.test_bytecode_verify_stack_flow"},
};

int main(int argc, char **argv) {
  return tf_main(argc, argv, tests, sizeof tests / sizeof tests[0]);
}

