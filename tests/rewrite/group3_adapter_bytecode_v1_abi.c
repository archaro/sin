#include "test_framework.h"

void test_bytecode_v1_abi_manifest(void);

static const TF_TestDescriptor tests[] = {
    {"rewrite.compiler.test_bytecode_v1_abi_manifest", test_bytecode_v1_abi_manifest, "", 30000,
     "baseline.legacy.unified.compiler.test_bytecode_v1_abi_manifest,bytecode.encoding.integer"},
};

int main(int argc, char **argv) {
  return tf_main(argc, argv, tests, sizeof tests / sizeof tests[0]);
}
