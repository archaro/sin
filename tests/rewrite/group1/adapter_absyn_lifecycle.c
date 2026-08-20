#include "test_framework.h"

void test_absyn_constructor_allocation_failures(void);

static const TF_TestDescriptor tests[] = {
    {"rewrite.core.test_absyn_constructor_allocation_failures",
     test_absyn_constructor_allocation_failures, "exclusive", 30000,
     "api.common.memory,api.compiler.ast-lifecycle,baseline.legacy.unified.core.test_absyn_constructor_allocation_failures"},
};

int main(int argc, char **argv) {
  return tf_main(argc, argv, tests, sizeof tests / sizeof tests[0]);
}
