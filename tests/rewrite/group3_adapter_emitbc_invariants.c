#include "test_framework.h"

void test_emitbc_invariants(void);

static const TF_TestDescriptor tests[] = {
    {"rewrite.compiler.test_emitbc_invariants", test_emitbc_invariants, "exclusive", 30000,
     "bytecode.lowering.expression,bytecode.lowering.statement,bytecode.lowering.short-circuit,bytecode.lowering.loop,bytecode.lowering.foreach,bytecode.lowering.item,bytecode.lowering.call,bytecode.lowering.libcall,api.compiler.bytecode-emission"},
};

int main(int argc, char **argv) {
  return tf_main(argc, argv, tests, sizeof tests / sizeof tests[0]);
}
