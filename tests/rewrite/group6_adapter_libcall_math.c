#include "test_framework.h"

void test_math_abs_registry_contract(void);
void test_math_abs_integer_inputs(void);
void test_math_abs_float_inputs_and_signed_zero(void);
void test_math_abs_rejects_nonnumeric_and_consumes_owned_value(void);
void test_math_abs_undefined_inputs_publish_error(void);
void test_math_abs_success_preserves_existing_error(void);

static const TF_TestDescriptor tests[] = {
    {"rewrite.runtime.test_math_abs_registry_contract", test_math_abs_registry_contract, "exclusive", 30000, "api.libcall.math,api.libcall.table,libcall.math.abs"},
    {"rewrite.runtime.test_math_abs_integer_inputs", test_math_abs_integer_inputs, "exclusive", 30000, "api.libcall.math,libcall.math.abs"},
    {"rewrite.runtime.test_math_abs_float_inputs_and_signed_zero", test_math_abs_float_inputs_and_signed_zero, "exclusive", 30000, "libcall.math.abs"},
    {"rewrite.runtime.test_math_abs_rejects_nonnumeric_and_consumes_owned_value", test_math_abs_rejects_nonnumeric_and_consumes_owned_value, "exclusive", 30000, "api.libcall.math,libcall.math.abs"},
    {"rewrite.runtime.test_math_abs_undefined_inputs_publish_error", test_math_abs_undefined_inputs_publish_error, "exclusive", 30000, "api.common.errors,api.libcall.math,libcall.math.abs"},
    {"rewrite.runtime.test_math_abs_success_preserves_existing_error", test_math_abs_success_preserves_existing_error, "exclusive", 30000, "api.libcall.math,libcall.math.abs"},
};

int main(int argc, char **argv) {
  return tf_main(argc, argv, tests, sizeof tests / sizeof tests[0]);
}
