#include "test_framework.h"

void test_conv_bool_registry_contract(void);
void test_conv_bool_uses_runtime_truthiness(void);
void test_conv_bool_consumes_values_and_preserves_diagnostics(void);
void test_conv_bool_source_integration_and_arity(void);

static const TF_TestDescriptor tests[] = {
    {"rewrite.runtime.test_conv_bool_registry_contract", test_conv_bool_registry_contract, "exclusive", 30000, "api.libcall.conv,libcall.conv.bool"},
    {"rewrite.runtime.test_conv_bool_uses_runtime_truthiness", test_conv_bool_uses_runtime_truthiness, "exclusive", 30000, "api.libcall.conv,libcall.conv.bool"},
    {"rewrite.runtime.test_conv_bool_consumes_values_and_preserves_diagnostics", test_conv_bool_consumes_values_and_preserves_diagnostics, "exclusive", 30000, "api.libcall.conv,libcall.conv.bool"},
    {"rewrite.runtime.test_conv_bool_source_integration_and_arity", test_conv_bool_source_integration_and_arity, "exclusive", 30000, "api.libcall.conv,libcall.conv.bool"},
};

int main(int argc, char **argv) {
  return tf_main(argc, argv, tests, sizeof tests / sizeof tests[0]);
}
