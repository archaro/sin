#include "test_framework.h"

void test_math_abs_registry_contract(void);
void test_math_abs_integer_inputs(void);
void test_math_abs_float_inputs_and_signed_zero(void);
void test_math_abs_rejects_nonnumeric_and_consumes_owned_value(void);
void test_math_abs_undefined_inputs_publish_error(void);
void test_math_abs_success_preserves_existing_error(void);
void test_math_min_max_registry_contract(void);
void test_math_min_max_integer_inputs(void);
void test_math_min_max_mixed_and_float_inputs(void);
void test_math_min_max_equality_and_signed_zero(void);
void test_math_min_max_rejects_invalid_arguments_and_consumes_owned_values(void);
void test_math_min_max_undefined_inputs_publish_error(void);
void test_math_min_max_success_preserves_existing_error(void);
void test_math_sqrt_pow_registry_contract(void);
void test_math_sqrt_integer_and_float_inputs(void);
void test_math_pow_integer_float_inputs_and_result_type(void);
void test_math_sqrt_pow_reject_invalid_args_and_consume_owned_values(void);
void test_math_sqrt_negative_and_undefined_inputs_publish_errors(void);
void test_math_pow_undefined_inputs_and_results_publish_error(void);
void test_math_sqrt_pow_success_preserves_existing_error(void);
void test_math_log_registry_contract(void);
void test_math_log_integer_and_float_inputs_return_floats(void);
void test_math_log_rejects_nonnumeric_and_consumes_owned_values(void);
void test_math_log_rejects_nonpositive_inputs(void);
void test_math_log_undefined_inputs_publish_error(void);
void test_math_log_success_preserves_existing_error(void);
void test_math_exp_registry_contract(void);
void test_math_exp_integer_float_and_signed_zero_inputs(void);
void test_math_exp_finite_underflow_succeeds(void);
void test_math_exp_rejects_nonnumeric_and_consumes_owned_value(void);
void test_math_exp_undefined_inputs_publish_error(void);
void test_math_exp_success_preserves_existing_error(void);
void test_math_trig_registry_contract(void);
void test_math_trig_unary_values_return_floats(void);
void test_math_trig_atan2_ordering_and_quadrants(void);
void test_math_trig_inverse_endpoints_and_signed_zero(void);
void test_math_trig_rejects_nonnumeric_and_consumes_owned_values(void);
void test_math_trig_inverse_domain_errors(void);
void test_math_trig_undefined_inputs_and_tan_nonfinite_result(void);
void test_math_trig_success_preserves_existing_error(void);
void test_math_rounding_registry_contract(void);
void test_math_rounding_integer_inputs_preserve_identity(void);
void test_math_floor_and_ceil_float_inputs_return_integers(void);
void test_math_round_float_halfway_values_away_from_zero(void);
void test_math_rounding_float_representability_boundaries(void);
void test_math_rounding_rejects_nonnumeric_and_consumes_owned_values(void);
void test_math_rounding_undefined_inputs_publish_error(void);
void test_math_rounding_success_preserves_existing_error(void);

void test_math_float_result_stack_and_diagnostic_contract(void);

