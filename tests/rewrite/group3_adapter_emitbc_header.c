#include "test_framework.h"

void test_emitbc_header(void);

static const TF_TestDescriptor tests[] = {
    {"rewrite.compiler.test_emitbc_header", test_emitbc_header, "exclusive", 30000,
     "baseline.legacy.unified.compiler.test_emitbc_header,bytecode.encoding.embedded-code,bytecode.encoding.header,bytecode.encoding.string"},
};

int main(int argc, char **argv) {
  return tf_main(argc, argv, tests, sizeof tests / sizeof tests[0]);
}

