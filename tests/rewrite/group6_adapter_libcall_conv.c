#include "test_framework.h"

void test_conv_bool_registry_contract(void);
void test_conv_bool_uses_runtime_truthiness(void);
void test_conv_bool_consumes_values_and_preserves_diagnostics(void);
void test_conv_bool_source_integration_and_arity(void);
void test_conv_str_registry_contract(void);
void test_conv_str_converts_values(void);
void test_conv_str_consumes_values_and_preserves_diagnostics(void);
void test_conv_str_source_integration_and_arity(void);
void test_conv_int_registry_contract(void);
void test_conv_int_converts_values(void);
void test_conv_int_rejects_invalid_values_and_boundaries(void);
void test_conv_int_consumes_values_and_preserves_diagnostics(void);
void test_conv_int_source_integration_and_arity(void);
void test_conv_float_registry_contract(void);
void test_conv_float_converts_values(void);
void test_conv_float_rejects_invalid_values_and_boundaries(void);
void test_conv_float_consumes_values_and_preserves_diagnostics(void);
void test_conv_float_source_integration_and_arity(void);

static const TF_TestDescriptor tests[] = {
    {"rewrite.runtime.test_conv_bool_registry_contract", test_conv_bool_registry_contract, "exclusive", 30000, "api.libcall.conv,libcall.conv.bool"},
    {"rewrite.runtime.test_conv_bool_uses_runtime_truthiness", test_conv_bool_uses_runtime_truthiness, "exclusive", 30000, "api.libcall.conv,libcall.conv.bool"},
    {"rewrite.runtime.test_conv_bool_consumes_values_and_preserves_diagnostics", test_conv_bool_consumes_values_and_preserves_diagnostics, "exclusive", 30000, "api.libcall.conv,libcall.conv.bool"},
    {"rewrite.runtime.test_conv_bool_source_integration_and_arity", test_conv_bool_source_integration_and_arity, "exclusive", 30000, "api.libcall.conv,libcall.conv.bool"},
    {"rewrite.runtime.test_conv_str_registry_contract", test_conv_str_registry_contract, "exclusive", 30000, "api.libcall.conv,api.libcall.table,libcall.conv.str"},
    {"rewrite.runtime.test_conv_str_converts_values", test_conv_str_converts_values, "exclusive", 30000, "api.libcall.conv,libcall.conv.str"},
    {"rewrite.runtime.test_conv_str_consumes_values_and_preserves_diagnostics", test_conv_str_consumes_values_and_preserves_diagnostics, "exclusive", 30000, "api.libcall.conv,libcall.conv.str"},
    {"rewrite.runtime.test_conv_str_source_integration_and_arity", test_conv_str_source_integration_and_arity, "exclusive", 30000, "api.libcall.conv,libcall.conv.str"},
    {"rewrite.runtime.test_conv_int_registry_contract", test_conv_int_registry_contract, "exclusive", 30000, "api.libcall.conv,libcall.conv.int"},
    {"rewrite.runtime.test_conv_int_converts_values", test_conv_int_converts_values, "exclusive", 30000, "api.libcall.conv,libcall.conv.int"},
    {"rewrite.runtime.test_conv_int_rejects_invalid_values_and_boundaries", test_conv_int_rejects_invalid_values_and_boundaries, "exclusive", 30000, "api.libcall.conv,libcall.conv.int"},
    {"rewrite.runtime.test_conv_int_consumes_values_and_preserves_diagnostics", test_conv_int_consumes_values_and_preserves_diagnostics, "exclusive", 30000, "api.libcall.conv,libcall.conv.int"},
    {"rewrite.runtime.test_conv_int_source_integration_and_arity", test_conv_int_source_integration_and_arity, "exclusive", 30000, "api.libcall.conv,libcall.conv.int"},
    {"rewrite.runtime.test_conv_float_registry_contract", test_conv_float_registry_contract, "exclusive", 30000, "api.libcall.conv,api.libcall.table,libcall.conv.float"},
    {"rewrite.runtime.test_conv_float_converts_values", test_conv_float_converts_values, "exclusive", 30000, "api.libcall.conv,libcall.conv.float"},
    {"rewrite.runtime.test_conv_float_rejects_invalid_values_and_boundaries", test_conv_float_rejects_invalid_values_and_boundaries, "exclusive", 30000, "api.libcall.conv,libcall.conv.float"},
    {"rewrite.runtime.test_conv_float_consumes_values_and_preserves_diagnostics", test_conv_float_consumes_values_and_preserves_diagnostics, "exclusive", 30000, "api.libcall.conv,libcall.conv.float"},
    {"rewrite.runtime.test_conv_float_source_integration_and_arity", test_conv_float_source_integration_and_arity, "exclusive", 30000, "api.libcall.conv,libcall.conv.float"},
};

int main(int argc, char **argv) {
  return tf_main(argc, argv, tests, sizeof tests / sizeof tests[0]);
}
