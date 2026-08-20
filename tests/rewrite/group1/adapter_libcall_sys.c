#include "test_framework.h"

void test_sys_source_libcall(void);

static const TF_TestDescriptor tests[] = {
    {"rewrite.runtime.test_sys_source_libcall", test_sys_source_libcall,
     "exclusive", 30000,
     "api.common.logging,baseline.legacy.unified.runtime.test_sys_source_libcall,libcall.sys.source"},
};

int main(int argc, char **argv) {
  return tf_main(argc, argv, tests, sizeof tests / sizeof tests[0]);
}
