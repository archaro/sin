#include "test_framework.h"

void test_fixture_policy_declared_goldens_exist(void);

static const TF_TestDescriptor tests[] = {
    {"rewrite.core.test_fixture_policy_declared_goldens_exist",
     test_fixture_policy_declared_goldens_exist, "exclusive", 30000,
     "baseline.legacy.unified.core.test_fixture_policy_declared_goldens_exist"},
};

int main(int argc, char **argv) {
  return tf_main(argc, argv, tests, sizeof tests / sizeof tests[0]);
}
