#include "test_framework.h"

void test_pipeline_golden(void);
void test_pipeline_large_local_lookup_duplicate(void);

static const TF_TestDescriptor tests[] = {
    {"rewrite.compiler.test_pipeline_golden", test_pipeline_golden, "exclusive", 30000,
     "api.compiler.lowering,executable.scomp.command-line,executable.scomp.input-output,executable.scomp.exit-status,executable.scomp.persistence,executable.scomp.errors"},
    {"rewrite.compiler.test_pipeline_large_local_lookup_duplicate", test_pipeline_large_local_lookup_duplicate, "exclusive", 30000,
     "executable.scomp.command-line,executable.scomp.input-output,executable.scomp.exit-status,executable.scomp.persistence,executable.scomp.errors"},
};

int main(int argc, char **argv) {
  return tf_main(argc, argv, tests, sizeof tests / sizeof tests[0]);
}
