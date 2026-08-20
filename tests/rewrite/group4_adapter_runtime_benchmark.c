#include "test_framework.h"

void test_runtime_benchmark_optin(void);

static const TF_TestDescriptor tests[] = {
    {"rewrite.runtime.test_runtime_benchmark_optin", test_runtime_benchmark_optin, "benchmark,exclusive", 120000,
     "baseline.legacy.unified.runtime.test_runtime_benchmark_optin,api.libcall.registry"},
};

int main(int argc, char **argv) {
  return tf_main(argc, argv, tests, sizeof tests / sizeof tests[0]);
}
