#include "test_framework.h"

void test_bytecode_wire_boundary_vectors(void);
void test_bytecode_wire_subsystems_agree(void);

static const TF_TestDescriptor tests[] = {
    {"rewrite.compiler.test_bytecode_wire_boundary_vectors", test_bytecode_wire_boundary_vectors, "", 30000,
     "api.bytecode.wire-format,baseline.legacy.unified.compiler.test_bytecode_wire_boundary_vectors"},
    {"rewrite.compiler.test_bytecode_wire_subsystems_agree", test_bytecode_wire_subsystems_agree, "", 30000,
     "api.bytecode.wire-format,baseline.legacy.unified.compiler.test_bytecode_wire_subsystems_agree"},
};

int main(int argc, char **argv) {
  return tf_main(argc, argv, tests, sizeof tests / sizeof tests[0]);
}
