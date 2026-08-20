#include "test_framework.h"

void test_emitbc_jumps(void);

static const TF_TestDescriptor tests[] = {
    {"rewrite.compiler.test_emitbc_jumps", test_emitbc_jumps, "exclusive", 30000,
     "api.compiler.lowering,baseline.legacy.unified.compiler.test_emitbc_jumps,bytecode.encoding.label"},
};

int main(int argc, char **argv) {
  return tf_main(argc, argv, tests, sizeof tests / sizeof tests[0]);
}
