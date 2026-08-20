#include "test_framework.h"

void test_libcall_output_formats_values(void);

static const TF_TestDescriptor tests[] = {
    {"rewrite.runtime.test_libcall_output_formats_values",
     test_libcall_output_formats_values, "exclusive", 30000,
     "api.common.logging,baseline.legacy.unified.runtime.test_libcall_output_formats_values,libcall.sys.log"},
};

int main(int argc, char **argv) {
  return tf_main(argc, argv, tests, sizeof tests / sizeof tests[0]);
}
