#include "test_framework.h"

void test_bytecode_convert_legacy_and_v1(void);
void test_bytecode_convert_malformed_matrix(void);
void test_bytecode_convert_v1_idempotent(void);
void test_bytecode_convert_legacy_token_boundaries(void);
void test_bytecode_convert_allocation_failures(void);
void test_embedded_code_conversion_boundaries(void);
void test_bytecode_convert_jump_lookup_scaling(void);

static const TF_TestDescriptor tests[] = {
    {"rewrite.compiler.test_bytecode_convert_legacy_and_v1", test_bytecode_convert_legacy_and_v1, "exclusive", 30000,
     "api.bytecode.conversion,baseline.legacy.unified.compiler.test_bytecode_convert_legacy_and_v1"},
    {"rewrite.compiler.test_bytecode_convert_malformed_matrix", test_bytecode_convert_malformed_matrix, "exclusive", 30000,
     "api.bytecode.conversion,baseline.legacy.unified.compiler.test_bytecode_convert_malformed_matrix"},
    {"rewrite.compiler.test_bytecode_convert_v1_idempotent", test_bytecode_convert_v1_idempotent, "exclusive", 30000,
     "baseline.legacy.unified.compiler.test_bytecode_convert_v1_idempotent"},
    {"rewrite.compiler.test_bytecode_convert_legacy_token_boundaries", test_bytecode_convert_legacy_token_boundaries, "exclusive", 30000,
     "baseline.legacy.unified.compiler.test_bytecode_convert_legacy_token_boundaries"},
    {"rewrite.compiler.test_bytecode_convert_allocation_failures", test_bytecode_convert_allocation_failures, "exclusive", 30000,
     "api.bytecode.conversion,baseline.legacy.unified.compiler.test_bytecode_convert_allocation_failures"},
    {"rewrite.compiler.test_embedded_code_conversion_boundaries", test_embedded_code_conversion_boundaries, "exclusive", 30000,
     "baseline.legacy.unified.compiler.test_embedded_code_conversion_boundaries"},
    {"rewrite.compiler.test_bytecode_convert_jump_lookup_scaling", test_bytecode_convert_jump_lookup_scaling, "exclusive", 30000,
     "baseline.legacy.unified.compiler.test_bytecode_convert_jump_lookup_scaling"},
};

int main(int argc, char **argv) {
  return tf_main(argc, argv, tests, sizeof tests / sizeof tests[0]);
}
