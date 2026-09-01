#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "config.h"
#include "error.h"
#include "item.h"
#include "libcall.h"
#include "libcall_handlers.h"
#include "stack.h"
#include "test_assert.h"
#include "test_helpers.h"
#include "value.h"

#include "shared/test_libcall_support.h"

extern CONFIG_t config;

static void assert_error(int expected_code, const char *expected_message) {
  ITEM_t *error = find_item(itemstore_root(config.itemstore_ctx), "error");
  ITEM_t *message = find_item(itemstore_root(config.itemstore_ctx), "error.msg");
  ASSERT_NOT_NULL(error);
  ASSERT_NOT_NULL(message);
  ASSERT_EQ_INT(expected_code, item_value(error)->i);
  ASSERT_EQ_INT(VALUE_str, item_value(message)->type);
  ASSERT_TRUE(strcmp(expected_message, item_value(message)->s) == 0);
}

static VALUE_t call_abs(VALUE_t input) {
  push_stack(config.vm->stack, input);
  (void)lc_math_abs(test_ctx(), NULL, itemstore_root(config.itemstore_ctx));
  return pop_stack(config.vm->stack);
}

void test_math_abs_registry_contract(void) {
  uint8_t lib_index = 0;
  uint8_t call_index = 0;
  uint8_t args = 0;
  size_t count = 0;

  for (size_t i = 0; libcalls[i].libname != NULL; i++) count++;
  ASSERT_TRUE(libcall_lookup_pair("math", "abs", &lib_index, &call_index,
                                  &args));
  ASSERT_EQ_INT(6, lib_index);
  ASSERT_EQ_INT(0, call_index);
  ASSERT_EQ_INT(1, args);
  ASSERT_TRUE(libcall_func_pair(lib_index, call_index) == lc_math_abs);
  ASSERT_TRUE(libcall_pair_arg_count(lib_index, call_index, &args));
  ASSERT_EQ_INT(1, args);
  ASSERT_EQ_INT(64, count);
}

void test_math_abs_integer_inputs(void) {
  setup_libcall_runtime();
  ASSERT_EQ_INT(0, size_stack(config.vm->stack));

  VALUE_t result = call_abs((VALUE_t){VALUE_int, {.i = 42}});
  ASSERT_EQ_INT(VALUE_int, result.type);
  ASSERT_EQ_INT(42, result.i);
  result = call_abs((VALUE_t){VALUE_int, {.i = -42}});
  ASSERT_EQ_INT(VALUE_int, result.type);
  ASSERT_EQ_INT(42, result.i);
  result = call_abs((VALUE_t){VALUE_int, {.i = 0}});
  ASSERT_EQ_INT(VALUE_int, result.type);
  ASSERT_EQ_INT(0, result.i);
  ASSERT_EQ_INT(0, size_stack(config.vm->stack));
  teardown_libcall_runtime();
}

void test_math_abs_float_inputs_and_signed_zero(void) {
  setup_libcall_runtime();

  VALUE_t result = call_abs((VALUE_t){VALUE_float, {.f = 3.5}});
  ASSERT_EQ_INT(VALUE_float, result.type);
  ASSERT_TRUE(result.f == 3.5);
  result = call_abs((VALUE_t){VALUE_float, {.f = -3.5}});
  ASSERT_EQ_INT(VALUE_float, result.type);
  ASSERT_TRUE(result.f == 3.5);
  result = call_abs((VALUE_t){VALUE_float, {.f = 0.0}});
  ASSERT_EQ_INT(VALUE_float, result.type);
  ASSERT_TRUE(result.f == 0.0 && !signbit(result.f));
  result = call_abs((VALUE_t){VALUE_float, {.f = -0.0}});
  ASSERT_EQ_INT(VALUE_float, result.type);
  ASSERT_TRUE(result.f == 0.0 && !signbit(result.f));

  teardown_libcall_runtime();
}

void test_math_abs_rejects_nonnumeric_and_consumes_owned_value(void) {
  setup_libcall_runtime();
  int before = size_stack(config.vm->stack);
  char *payload = strdup("owned by the argument");
  ASSERT_NOT_NULL(payload);
  VALUE_t result = call_abs((VALUE_t){VALUE_str, {.s = payload}});
  ASSERT_EQ_INT(VALUE_nil, result.type);
  ASSERT_EQ_INT(before, size_stack(config.vm->stack));
  assert_error(ERR_RUNTIME_INVALIDARGS,
               "Invalid arguments to library call. (math.abs expects an integer or float)");
  teardown_libcall_runtime();
}

void test_math_abs_undefined_inputs_publish_error(void) {
  const double undefined_values[] = {NAN, INFINITY, -INFINITY};

  setup_libcall_runtime();
  VALUE_t result = call_abs((VALUE_t){VALUE_int, {.i = INT64_MIN}});
  ASSERT_EQ_INT(VALUE_nil, result.type);
  assert_error(ERR_RUNTIME_UNDEFINED, errmsg[ERR_RUNTIME_UNDEFINED]);
  teardown_libcall_runtime();

  for (size_t i = 0; i < sizeof(undefined_values) / sizeof(undefined_values[0]); i++) {
    setup_libcall_runtime();
    result = call_abs((VALUE_t){VALUE_float, {.f = undefined_values[i]}});
    ASSERT_EQ_INT(VALUE_nil, result.type);
    assert_error(ERR_RUNTIME_UNDEFINED, errmsg[ERR_RUNTIME_UNDEFINED]);
    teardown_libcall_runtime();
  }
}

void test_math_abs_success_preserves_existing_error(void) {
  setup_libcall_runtime();
  set_error_item(itemstore_root(config.itemstore_ctx), ERR_RUNTIME_INVALIDARGS,
                 "prior error", NULL);
  VALUE_t result = call_abs((VALUE_t){VALUE_int, {.i = -7}});
  ASSERT_EQ_INT(VALUE_int, result.type);
  ASSERT_EQ_INT(7, result.i);
  assert_error(ERR_RUNTIME_INVALIDARGS,
               "Invalid arguments to library call. (prior error)");
  teardown_libcall_runtime();
}
