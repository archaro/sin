#include "test_framework.h"

static void duplicate_fixture_test(void) { TF_ASSERT_TRUE(true); }

static const TF_TestDescriptor tests[] = {
  {"assert_fail_bool", duplicate_fixture_test, "helper", 1000, "framework.duplicate"}
};

int main(int argc, char **argv) {
  return tf_main(argc, argv, tests, sizeof tests / sizeof tests[0]);
}
