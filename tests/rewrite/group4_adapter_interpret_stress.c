#include "test_framework.h"

void test_interpret_stress(void);

static const TF_TestDescriptor tests[] = {
    {"rewrite.runtime.test_interpret_stress", test_interpret_stress, "exclusive", 120000,
     "api.runtime.interpreter,baseline.legacy.unified.runtime.test_interpret_stress,bytecode.runtime.cleanup,executable.sin.command-line,executable.sin.errors,executable.sin.exit-status,executable.sin.input-output"},
};

int main(int argc, char **argv) {
  return tf_main(argc, argv, tests, sizeof tests / sizeof tests[0]);
}