static const TF_TestDescriptor tests[] = {
    {"rewrite.runtime.test_math_float_result_stack_and_diagnostic_contract", test_math_float_result_stack_and_diagnostic_contract, "exclusive", 30000, "api.common.errors,api.libcall.math,libcall.math.sqrt,libcall.math.pow,libcall.math.log,libcall.math.exp,libcall.math.sin,libcall.math.atan2"},
    {"rewrite.runtime.test_math_abs_registry_contract", test_math_abs_registry_contract, "exclusive", 30000, "api.libcall.math,api.libcall.table,libcall.math.abs"},
    {"rewrite.runtime.test_math_abs_integer_inputs", test_math_abs_integer_inputs, "exclusive", 30000, "api.libcall.math,libcall.math.abs"},
    {"rewrite.runtime.test_math_abs_float_inputs_and_signed_zero", test_math_abs_float_inputs_and_signed_zero, "exclusive", 30000, "libcall.math.abs"},
    {"rewrite.runtime.test_math_abs_rejects_nonnumeric_and_consumes_owned_value", test_math_abs_rejects_nonnumeric_and_consumes_owned_value, "exclusive", 30000, "api.libcall.math,libcall.math.abs"},
    {"rewrite.runtime.test_math_abs_undefined_inputs_publish_error", test_math_abs_undefined_inputs_publish_error, "exclusive", 30000, "api.common.errors,api.libcall.math,libcall.math.abs"},
    {"rewrite.runtime.test_math_abs_success_preserves_existing_error", test_math_abs_success_preserves_existing_error, "exclusive", 30000, "api.libcall.math,libcall.math.abs"},
    {"rewrite.runtime.test_math_min_max_registry_contract", test_math_min_max_registry_contract, "exclusive", 30000, "api.libcall.math,api.libcall.table,libcall.math.min,libcall.math.max"},
    {"rewrite.runtime.test_math_min_max_integer_inputs", test_math_min_max_integer_inputs, "exclusive", 30000, "api.libcall.math,libcall.math.min,libcall.math.max"},
    {"rewrite.runtime.test_math_min_max_mixed_and_float_inputs", test_math_min_max_mixed_and_float_inputs, "exclusive", 30000, "api.libcall.math,libcall.math.min,libcall.math.max"},
    {"rewrite.runtime.test_math_min_max_equality_and_signed_zero", test_math_min_max_equality_and_signed_zero, "exclusive", 30000, "api.libcall.math,libcall.math.min,libcall.math.max"},
    {"rewrite.runtime.test_math_min_max_rejects_invalid_arguments_and_consumes_owned_values", test_math_min_max_rejects_invalid_arguments_and_consumes_owned_values, "exclusive", 30000, "api.libcall.math,libcall.math.min,libcall.math.max"},
    {"rewrite.runtime.test_math_min_max_undefined_inputs_publish_error", test_math_min_max_undefined_inputs_publish_error, "exclusive", 30000, "api.common.errors,api.libcall.math,libcall.math.min,libcall.math.max"},
    {"rewrite.runtime.test_math_min_max_success_preserves_existing_error", test_math_min_max_success_preserves_existing_error, "exclusive", 30000, "api.libcall.math,libcall.math.min,libcall.math.max"},
    {"rewrite.runtime.test_math_sqrt_pow_registry_contract", test_math_sqrt_pow_registry_contract, "exclusive", 30000, "api.libcall.math,api.libcall.table,libcall.math.sqrt,libcall.math.pow"},
    {"rewrite.runtime.test_math_sqrt_integer_and_float_inputs", test_math_sqrt_integer_and_float_inputs, "exclusive", 30000, "api.libcall.math,libcall.math.sqrt"},
    {"rewrite.runtime.test_math_pow_integer_float_inputs_and_result_type", test_math_pow_integer_float_inputs_and_result_type, "exclusive", 30000, "api.libcall.math,libcall.math.pow"},
    {"rewrite.runtime.test_math_sqrt_pow_reject_invalid_args_and_consume_owned_values", test_math_sqrt_pow_reject_invalid_args_and_consume_owned_values, "exclusive", 30000, "api.libcall.math,libcall.math.sqrt,libcall.math.pow"},
    {"rewrite.runtime.test_math_sqrt_negative_and_undefined_inputs_publish_errors", test_math_sqrt_negative_and_undefined_inputs_publish_errors, "exclusive", 30000, "api.common.errors,api.libcall.math,libcall.math.sqrt"},
    {"rewrite.runtime.test_math_pow_undefined_inputs_and_results_publish_error", test_math_pow_undefined_inputs_and_results_publish_error, "exclusive", 30000, "api.common.errors,api.libcall.math,libcall.math.pow"},
    {"rewrite.runtime.test_math_sqrt_pow_success_preserves_existing_error", test_math_sqrt_pow_success_preserves_existing_error, "exclusive", 30000, "api.libcall.math,libcall.math.sqrt,libcall.math.pow"},
    {"rewrite.runtime.test_math_rounding_registry_contract", test_math_rounding_registry_contract, "exclusive", 30000, "api.libcall.math,api.libcall.table,libcall.math.floor,libcall.math.ceil,libcall.math.round"},
    {"rewrite.runtime.test_math_rounding_integer_inputs_preserve_identity", test_math_rounding_integer_inputs_preserve_identity, "exclusive", 30000, "api.libcall.math,libcall.math.floor,libcall.math.ceil,libcall.math.round"},
    {"rewrite.runtime.test_math_floor_and_ceil_float_inputs_return_integers", test_math_floor_and_ceil_float_inputs_return_integers, "exclusive", 30000, "api.libcall.math,libcall.math.floor,libcall.math.ceil,libcall.math.round"},
    {"rewrite.runtime.test_math_round_float_halfway_values_away_from_zero", test_math_round_float_halfway_values_away_from_zero, "exclusive", 30000, "api.libcall.math,libcall.math.round"},
    {"rewrite.runtime.test_math_rounding_float_representability_boundaries", test_math_rounding_float_representability_boundaries, "exclusive", 30000, "api.common.errors,api.libcall.math,libcall.math.floor,libcall.math.ceil,libcall.math.round"},
    {"rewrite.runtime.test_math_rounding_rejects_nonnumeric_and_consumes_owned_values", test_math_rounding_rejects_nonnumeric_and_consumes_owned_values, "exclusive", 30000, "api.libcall.math,libcall.math.floor,libcall.math.ceil,libcall.math.round"},
    {"rewrite.runtime.test_math_rounding_undefined_inputs_publish_error", test_math_rounding_undefined_inputs_publish_error, "exclusive", 30000, "api.common.errors,api.libcall.math,libcall.math.floor,libcall.math.ceil,libcall.math.round"},
    {"rewrite.runtime.test_math_rounding_success_preserves_existing_error", test_math_rounding_success_preserves_existing_error, "exclusive", 30000, "api.libcall.math,libcall.math.floor,libcall.math.ceil,libcall.math.round"},
    {"rewrite.runtime.test_math_log_integer_and_float_inputs_return_floats", test_math_log_integer_and_float_inputs_return_floats, "exclusive", 30000, "api.libcall.math,libcall.math.log,libcall.math.log2,libcall.math.log10"},
    {"rewrite.runtime.test_math_log_rejects_nonpositive_inputs", test_math_log_rejects_nonpositive_inputs, "exclusive", 30000, "api.common.errors,api.libcall.math,libcall.math.log,libcall.math.log2,libcall.math.log10"},
    {"rewrite.runtime.test_math_log_rejects_nonnumeric_and_consumes_owned_values", test_math_log_rejects_nonnumeric_and_consumes_owned_values, "exclusive", 30000, "api.libcall.math,libcall.math.log,libcall.math.log2,libcall.math.log10"},
    {"rewrite.runtime.test_math_log_registry_contract", test_math_log_registry_contract, "exclusive", 30000, "api.libcall.math,api.libcall.table,libcall.math.log,libcall.math.log2,libcall.math.log10"},
    {"rewrite.runtime.test_math_log_success_preserves_existing_error", test_math_log_success_preserves_existing_error, "exclusive", 30000, "api.libcall.math,libcall.math.log,libcall.math.log2,libcall.math.log10"},
    {"rewrite.runtime.test_math_log_undefined_inputs_publish_error", test_math_log_undefined_inputs_publish_error, "exclusive", 30000, "api.common.errors,api.libcall.math,libcall.math.log,libcall.math.log2,libcall.math.log10"},
    {"rewrite.runtime.test_math_exp_integer_float_and_signed_zero_inputs", test_math_exp_integer_float_and_signed_zero_inputs, "exclusive", 30000, "api.libcall.math,libcall.math.exp"},
    {"rewrite.runtime.test_math_exp_finite_underflow_succeeds", test_math_exp_finite_underflow_succeeds, "exclusive", 30000, "api.libcall.math,libcall.math.exp"},
    {"rewrite.runtime.test_math_exp_rejects_nonnumeric_and_consumes_owned_value", test_math_exp_rejects_nonnumeric_and_consumes_owned_value, "exclusive", 30000, "api.libcall.math,libcall.math.exp"},
    {"rewrite.runtime.test_math_exp_registry_contract", test_math_exp_registry_contract, "exclusive", 30000, "api.libcall.math,api.libcall.table,libcall.math.exp"},
    {"rewrite.runtime.test_math_exp_success_preserves_existing_error", test_math_exp_success_preserves_existing_error, "exclusive", 30000, "api.libcall.math,libcall.math.exp"},
    {"rewrite.runtime.test_math_exp_undefined_inputs_publish_error", test_math_exp_undefined_inputs_publish_error, "exclusive", 30000, "api.common.errors,api.libcall.math,libcall.math.exp"},
    {"rewrite.runtime.test_math_trig_registry_contract", test_math_trig_registry_contract, "exclusive", 30000, "api.libcall.math,api.libcall.table,libcall.math.sin,libcall.math.cos,libcall.math.tan,libcall.math.asin,libcall.math.acos,libcall.math.atan,libcall.math.atan2"},
    {"rewrite.runtime.test_math_trig_unary_values_return_floats", test_math_trig_unary_values_return_floats, "exclusive", 30000, "api.libcall.math,libcall.math.sin,libcall.math.cos,libcall.math.tan,libcall.math.atan"},
    {"rewrite.runtime.test_math_trig_atan2_ordering_and_quadrants", test_math_trig_atan2_ordering_and_quadrants, "exclusive", 30000, "api.libcall.math,libcall.math.atan2"},
    {"rewrite.runtime.test_math_trig_inverse_endpoints_and_signed_zero", test_math_trig_inverse_endpoints_and_signed_zero, "exclusive", 30000, "api.libcall.math,libcall.math.asin,libcall.math.acos"},
    {"rewrite.runtime.test_math_trig_rejects_nonnumeric_and_consumes_owned_values", test_math_trig_rejects_nonnumeric_and_consumes_owned_values, "exclusive", 30000, "api.libcall.math,libcall.math.sin,libcall.math.cos,libcall.math.tan,libcall.math.asin,libcall.math.acos,libcall.math.atan,libcall.math.atan2"},
    {"rewrite.runtime.test_math_trig_inverse_domain_errors", test_math_trig_inverse_domain_errors, "exclusive", 30000, "api.common.errors,api.libcall.math,libcall.math.asin,libcall.math.acos"},
    {"rewrite.runtime.test_math_trig_undefined_inputs_and_tan_nonfinite_result", test_math_trig_undefined_inputs_and_tan_nonfinite_result, "exclusive", 30000, "api.common.errors,api.libcall.math,libcall.math.sin,libcall.math.cos,libcall.math.tan,libcall.math.asin,libcall.math.acos,libcall.math.atan,libcall.math.atan2"},
    {"rewrite.runtime.test_math_trig_success_preserves_existing_error", test_math_trig_success_preserves_existing_error, "exclusive", 30000, "api.libcall.math,libcall.math.sin,libcall.math.atan2"},
};

int main(int argc, char **argv) {
  return tf_main(argc, argv, tests, sizeof tests / sizeof tests[0]);
}
