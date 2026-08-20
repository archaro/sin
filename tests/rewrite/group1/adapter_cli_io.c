#include "test_framework.h"

void test_cli_io_helpers(void);

static const TF_TestDescriptor tests[] = {
    {"rewrite.core.test_cli_io_helpers", test_cli_io_helpers, "exclusive",
     30000, "api.common.cli-io,baseline.legacy.unified.core.test_cli_io_helpers"},
};

int main(int argc, char **argv) {
  return tf_main(argc, argv, tests, sizeof tests / sizeof tests[0]);
}
