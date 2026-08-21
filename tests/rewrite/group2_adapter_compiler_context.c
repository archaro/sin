#include "test_framework.h"

void test_compiler_context_failures(void);

static const TF_TestDescriptor tests[] = {
    {"rewrite.compiler.test_compiler_context_failures", test_compiler_context_failures, "exclusive", 30000,
     "api.compiler.context"},
};

int main(int argc, char **argv) {
  return tf_main(argc, argv, tests, sizeof tests / sizeof tests[0]);
}
