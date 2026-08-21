#include "test_framework.h"

void test_interpret_stress(void);

static const TF_TestDescriptor tests[] = {
    {"rewrite.runtime.test_interpret_stress", test_interpret_stress, "exclusive", 120000,
     "bytecode.runtime.cleanup,api.runtime.interpreter,executable.sin.command-line,executable.sin.input-output,executable.sin.exit-status,executable.sin.errors"},
};

int main(int argc, char **argv) {
  return tf_main(argc, argv, tests, sizeof tests / sizeof tests[0]);
}
