#include "test_framework.h"

void test_emitbc_invariants(void);

static const TF_TestDescriptor tests[] = {
    {"rewrite.compiler.test_emitbc_invariants", test_emitbc_invariants, "exclusive", 30000,
     "api.compiler.bytecode-emission,baseline.legacy.unified.compiler.test_emitbc_invariants,bytecode.lowering.call,bytecode.lowering.expression,bytecode.lowering.foreach,bytecode.lowering.item,bytecode.lowering.libcall,bytecode.lowering.loop,bytecode.lowering.short-circuit,bytecode.lowering.statement"},
};

int main(int argc, char **argv) {
  return tf_main(argc, argv, tests, sizeof tests / sizeof tests[0]);
}
