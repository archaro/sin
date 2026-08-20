#include "test_framework.h"

void test_pipeline_source_golden(void);

static const TF_TestDescriptor tests[] = {
    {"rewrite.compiler.test_pipeline_source_golden", test_pipeline_source_golden, "exclusive", 30000,
     "api.compiler.pipeline,baseline.legacy.unified.compiler.test_pipeline_source_golden"},
};

int main(int argc, char **argv) {
  return tf_main(argc, argv, tests, sizeof tests / sizeof tests[0]);
}
