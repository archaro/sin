#include "test_framework.h"

void test_pipeline_golden(void);
void test_pipeline_large_local_lookup_duplicate(void);

static const TF_TestDescriptor tests[] = {
    {"rewrite.compiler.test_pipeline_golden", test_pipeline_golden, "exclusive", 30000,
     "api.compiler.lowering,baseline.legacy.unified.compiler.test_pipeline_golden,executable.scomp.command-line,executable.scomp.errors,executable.scomp.exit-status,executable.scomp.input-output,executable.scomp.persistence"},
    {"rewrite.compiler.test_pipeline_large_local_lookup_duplicate", test_pipeline_large_local_lookup_duplicate, "exclusive", 30000,
     "baseline.legacy.unified.compiler.test_pipeline_large_local_lookup_duplicate,executable.scomp.command-line,executable.scomp.errors,executable.scomp.exit-status,executable.scomp.input-output,executable.scomp.persistence"},
};

int main(int argc, char **argv) {
  return tf_main(argc, argv, tests, sizeof tests / sizeof tests[0]);
}
