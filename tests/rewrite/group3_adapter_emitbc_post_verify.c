#include "test_framework.h"

void test_emitbc_post_emission_verification(void);

static const TF_TestDescriptor tests[] = {
    {"rewrite.compiler.test_emitbc_post_emission_verification", test_emitbc_post_emission_verification, "exclusive", 30000,
     "api.compiler.bytecode-emission,baseline.legacy.unified.compiler.test_emitbc_post_emission_verification"},
};

int main(int argc, char **argv) {
  return tf_main(argc, argv, tests, sizeof tests / sizeof tests[0]);
}
