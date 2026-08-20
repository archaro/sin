#include "test_framework.h"

void test_sconv_v2_canonical_and_invocation_modes(void);

static const TF_TestDescriptor tests[] = {
    {"rewrite.core.test_sconv_v2_canonical_and_invocation_modes",
     test_sconv_v2_canonical_and_invocation_modes, "exclusive", 30000,
     "api.common.paths,baseline.legacy.unified.core.test_sconv_v2_canonical_and_invocation_modes,executable.sconv.command-line,executable.sconv.errors,executable.sconv.exit-status,executable.sconv.input-output,executable.sconv.persistence"},
};

int main(int argc, char **argv) {
  return tf_main(argc, argv, tests, sizeof tests / sizeof tests[0]);
}
