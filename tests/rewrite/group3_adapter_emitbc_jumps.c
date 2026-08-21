#include "test_framework.h"

void test_emitbc_jumps(void);

static const TF_TestDescriptor tests[] = {
    {"rewrite.compiler.test_emitbc_jumps", test_emitbc_jumps, "exclusive", 30000,
     "bytecode.encoding.label,api.compiler.lowering"},
};

int main(int argc, char **argv) {
  return tf_main(argc, argv, tests, sizeof tests / sizeof tests[0]);
}
