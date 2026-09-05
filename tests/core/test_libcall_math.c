#include <math.h>
#include <stdio.h>
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

static VALUE_t call_math_unary(OP_t handler, VALUE_t input) {
  push_stack(config.vm->stack, input);
  (void)handler(test_ctx(), NULL, itemstore_root(config.itemstore_ctx));
  return pop_stack(config.vm->stack);
}

static VALUE_t call_math_binary(OP_t handler, VALUE_t left, VALUE_t right) {
  push_stack(config.vm->stack, left);
  push_stack(config.vm->stack, right);
  (void)handler(test_ctx(), NULL, itemstore_root(config.itemstore_ctx));
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
  ASSERT_EQ_INT(86, count);
}

void test_math_rounding_registry_contract(void) {
  struct manifest {
    const char *name;
    uint8_t call_index;
    OP_t handler;
  };
  static const struct manifest manifest[] = {
      {"floor", 3, lc_math_floor},
      {"ceil", 4, lc_math_ceil},
      {"round", 5, lc_math_round},
  };
  size_t count = 0;

  for (size_t i = 0; libcalls[i].libname != NULL; i++) count++;
  ASSERT_EQ_INT(86, count);
  for (size_t i = 0; i < sizeof(manifest) / sizeof(manifest[0]); i++) {
    uint8_t lib_index = 0;
    uint8_t call_index = 0;
    uint8_t args = 0;
    ASSERT_TRUE(libcall_lookup_pair("math", manifest[i].name, &lib_index,
                                    &call_index, &args));
    ASSERT_EQ_INT(6, lib_index);
    ASSERT_EQ_INT(manifest[i].call_index, call_index);
    ASSERT_EQ_INT(1, args);
    ASSERT_TRUE(libcall_func_pair(lib_index, call_index) == manifest[i].handler);
    ASSERT_TRUE(libcall_pair_arg_count(lib_index, call_index, &args));
    ASSERT_EQ_INT(1, args);
  }
}

void test_math_min_max_registry_contract(void) {
  struct manifest {
    const char *name;
    uint8_t call_index;
    OP_t handler;
  };
  static const struct manifest manifest[] = {
      {"min", 1, lc_math_min},
      {"max", 2, lc_math_max},
  };

  for (size_t i = 0; i < sizeof(manifest) / sizeof(manifest[0]); i++) {
    uint8_t lib_index = 0;
    uint8_t call_index = 0;
    uint8_t args = 0;
    ASSERT_TRUE(libcall_lookup_pair("math", manifest[i].name, &lib_index,
                                    &call_index, &args));
    ASSERT_EQ_INT(6, lib_index);
    ASSERT_EQ_INT(manifest[i].call_index, call_index);
    ASSERT_EQ_INT(2, args);
    ASSERT_TRUE(libcall_func_pair(lib_index, call_index) == manifest[i].handler);
    ASSERT_TRUE(libcall_pair_arg_count(lib_index, call_index, &args));
    ASSERT_EQ_INT(2, args);
  }
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

void test_math_rounding_integer_inputs_preserve_identity(void) {
  const OP_t operations[] = {lc_math_floor, lc_math_ceil, lc_math_round};
  const int64_t values[] = {INT64_MIN, -42, 0, 42, INT64_MAX};

  setup_libcall_runtime();
  for (size_t op = 0; op < sizeof(operations) / sizeof(operations[0]); op++) {
    for (size_t i = 0; i < sizeof(values) / sizeof(values[0]); i++) {
      VALUE_t result = call_math_unary(
          operations[op], (VALUE_t){VALUE_int, {.i = values[i]}});
      ASSERT_EQ_INT(VALUE_int, result.type);
      ASSERT_EQ_INT(values[i], result.i);
      ASSERT_EQ_INT(0, size_stack(config.vm->stack));
    }
  }
  teardown_libcall_runtime();
}

void test_math_floor_and_ceil_float_inputs_return_integers(void) {
  setup_libcall_runtime();

  VALUE_t result = call_math_unary(lc_math_floor,
                                   (VALUE_t){VALUE_float, {.f = 3.75}});
  ASSERT_EQ_INT(VALUE_int, result.type);
  ASSERT_EQ_INT(3, result.i);
  result = call_math_unary(lc_math_floor,
                           (VALUE_t){VALUE_float, {.f = -3.25}});
  ASSERT_EQ_INT(VALUE_int, result.type);
  ASSERT_EQ_INT(-4, result.i);
  result = call_math_unary(lc_math_ceil,
                           (VALUE_t){VALUE_float, {.f = 3.25}});
  ASSERT_EQ_INT(VALUE_int, result.type);
  ASSERT_EQ_INT(4, result.i);
  result = call_math_unary(lc_math_ceil,
                           (VALUE_t){VALUE_float, {.f = -3.75}});
  ASSERT_EQ_INT(VALUE_int, result.type);
  ASSERT_EQ_INT(-3, result.i);
  result = call_math_unary(lc_math_floor,
                           (VALUE_t){VALUE_float, {.f = 0.0}});
  ASSERT_EQ_INT(VALUE_int, result.type);
  ASSERT_EQ_INT(0, result.i);
  result = call_math_unary(lc_math_ceil,
                           (VALUE_t){VALUE_float, {.f = -0.0}});
  ASSERT_EQ_INT(VALUE_int, result.type);
  ASSERT_EQ_INT(0, result.i);
  result = call_math_unary(lc_math_round,
                           (VALUE_t){VALUE_float, {.f = 0.0}});
  ASSERT_EQ_INT(VALUE_int, result.type);
  ASSERT_EQ_INT(0, result.i);
  result = call_math_unary(lc_math_round,
                           (VALUE_t){VALUE_float, {.f = -0.0}});
  ASSERT_EQ_INT(VALUE_int, result.type);
  ASSERT_EQ_INT(0, result.i);
  ASSERT_EQ_INT(0, size_stack(config.vm->stack));

  teardown_libcall_runtime();
}

void test_math_round_float_halfway_values_away_from_zero(void) {
  setup_libcall_runtime();

  const struct {
    double input;
    int64_t expected;
  } cases[] = {{0.5, 1}, {-0.5, -1}, {1.5, 2}, {-1.5, -2},
               {2.5, 3}, {-2.5, -3}, {0.49, 0}, {-0.51, -1}};
  for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
    VALUE_t result = call_math_unary(
        lc_math_round, (VALUE_t){VALUE_float, {.f = cases[i].input}});
    ASSERT_EQ_INT(VALUE_int, result.type);
    ASSERT_EQ_INT(cases[i].expected, result.i);
  }
  ASSERT_EQ_INT(0, size_stack(config.vm->stack));

  teardown_libcall_runtime();
}

void test_math_rounding_float_representability_boundaries(void) {
  setup_libcall_runtime();

  const OP_t operations[] = {lc_math_floor, lc_math_ceil, lc_math_round};
  for (size_t i = 0; i < sizeof(operations) / sizeof(operations[0]); i++) {
    VALUE_t result = call_math_unary(
        operations[i], (VALUE_t){VALUE_float, {.f = -0x1p63}});
    ASSERT_EQ_INT(VALUE_int, result.type);
    ASSERT_EQ_INT(INT64_MIN, result.i);
    result = call_math_unary(
        operations[i],
        (VALUE_t){VALUE_float, {.f = nextafter(0x1p63, 0.0)}});
    ASSERT_EQ_INT(VALUE_int, result.type);
    ASSERT_EQ_INT((int64_t)nextafter(0x1p63, 0.0), result.i);
  }
  ASSERT_EQ_INT(0, size_stack(config.vm->stack));
  teardown_libcall_runtime();

  setup_libcall_runtime();
  const OP_t undefined_operations[] = {lc_math_floor, lc_math_ceil,
                                       lc_math_round};
  for (size_t i = 0; i < sizeof(undefined_operations) /
                             sizeof(undefined_operations[0]);
       i++) {
    VALUE_t result = call_math_unary(
        undefined_operations[i], (VALUE_t){VALUE_float, {.f = 0x1p63}});
    ASSERT_EQ_INT(VALUE_nil, result.type);
    ASSERT_EQ_INT(0, size_stack(config.vm->stack));
    assert_error(ERR_RUNTIME_UNDEFINED, errmsg[ERR_RUNTIME_UNDEFINED]);
  }
  teardown_libcall_runtime();
}

void test_math_rounding_rejects_nonnumeric_and_consumes_owned_values(void) {
  const struct {
    OP_t handler;
    const char *detail;
  } operations[] = {
      {lc_math_floor,
       "Invalid arguments to library call. (math.floor expects an integer or float)"},
      {lc_math_ceil,
       "Invalid arguments to library call. (math.ceil expects an integer or float)"},
      {lc_math_round,
       "Invalid arguments to library call. (math.round expects an integer or float)"},
  };

  for (size_t i = 0; i < sizeof(operations) / sizeof(operations[0]); i++) {
    setup_libcall_runtime();
    int before = size_stack(config.vm->stack);
    char *payload = strdup("owned by the argument");
    ASSERT_NOT_NULL(payload);
    VALUE_t result = call_math_unary(
        operations[i].handler, (VALUE_t){VALUE_str, {.s = payload}});
    ASSERT_EQ_INT(VALUE_nil, result.type);
    ASSERT_EQ_INT(before, size_stack(config.vm->stack));
    assert_error(ERR_RUNTIME_INVALIDARGS, operations[i].detail);
    teardown_libcall_runtime();
  }
}

void test_math_rounding_undefined_inputs_publish_error(void) {
  const double undefined_values[] = {
      NAN, INFINITY, -INFINITY, 0x1p63,
      nextafter(-0x1p63, -INFINITY),
  };
  const OP_t operations[] = {lc_math_floor, lc_math_ceil, lc_math_round};

  for (size_t op = 0; op < sizeof(operations) / sizeof(operations[0]); op++) {
    for (size_t i = 0; i < sizeof(undefined_values) /
                               sizeof(undefined_values[0]);
         i++) {
      setup_libcall_runtime();
      VALUE_t result = call_math_unary(
          operations[op], (VALUE_t){VALUE_float, {.f = undefined_values[i]}});
      ASSERT_EQ_INT(VALUE_nil, result.type);
      ASSERT_EQ_INT(0, size_stack(config.vm->stack));
      assert_error(ERR_RUNTIME_UNDEFINED, errmsg[ERR_RUNTIME_UNDEFINED]);
      teardown_libcall_runtime();
    }
  }
}

void test_math_rounding_success_preserves_existing_error(void) {
  const OP_t operations[] = {lc_math_floor, lc_math_ceil, lc_math_round};
  for (size_t i = 0; i < sizeof(operations) / sizeof(operations[0]); i++) {
    setup_libcall_runtime();
    set_error_item(itemstore_root(config.itemstore_ctx), ERR_RUNTIME_INVALIDARGS,
                   "prior error", NULL);
    VALUE_t result = call_math_unary(
        operations[i], (VALUE_t){VALUE_float, {.f = -3.25}});
    ASSERT_EQ_INT(VALUE_int, result.type);
    ASSERT_EQ_INT(i == 0 ? -4 : (i == 1 ? -3 : -3), result.i);
    ASSERT_EQ_INT(0, size_stack(config.vm->stack));
    assert_error(ERR_RUNTIME_INVALIDARGS,
                 "Invalid arguments to library call. (prior error)");
    teardown_libcall_runtime();
  }
}

void test_math_min_max_integer_inputs(void) {
  setup_libcall_runtime();
  ASSERT_EQ_INT(0, size_stack(config.vm->stack));

  VALUE_t result = call_math_binary(
      lc_math_min, (VALUE_t){VALUE_int, {.i = 8}},
      (VALUE_t){VALUE_int, {.i = -2}});
  ASSERT_EQ_INT(VALUE_int, result.type);
  ASSERT_EQ_INT(-2, result.i);
  result = call_math_binary(
      lc_math_max, (VALUE_t){VALUE_int, {.i = 8}},
      (VALUE_t){VALUE_int, {.i = -2}});
  ASSERT_EQ_INT(VALUE_int, result.type);
  ASSERT_EQ_INT(8, result.i);
  ASSERT_EQ_INT(0, size_stack(config.vm->stack));
  teardown_libcall_runtime();
}

void test_math_min_max_mixed_and_float_inputs(void) {
  setup_libcall_runtime();

  VALUE_t result = call_math_binary(
      lc_math_min, (VALUE_t){VALUE_int, {.i = 3}},
      (VALUE_t){VALUE_float, {.f = 4.5}});
  ASSERT_EQ_INT(VALUE_float, result.type);
  ASSERT_TRUE(result.f == 3.0);
  result = call_math_binary(
      lc_math_min, (VALUE_t){VALUE_float, {.f = 4.5}},
      (VALUE_t){VALUE_int, {.i = 3}});
  ASSERT_EQ_INT(VALUE_float, result.type);
  ASSERT_TRUE(result.f == 3.0);
  result = call_math_binary(
      lc_math_max, (VALUE_t){VALUE_int, {.i = 3}},
      (VALUE_t){VALUE_float, {.f = 4.5}});
  ASSERT_EQ_INT(VALUE_float, result.type);
  ASSERT_TRUE(result.f == 4.5);
  result = call_math_binary(
      lc_math_max, (VALUE_t){VALUE_float, {.f = 4.5}},
      (VALUE_t){VALUE_int, {.i = 3}});
  ASSERT_EQ_INT(VALUE_float, result.type);
  ASSERT_TRUE(result.f == 4.5);
  result = call_math_binary(
      lc_math_min, (VALUE_t){VALUE_float, {.f = 8.5}},
      (VALUE_t){VALUE_float, {.f = 2.25}});
  ASSERT_EQ_INT(VALUE_float, result.type);
  ASSERT_TRUE(result.f == 2.25);
  result = call_math_binary(
      lc_math_max, (VALUE_t){VALUE_float, {.f = 2.25}},
      (VALUE_t){VALUE_float, {.f = 8.5}});
  ASSERT_EQ_INT(VALUE_float, result.type);
  ASSERT_TRUE(result.f == 8.5);
  ASSERT_EQ_INT(0, size_stack(config.vm->stack));

  teardown_libcall_runtime();
}

void test_math_min_max_equality_and_signed_zero(void) {
  setup_libcall_runtime();

  VALUE_t result = call_math_binary(
      lc_math_min, (VALUE_t){VALUE_int, {.i = 7}},
      (VALUE_t){VALUE_int, {.i = 7}});
  ASSERT_EQ_INT(VALUE_int, result.type);
  ASSERT_EQ_INT(7, result.i);
  result = call_math_binary(
      lc_math_max, (VALUE_t){VALUE_float, {.f = 7.0}},
      (VALUE_t){VALUE_float, {.f = 7.0}});
  ASSERT_EQ_INT(VALUE_float, result.type);
  ASSERT_TRUE(result.f == 7.0);
  result = call_math_binary(
      lc_math_min, (VALUE_t){VALUE_int, {.i = 7}},
      (VALUE_t){VALUE_float, {.f = 7.0}});
  ASSERT_EQ_INT(VALUE_float, result.type);
  ASSERT_TRUE(result.f == 7.0);
  result = call_math_binary(
      lc_math_max, (VALUE_t){VALUE_float, {.f = 7.0}},
      (VALUE_t){VALUE_int, {.i = 7}});
  ASSERT_EQ_INT(VALUE_float, result.type);
  ASSERT_TRUE(result.f == 7.0);

  result = call_math_binary(
      lc_math_min, (VALUE_t){VALUE_float, {.f = 0.0}},
      (VALUE_t){VALUE_float, {.f = -0.0}});
  ASSERT_EQ_INT(VALUE_float, result.type);
  ASSERT_TRUE(result.f == 0.0 && signbit(result.f));
  result = call_math_binary(
      lc_math_min, (VALUE_t){VALUE_float, {.f = -0.0}},
      (VALUE_t){VALUE_float, {.f = 0.0}});
  ASSERT_EQ_INT(VALUE_float, result.type);
  ASSERT_TRUE(result.f == 0.0 && signbit(result.f));
  result = call_math_binary(
      lc_math_max, (VALUE_t){VALUE_float, {.f = 0.0}},
      (VALUE_t){VALUE_float, {.f = -0.0}});
  ASSERT_EQ_INT(VALUE_float, result.type);
  ASSERT_TRUE(result.f == 0.0 && !signbit(result.f));
  result = call_math_binary(
      lc_math_max, (VALUE_t){VALUE_float, {.f = -0.0}},
      (VALUE_t){VALUE_float, {.f = 0.0}});
  ASSERT_EQ_INT(VALUE_float, result.type);
  ASSERT_TRUE(result.f == 0.0 && !signbit(result.f));
  ASSERT_EQ_INT(0, size_stack(config.vm->stack));

  teardown_libcall_runtime();
}

void test_math_min_max_rejects_invalid_arguments_and_consumes_owned_values(void) {
  setup_libcall_runtime();
  int before = size_stack(config.vm->stack);
  VALUE_t result = call_math_binary(
      lc_math_min, VALUE_NIL, (VALUE_t){VALUE_int, {.i = 2}});
  ASSERT_EQ_INT(VALUE_nil, result.type);
  ASSERT_EQ_INT(before, size_stack(config.vm->stack));
  assert_error(ERR_RUNTIME_INVALIDARGS,
               "Invalid arguments to library call. (math.min expects two integer or float arguments)");
  teardown_libcall_runtime();

  setup_libcall_runtime();
  before = size_stack(config.vm->stack);
  result = call_math_binary(
      lc_math_max, (VALUE_t){VALUE_int, {.i = 2}}, VALUE_NIL);
  ASSERT_EQ_INT(VALUE_nil, result.type);
  ASSERT_EQ_INT(before, size_stack(config.vm->stack));
  assert_error(ERR_RUNTIME_INVALIDARGS,
               "Invalid arguments to library call. (math.max expects two integer or float arguments)");
  teardown_libcall_runtime();

  setup_libcall_runtime();
  before = size_stack(config.vm->stack);
  char *left_payload = strdup("owned invalid left");
  ASSERT_NOT_NULL(left_payload);
  result = call_math_binary(
      lc_math_min, (VALUE_t){VALUE_str, {.s = left_payload}},
      (VALUE_t){VALUE_int, {.i = 2}});
  ASSERT_EQ_INT(VALUE_nil, result.type);
  ASSERT_EQ_INT(before, size_stack(config.vm->stack));
  assert_error(ERR_RUNTIME_INVALIDARGS,
               "Invalid arguments to library call. (math.min expects two integer or float arguments)");
  teardown_libcall_runtime();

  setup_libcall_runtime();
  before = size_stack(config.vm->stack);
  char *right_payload = strdup("owned invalid right");
  ASSERT_NOT_NULL(right_payload);
  result = call_math_binary(
      lc_math_max, (VALUE_t){VALUE_int, {.i = 2}},
      (VALUE_t){VALUE_str, {.s = right_payload}});
  ASSERT_EQ_INT(VALUE_nil, result.type);
  ASSERT_EQ_INT(before, size_stack(config.vm->stack));
  assert_error(ERR_RUNTIME_INVALIDARGS,
               "Invalid arguments to library call. (math.max expects two integer or float arguments)");
  teardown_libcall_runtime();
}

void test_math_min_max_undefined_inputs_publish_error(void) {
  const double undefined_values[] = {NAN, INFINITY, -INFINITY};
  const OP_t operations[] = {lc_math_min, lc_math_max};

  for (size_t op = 0; op < sizeof(operations) / sizeof(operations[0]); op++) {
    for (size_t i = 0; i < sizeof(undefined_values) / sizeof(undefined_values[0]); i++) {
      setup_libcall_runtime();
      VALUE_t result = call_math_binary(
          operations[op], (VALUE_t){VALUE_float, {.f = undefined_values[i]}},
          (VALUE_t){VALUE_int, {.i = 1}});
      ASSERT_EQ_INT(VALUE_nil, result.type);
      ASSERT_EQ_INT(0, size_stack(config.vm->stack));
      assert_error(ERR_RUNTIME_UNDEFINED, errmsg[ERR_RUNTIME_UNDEFINED]);
      teardown_libcall_runtime();

      setup_libcall_runtime();
      result = call_math_binary(
          operations[op], (VALUE_t){VALUE_int, {.i = 1}},
          (VALUE_t){VALUE_float, {.f = undefined_values[i]}});
      ASSERT_EQ_INT(VALUE_nil, result.type);
      ASSERT_EQ_INT(0, size_stack(config.vm->stack));
      assert_error(ERR_RUNTIME_UNDEFINED, errmsg[ERR_RUNTIME_UNDEFINED]);
      teardown_libcall_runtime();
    }
  }
}

void test_math_min_max_success_preserves_existing_error(void) {
  const OP_t operations[] = {lc_math_min, lc_math_max};

  for (size_t i = 0; i < sizeof(operations) / sizeof(operations[0]); i++) {
    setup_libcall_runtime();
    set_error_item(itemstore_root(config.itemstore_ctx), ERR_RUNTIME_INVALIDARGS,
                   "prior error", NULL);
    VALUE_t result = call_math_binary(
        operations[i], (VALUE_t){VALUE_int, {.i = 3}},
        (VALUE_t){VALUE_int, {.i = 7}});
    ASSERT_EQ_INT(VALUE_int, result.type);
    ASSERT_EQ_INT(i == 0 ? 3 : 7, result.i);
    ASSERT_EQ_INT(0, size_stack(config.vm->stack));
    assert_error(ERR_RUNTIME_INVALIDARGS,
                 "Invalid arguments to library call. (prior error)");
    teardown_libcall_runtime();
  }
}

void test_math_sqrt_pow_registry_contract(void) {
  const struct {
    const char *name;
    uint8_t call_index;
    uint8_t args;
    OP_t handler;
  } manifest[] = {
      {"sqrt", 6, 1, lc_math_sqrt},
      {"pow", 7, 2, lc_math_pow},
  };
  size_t count = 0;

  for (size_t i = 0; libcalls[i].libname != NULL; i++) count++;
  ASSERT_EQ_INT(86, count);
  for (size_t i = 0; i < sizeof(manifest) / sizeof(manifest[0]); i++) {
    uint8_t lib_index = 0;
    uint8_t call_index = 0;
    uint8_t args = 0;
    ASSERT_TRUE(libcall_lookup_pair("math", manifest[i].name, &lib_index,
                                   &call_index, &args));
    ASSERT_EQ_INT(6, lib_index);
    ASSERT_EQ_INT(manifest[i].call_index, call_index);
    ASSERT_EQ_INT(manifest[i].args, args);
    ASSERT_TRUE(libcall_func_pair(lib_index, call_index) == manifest[i].handler);
    ASSERT_TRUE(libcall_pair_arg_count(lib_index, call_index, &args));
    ASSERT_EQ_INT(manifest[i].args, args);
  }
}

void test_math_sqrt_integer_and_float_inputs(void) {
  setup_libcall_runtime();

  VALUE_t result = call_math_unary(lc_math_sqrt,
                                   (VALUE_t){VALUE_int, {.i = 9}});
  ASSERT_EQ_INT(VALUE_float, result.type);
  ASSERT_TRUE(result.f == 3.0);
  result = call_math_unary(lc_math_sqrt,
                           (VALUE_t){VALUE_float, {.f = 2.25}});
  ASSERT_EQ_INT(VALUE_float, result.type);
  ASSERT_TRUE(result.f == 1.5);
  result = call_math_unary(lc_math_sqrt,
                           (VALUE_t){VALUE_float, {.f = -0.0}});
  ASSERT_EQ_INT(VALUE_float, result.type);
  ASSERT_TRUE(result.f == 0.0 && signbit(result.f));
  ASSERT_EQ_INT(0, size_stack(config.vm->stack));

  teardown_libcall_runtime();
}

void test_math_pow_integer_float_inputs_and_result_type(void) {
  setup_libcall_runtime();

  VALUE_t result = call_math_binary(
      lc_math_pow, (VALUE_t){VALUE_int, {.i = 2}},
      (VALUE_t){VALUE_int, {.i = 10}});
  ASSERT_EQ_INT(VALUE_float, result.type);
  ASSERT_TRUE(result.f == 1024.0);
  result = call_math_binary(
      lc_math_pow, (VALUE_t){VALUE_float, {.f = 9.0}},
      (VALUE_t){VALUE_int, {.i = 2}});
  ASSERT_EQ_INT(VALUE_float, result.type);
  ASSERT_TRUE(result.f == 81.0);
  result = call_math_binary(
      lc_math_pow, (VALUE_t){VALUE_int, {.i = 4}},
      (VALUE_t){VALUE_float, {.f = 0.5}});
  ASSERT_EQ_INT(VALUE_float, result.type);
  ASSERT_TRUE(result.f == 2.0);
  ASSERT_EQ_INT(0, size_stack(config.vm->stack));

  teardown_libcall_runtime();
}

void test_math_sqrt_pow_reject_invalid_args_and_consume_owned_values(void) {
  setup_libcall_runtime();
  int before = size_stack(config.vm->stack);
  char *payload = strdup("owned by the argument");
  ASSERT_NOT_NULL(payload);
  VALUE_t result = call_math_unary(
      lc_math_sqrt, (VALUE_t){VALUE_str, {.s = payload}});
  ASSERT_EQ_INT(VALUE_nil, result.type);
  ASSERT_EQ_INT(before, size_stack(config.vm->stack));
  assert_error(ERR_RUNTIME_INVALIDARGS,
               "Invalid arguments to library call. (math.sqrt expects an integer or float)");
  teardown_libcall_runtime();

  setup_libcall_runtime();
  before = size_stack(config.vm->stack);
  payload = strdup("owned invalid base");
  ASSERT_NOT_NULL(payload);
  result = call_math_binary(
      lc_math_pow, (VALUE_t){VALUE_str, {.s = payload}},
      (VALUE_t){VALUE_int, {.i = 2}});
  ASSERT_EQ_INT(VALUE_nil, result.type);
  ASSERT_EQ_INT(before, size_stack(config.vm->stack));
  assert_error(ERR_RUNTIME_INVALIDARGS,
               "Invalid arguments to library call. (math.pow expects two integer or float arguments)");
  teardown_libcall_runtime();

  setup_libcall_runtime();
  before = size_stack(config.vm->stack);
  payload = strdup("owned invalid exponent");
  ASSERT_NOT_NULL(payload);
  result = call_math_binary(
      lc_math_pow, (VALUE_t){VALUE_int, {.i = 2}},
      (VALUE_t){VALUE_str, {.s = payload}});
  ASSERT_EQ_INT(VALUE_nil, result.type);
  ASSERT_EQ_INT(before, size_stack(config.vm->stack));
  assert_error(ERR_RUNTIME_INVALIDARGS,
               "Invalid arguments to library call. (math.pow expects two integer or float arguments)");
  teardown_libcall_runtime();
}

void test_math_sqrt_negative_and_undefined_inputs_publish_errors(void) {
  const double undefined_values[] = {NAN, INFINITY};

  setup_libcall_runtime();
  VALUE_t result = call_math_unary(
      lc_math_sqrt, (VALUE_t){VALUE_int, {.i = -1}});
  ASSERT_EQ_INT(VALUE_nil, result.type);
  assert_error(ERR_RUNTIME_INVALIDARGS,
               "Invalid arguments to library call. (math.sqrt expects a non-negative number)");
  teardown_libcall_runtime();

  setup_libcall_runtime();
  result = call_math_unary(
      lc_math_sqrt, (VALUE_t){VALUE_float, {.f = -2.25}});
  ASSERT_EQ_INT(VALUE_nil, result.type);
  assert_error(ERR_RUNTIME_INVALIDARGS,
               "Invalid arguments to library call. (math.sqrt expects a non-negative number)");
  teardown_libcall_runtime();

  setup_libcall_runtime();
  result = call_math_unary(
      lc_math_sqrt, (VALUE_t){VALUE_float, {.f = -INFINITY}});
  ASSERT_EQ_INT(VALUE_nil, result.type);
  assert_error(ERR_RUNTIME_INVALIDARGS,
               "Invalid arguments to library call. (math.sqrt expects a non-negative number)");
  teardown_libcall_runtime();

  for (size_t i = 0; i < sizeof(undefined_values) / sizeof(undefined_values[0]); i++) {
    setup_libcall_runtime();
    result = call_math_unary(
        lc_math_sqrt, (VALUE_t){VALUE_float, {.f = undefined_values[i]}});
    ASSERT_EQ_INT(VALUE_nil, result.type);
    assert_error(ERR_RUNTIME_UNDEFINED, errmsg[ERR_RUNTIME_UNDEFINED]);
    teardown_libcall_runtime();
  }
}

void test_math_pow_undefined_inputs_and_results_publish_error(void) {
  const double undefined_values[] = {NAN, INFINITY, -INFINITY};

  for (size_t i = 0; i < sizeof(undefined_values) / sizeof(undefined_values[0]); i++) {
    setup_libcall_runtime();
    VALUE_t result = call_math_binary(
        lc_math_pow, (VALUE_t){VALUE_float, {.f = undefined_values[i]}},
        (VALUE_t){VALUE_int, {.i = 2}});
    ASSERT_EQ_INT(VALUE_nil, result.type);
    assert_error(ERR_RUNTIME_UNDEFINED, errmsg[ERR_RUNTIME_UNDEFINED]);
    teardown_libcall_runtime();

    setup_libcall_runtime();
    result = call_math_binary(
        lc_math_pow, (VALUE_t){VALUE_int, {.i = 2}},
        (VALUE_t){VALUE_float, {.f = undefined_values[i]}});
    ASSERT_EQ_INT(VALUE_nil, result.type);
    assert_error(ERR_RUNTIME_UNDEFINED, errmsg[ERR_RUNTIME_UNDEFINED]);
    teardown_libcall_runtime();
  }

  setup_libcall_runtime();
  VALUE_t result = call_math_binary(
      lc_math_pow, (VALUE_t){VALUE_int, {.i = 10}},
      (VALUE_t){VALUE_int, {.i = 1000}});
  ASSERT_EQ_INT(VALUE_nil, result.type);
  assert_error(ERR_RUNTIME_UNDEFINED, errmsg[ERR_RUNTIME_UNDEFINED]);
  teardown_libcall_runtime();

  setup_libcall_runtime();
  result = call_math_binary(
      lc_math_pow, (VALUE_t){VALUE_int, {.i = -1}},
      (VALUE_t){VALUE_float, {.f = 0.5}});
  ASSERT_EQ_INT(VALUE_nil, result.type);
  assert_error(ERR_RUNTIME_UNDEFINED, errmsg[ERR_RUNTIME_UNDEFINED]);
  teardown_libcall_runtime();
}

void test_math_sqrt_pow_success_preserves_existing_error(void) {
  setup_libcall_runtime();
  set_error_item(itemstore_root(config.itemstore_ctx), ERR_RUNTIME_INVALIDARGS,
                 "prior error", NULL);
  VALUE_t result = call_math_unary(
      lc_math_sqrt, (VALUE_t){VALUE_int, {.i = 16}});
  ASSERT_EQ_INT(VALUE_float, result.type);
  ASSERT_TRUE(result.f == 4.0);
  assert_error(ERR_RUNTIME_INVALIDARGS,
               "Invalid arguments to library call. (prior error)");
  teardown_libcall_runtime();

  setup_libcall_runtime();
  set_error_item(itemstore_root(config.itemstore_ctx), ERR_RUNTIME_INVALIDARGS,
                 "prior error", NULL);
  result = call_math_binary(
      lc_math_pow, (VALUE_t){VALUE_int, {.i = 2}},
      (VALUE_t){VALUE_int, {.i = 3}});
  ASSERT_EQ_INT(VALUE_float, result.type);
  ASSERT_TRUE(result.f == 8.0);
  assert_error(ERR_RUNTIME_INVALIDARGS,
               "Invalid arguments to library call. (prior error)");
  teardown_libcall_runtime();
}

void test_math_log_registry_contract(void) {
  const struct {
    const char *name;
    uint8_t call_index;
    OP_t handler;
  } manifest[] = {
      {"log", 8, lc_math_log},
      {"log2", 9, lc_math_log2},
      {"log10", 10, lc_math_log10},
  };
  size_t count = 0;

  for (size_t i = 0; libcalls[i].libname != NULL; i++) count++;
  ASSERT_EQ_INT(86, count);
  for (size_t i = 0; i < sizeof(manifest) / sizeof(manifest[0]); i++) {
    uint8_t lib_index = 0;
    uint8_t call_index = 0;
    uint8_t args = 0;
    ASSERT_TRUE(libcall_lookup_pair("math", manifest[i].name, &lib_index,
                                   &call_index, &args));
    ASSERT_EQ_INT(6, lib_index);
    ASSERT_EQ_INT(manifest[i].call_index, call_index);
    ASSERT_EQ_INT(1, args);
    ASSERT_TRUE(libcall_func_pair(lib_index, call_index) == manifest[i].handler);
    ASSERT_TRUE(libcall_pair_arg_count(lib_index, call_index, &args));
    ASSERT_EQ_INT(1, args);
  }
}

void test_math_log_integer_and_float_inputs_return_floats(void) {
  const struct {
    OP_t handler;
    double (*operation)(double);
    int64_t integer_input;
    double float_input;
  } operations[] = {
      {lc_math_log, log, 1, 2.5},
      {lc_math_log2, log2, 8, 2.5},
      {lc_math_log10, log10, 100, 2.5},
  };

  setup_libcall_runtime();
  for (size_t i = 0; i < sizeof(operations) / sizeof(operations[0]); i++) {
    VALUE_t result = call_math_unary(
        operations[i].handler,
        (VALUE_t){VALUE_int, {.i = operations[i].integer_input}});
    ASSERT_EQ_INT(VALUE_float, result.type);
    ASSERT_TRUE(result.f == operations[i].operation(
                              (double)operations[i].integer_input));
    ASSERT_EQ_INT(0, size_stack(config.vm->stack));

    result = call_math_unary(
        operations[i].handler,
        (VALUE_t){VALUE_float, {.f = operations[i].float_input}});
    ASSERT_EQ_INT(VALUE_float, result.type);
    ASSERT_TRUE(result.f == operations[i].operation(operations[i].float_input));
    ASSERT_EQ_INT(0, size_stack(config.vm->stack));
  }
  teardown_libcall_runtime();
}

void test_math_log_rejects_nonnumeric_and_consumes_owned_values(void) {
  const struct {
    OP_t handler;
    const char *detail;
  } operations[] = {
      {lc_math_log,
       "Invalid arguments to library call. (math.log expects an integer or float)"},
      {lc_math_log2,
       "Invalid arguments to library call. (math.log2 expects an integer or float)"},
      {lc_math_log10,
       "Invalid arguments to library call. (math.log10 expects an integer or float)"},
  };

  for (size_t i = 0; i < sizeof(operations) / sizeof(operations[0]); i++) {
    setup_libcall_runtime();
    int before = size_stack(config.vm->stack);
    char *payload = strdup("owned by the argument");
    ASSERT_NOT_NULL(payload);
    VALUE_t result = call_math_unary(
        operations[i].handler, (VALUE_t){VALUE_str, {.s = payload}});
    ASSERT_EQ_INT(VALUE_nil, result.type);
    ASSERT_EQ_INT(before, size_stack(config.vm->stack));
    assert_error(ERR_RUNTIME_INVALIDARGS, operations[i].detail);
    teardown_libcall_runtime();
  }
}

void test_math_log_rejects_nonpositive_inputs(void) {
  const struct {
    VALUE_t value;
    const char *suffix;
  } invalid_values[] = {
      {{VALUE_int, {.i = 0}}, "positive number"},
      {{VALUE_int, {.i = -7}}, "positive number"},
      {{VALUE_float, {.f = 0.0}}, "positive number"},
      {{VALUE_float, {.f = -0.0}}, "positive number"},
      {{VALUE_float, {.f = -INFINITY}}, "positive number"},
  };
  const struct {
    OP_t handler;
    const char *name;
  } operations[] = {
      {lc_math_log, "math.log"},
      {lc_math_log2, "math.log2"},
      {lc_math_log10, "math.log10"},
  };

  for (size_t op = 0; op < sizeof(operations) / sizeof(operations[0]); op++) {
    for (size_t i = 0;
         i < sizeof(invalid_values) / sizeof(invalid_values[0]); i++) {
      setup_libcall_runtime();
      VALUE_t result = call_math_unary(operations[op].handler,
                                       invalid_values[i].value);
      ASSERT_EQ_INT(VALUE_nil, result.type);
      ASSERT_EQ_INT(0, size_stack(config.vm->stack));
      char expected[160];
      (void)snprintf(expected, sizeof(expected),
                     "Invalid arguments to library call. (%s expects a %s)",
                     operations[op].name, invalid_values[i].suffix);
      assert_error(ERR_RUNTIME_INVALIDARGS, expected);
      teardown_libcall_runtime();
    }
  }
}

void test_math_log_undefined_inputs_publish_error(void) {
  const double undefined_values[] = {NAN, INFINITY};
  const OP_t operations[] = {lc_math_log, lc_math_log2, lc_math_log10};

  for (size_t op = 0; op < sizeof(operations) / sizeof(operations[0]); op++) {
    for (size_t i = 0;
         i < sizeof(undefined_values) / sizeof(undefined_values[0]); i++) {
      setup_libcall_runtime();
      VALUE_t result = call_math_unary(
          operations[op], (VALUE_t){VALUE_float, {.f = undefined_values[i]}});
      ASSERT_EQ_INT(VALUE_nil, result.type);
      ASSERT_EQ_INT(0, size_stack(config.vm->stack));
      assert_error(ERR_RUNTIME_UNDEFINED, errmsg[ERR_RUNTIME_UNDEFINED]);
      teardown_libcall_runtime();
    }
  }
}

void test_math_log_success_preserves_existing_error(void) {
  const OP_t operations[] = {lc_math_log, lc_math_log2, lc_math_log10};

  for (size_t i = 0; i < sizeof(operations) / sizeof(operations[0]); i++) {
    setup_libcall_runtime();
    set_error_item(itemstore_root(config.itemstore_ctx), ERR_RUNTIME_INVALIDARGS,
                   "prior error", NULL);
    VALUE_t result = call_math_unary(
        operations[i], (VALUE_t){VALUE_int, {.i = i == 0 ? 1 : 8}});
    ASSERT_EQ_INT(VALUE_float, result.type);
    ASSERT_EQ_INT(0, size_stack(config.vm->stack));
    assert_error(ERR_RUNTIME_INVALIDARGS,
                 "Invalid arguments to library call. (prior error)");
    teardown_libcall_runtime();
  }
}

void test_math_exp_registry_contract(void) {
  uint8_t lib_index = 0;
  uint8_t call_index = 0;
  uint8_t args = 0;
  size_t count = 0;

  for (size_t i = 0; libcalls[i].libname != NULL; i++) count++;
  ASSERT_TRUE(libcall_lookup_pair("math", "exp", &lib_index, &call_index,
                                 &args));
  ASSERT_EQ_INT(6, lib_index);
  ASSERT_EQ_INT(11, call_index);
  ASSERT_EQ_INT(1, args);
  ASSERT_TRUE(libcall_func_pair(lib_index, call_index) == lc_math_exp);
  ASSERT_TRUE(libcall_pair_arg_count(lib_index, call_index, &args));
  ASSERT_EQ_INT(1, args);
  ASSERT_EQ_INT(86, count);
}

void test_math_exp_integer_float_and_signed_zero_inputs(void) {
  setup_libcall_runtime();

  VALUE_t result = call_math_unary(
      lc_math_exp, (VALUE_t){VALUE_int, {.i = 1}});
  ASSERT_EQ_INT(VALUE_float, result.type);
  ASSERT_TRUE(result.f == exp(1.0));
  ASSERT_EQ_INT(0, size_stack(config.vm->stack));

  result = call_math_unary(
      lc_math_exp, (VALUE_t){VALUE_float, {.f = -1.5}});
  ASSERT_EQ_INT(VALUE_float, result.type);
  ASSERT_TRUE(result.f == exp(-1.5));
  ASSERT_EQ_INT(0, size_stack(config.vm->stack));

  result = call_math_unary(
      lc_math_exp, (VALUE_t){VALUE_float, {.f = 0.0}});
  ASSERT_EQ_INT(VALUE_float, result.type);
  ASSERT_TRUE(result.f == 1.0);
  ASSERT_EQ_INT(0, size_stack(config.vm->stack));

  result = call_math_unary(
      lc_math_exp, (VALUE_t){VALUE_float, {.f = -0.0}});
  ASSERT_EQ_INT(VALUE_float, result.type);
  ASSERT_TRUE(result.f == 1.0);
  ASSERT_EQ_INT(0, size_stack(config.vm->stack));

  teardown_libcall_runtime();
}

void test_math_exp_finite_underflow_succeeds(void) {
  setup_libcall_runtime();

  VALUE_t result = call_math_unary(
      lc_math_exp, (VALUE_t){VALUE_float, {.f = -1000.0}});
  ASSERT_EQ_INT(VALUE_float, result.type);
  ASSERT_TRUE(result.f == 0.0);
  ASSERT_EQ_INT(0, size_stack(config.vm->stack));

  teardown_libcall_runtime();
}

void test_math_exp_rejects_nonnumeric_and_consumes_owned_value(void) {
  setup_libcall_runtime();
  int before = size_stack(config.vm->stack);
  char *payload = strdup("owned by the argument");
  ASSERT_NOT_NULL(payload);
  VALUE_t result = call_math_unary(
      lc_math_exp, (VALUE_t){VALUE_str, {.s = payload}});
  ASSERT_EQ_INT(VALUE_nil, result.type);
  ASSERT_EQ_INT(before, size_stack(config.vm->stack));
  assert_error(ERR_RUNTIME_INVALIDARGS,
               "Invalid arguments to library call. (math.exp expects an integer or float)");
  teardown_libcall_runtime();
}

void test_math_exp_undefined_inputs_publish_error(void) {
  const double undefined_values[] = {NAN, INFINITY, -INFINITY, 1000.0};

  for (size_t i = 0; i < sizeof(undefined_values) /
                             sizeof(undefined_values[0]);
       i++) {
    setup_libcall_runtime();
    VALUE_t result = call_math_unary(
        lc_math_exp, (VALUE_t){VALUE_float, {.f = undefined_values[i]}});
    ASSERT_EQ_INT(VALUE_nil, result.type);
    ASSERT_EQ_INT(0, size_stack(config.vm->stack));
    assert_error(ERR_RUNTIME_UNDEFINED, errmsg[ERR_RUNTIME_UNDEFINED]);
    teardown_libcall_runtime();
  }
}

void test_math_exp_success_preserves_existing_error(void) {
  setup_libcall_runtime();
  set_error_item(itemstore_root(config.itemstore_ctx), ERR_RUNTIME_INVALIDARGS,
                 "prior error", NULL);
  VALUE_t result = call_math_unary(
      lc_math_exp, (VALUE_t){VALUE_int, {.i = 1}});
  ASSERT_EQ_INT(VALUE_float, result.type);
  ASSERT_TRUE(result.f == exp(1.0));
  ASSERT_EQ_INT(0, size_stack(config.vm->stack));
  assert_error(ERR_RUNTIME_INVALIDARGS,
               "Invalid arguments to library call. (prior error)");
  teardown_libcall_runtime();
}

static void assert_math_float_close(double actual, double expected) {
  ASSERT_TRUE(isfinite(actual));
  ASSERT_TRUE(fabs(actual - expected) <= 1e-12);
}

void test_math_trig_registry_contract(void) {
  struct manifest {
    const char *name;
    uint8_t call_index;
    uint8_t args;
    OP_t handler;
  };
  static const struct manifest manifest[] = {
      {"sin", 12, 1, lc_math_sin},   {"cos", 13, 1, lc_math_cos},
      {"tan", 14, 1, lc_math_tan},   {"asin", 15, 1, lc_math_asin},
      {"acos", 16, 1, lc_math_acos}, {"atan", 17, 1, lc_math_atan},
      {"atan2", 18, 2, lc_math_atan2},
  };
  size_t count = 0;

  for (size_t i = 0; libcalls[i].libname != NULL; i++) count++;
  ASSERT_EQ_INT(86, count);
  for (size_t i = 0; i < sizeof(manifest) / sizeof(manifest[0]); i++) {
    uint8_t lib_index = 0;
    uint8_t call_index = 0;
    uint8_t args = 0;
    ASSERT_TRUE(libcall_lookup_pair("math", manifest[i].name, &lib_index,
                                    &call_index, &args));
    ASSERT_EQ_INT(6, lib_index);
    ASSERT_EQ_INT(manifest[i].call_index, call_index);
    ASSERT_EQ_INT(manifest[i].args, args);
    ASSERT_TRUE(libcall_func_pair(lib_index, call_index) == manifest[i].handler);
    ASSERT_TRUE(libcall_pair_arg_count(lib_index, call_index, &args));
    ASSERT_EQ_INT(manifest[i].args, args);
  }
}

void test_math_trig_unary_values_return_floats(void) {
  const struct {
    OP_t handler;
    VALUE_t input;
    double expected;
  } cases[] = {
      {lc_math_sin, {VALUE_int, {.i = 0}}, 0.0},
      {lc_math_sin, {VALUE_float, {.f = M_PI / 2.0}}, 1.0},
      {lc_math_cos, {VALUE_int, {.i = 0}}, 1.0},
      {lc_math_cos, {VALUE_float, {.f = M_PI}}, -1.0},
      {lc_math_tan, {VALUE_float, {.f = M_PI / 4.0}}, 1.0},
      {lc_math_atan, {VALUE_int, {.i = 1}}, M_PI / 4.0},
  };

  setup_libcall_runtime();
  for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
    VALUE_t result = call_math_unary(cases[i].handler, cases[i].input);
    ASSERT_EQ_INT(VALUE_float, result.type);
    assert_math_float_close(result.f, cases[i].expected);
  }
  VALUE_t result = call_math_unary(
      lc_math_sin, (VALUE_t){VALUE_float, {.f = -0.0}});
  ASSERT_EQ_INT(VALUE_float, result.type);
  ASSERT_TRUE(result.f == 0.0 && signbit(result.f));
  result = call_math_unary(lc_math_tan,
                           (VALUE_t){VALUE_float, {.f = -0.0}});
  ASSERT_EQ_INT(VALUE_float, result.type);
  ASSERT_TRUE(result.f == 0.0 && signbit(result.f));
  ASSERT_EQ_INT(0, size_stack(config.vm->stack));
  teardown_libcall_runtime();
}

void test_math_trig_atan2_ordering_and_quadrants(void) {
  setup_libcall_runtime();

  VALUE_t result = call_math_binary(
      lc_math_atan2, (VALUE_t){VALUE_int, {.i = 1}},
      (VALUE_t){VALUE_int, {.i = 1}});
  ASSERT_EQ_INT(VALUE_float, result.type);
  assert_math_float_close(result.f, M_PI / 4.0);
  result = call_math_binary(lc_math_atan2,
                            (VALUE_t){VALUE_int, {.i = 1}},
                            (VALUE_t){VALUE_int, {.i = -1}});
  assert_math_float_close(result.f, 3.0 * M_PI / 4.0);
  result = call_math_binary(lc_math_atan2,
                            (VALUE_t){VALUE_int, {.i = -1}},
                            (VALUE_t){VALUE_int, {.i = -1}});
  assert_math_float_close(result.f, -3.0 * M_PI / 4.0);
  result = call_math_binary(lc_math_atan2,
                            (VALUE_t){VALUE_int, {.i = -1}},
                            (VALUE_t){VALUE_int, {.i = 1}});
  assert_math_float_close(result.f, -M_PI / 4.0);
  ASSERT_EQ_INT(0, size_stack(config.vm->stack));
  teardown_libcall_runtime();
}

void test_math_trig_inverse_endpoints_and_signed_zero(void) {
  setup_libcall_runtime();

  VALUE_t result = call_math_unary(
      lc_math_asin, (VALUE_t){VALUE_int, {.i = 1}});
  ASSERT_EQ_INT(VALUE_float, result.type);
  assert_math_float_close(result.f, M_PI / 2.0);
  result = call_math_unary(
      lc_math_asin, (VALUE_t){VALUE_int, {.i = -1}});
  assert_math_float_close(result.f, -M_PI / 2.0);
  result = call_math_unary(
      lc_math_acos, (VALUE_t){VALUE_int, {.i = 1}});
  assert_math_float_close(result.f, 0.0);
  result = call_math_unary(
      lc_math_acos, (VALUE_t){VALUE_int, {.i = -1}});
  assert_math_float_close(result.f, M_PI);
  result = call_math_unary(
      lc_math_asin, (VALUE_t){VALUE_float, {.f = -0.0}});
  ASSERT_TRUE(result.f == 0.0 && signbit(result.f));
  ASSERT_EQ_INT(0, size_stack(config.vm->stack));
  teardown_libcall_runtime();
}

void test_math_trig_rejects_nonnumeric_and_consumes_owned_values(void) {
  const OP_t unary[] = {lc_math_sin, lc_math_cos, lc_math_tan, lc_math_asin,
                        lc_math_acos, lc_math_atan};

  setup_libcall_runtime();
  for (size_t i = 0; i < sizeof(unary) / sizeof(unary[0]); i++) {
    char *payload = strdup("owned trig argument");
    ASSERT_NOT_NULL(payload);
    VALUE_t result = call_math_unary(unary[i],
                                     (VALUE_t){VALUE_str, {.s = payload}});
    ASSERT_EQ_INT(VALUE_nil, result.type);
    ASSERT_EQ_INT(0, size_stack(config.vm->stack));
  }
  char *left_payload = strdup("owned y argument");
  char *right_payload = strdup("owned x argument");
  ASSERT_NOT_NULL(left_payload);
  ASSERT_NOT_NULL(right_payload);
  VALUE_t result = call_math_binary(
      lc_math_atan2, (VALUE_t){VALUE_str, {.s = left_payload}},
      (VALUE_t){VALUE_str, {.s = right_payload}});
  ASSERT_EQ_INT(VALUE_nil, result.type);
  ASSERT_EQ_INT(0, size_stack(config.vm->stack));
  assert_error(ERR_RUNTIME_INVALIDARGS,
               "Invalid arguments to library call. (math.atan2 expects two integer or float arguments)");
  teardown_libcall_runtime();
}

void test_math_trig_inverse_domain_errors(void) {
  setup_libcall_runtime();

  VALUE_t result = call_math_unary(
      lc_math_asin, (VALUE_t){VALUE_float, {.f = nextafter(1.0, 2.0)}});
  ASSERT_EQ_INT(VALUE_nil, result.type);
  assert_error(ERR_RUNTIME_INVALIDARGS,
               "Invalid arguments to library call. (math.asin expects a number in the inclusive range [-1, 1])");
  result = call_math_unary(lc_math_acos,
                           (VALUE_t){VALUE_float, {.f = -1.1}});
  ASSERT_EQ_INT(VALUE_nil, result.type);
  assert_error(ERR_RUNTIME_INVALIDARGS,
               "Invalid arguments to library call. (math.acos expects a number in the inclusive range [-1, 1])");
  ASSERT_EQ_INT(0, size_stack(config.vm->stack));
  teardown_libcall_runtime();
}

void test_math_trig_undefined_inputs_and_tan_nonfinite_result(void) {
  const double inputs[] = {NAN, INFINITY, -INFINITY};
  const OP_t unary[] = {lc_math_sin, lc_math_cos, lc_math_tan, lc_math_asin,
                        lc_math_acos, lc_math_atan};

  for (size_t op = 0; op < sizeof(unary) / sizeof(unary[0]); op++) {
    for (size_t i = 0; i < sizeof(inputs) / sizeof(inputs[0]); i++) {
      setup_libcall_runtime();
      VALUE_t result = call_math_unary(
          unary[op], (VALUE_t){VALUE_float, {.f = inputs[i]}});
      ASSERT_EQ_INT(VALUE_nil, result.type);
      assert_error(ERR_RUNTIME_UNDEFINED, errmsg[ERR_RUNTIME_UNDEFINED]);
      teardown_libcall_runtime();
    }
  }

  setup_libcall_runtime();
  VALUE_t result = call_math_binary(
      lc_math_atan2, (VALUE_t){VALUE_float, {.f = NAN}},
      (VALUE_t){VALUE_int, {.i = 1}});
  ASSERT_EQ_INT(VALUE_nil, result.type);
  assert_error(ERR_RUNTIME_UNDEFINED, errmsg[ERR_RUNTIME_UNDEFINED]);
  teardown_libcall_runtime();

  setup_libcall_runtime();
  double input = M_PI / 2.0;
  double expected = tan(input);
  result = call_math_unary(
      lc_math_tan, (VALUE_t){VALUE_float, {.f = input}});
  if (isfinite(expected)) {
    ASSERT_EQ_INT(VALUE_float, result.type);
    assert_math_float_close(result.f, expected);
  } else {
    ASSERT_EQ_INT(VALUE_nil, result.type);
    assert_error(ERR_RUNTIME_UNDEFINED, errmsg[ERR_RUNTIME_UNDEFINED]);
  }
  teardown_libcall_runtime();
}

void test_math_trig_success_preserves_existing_error(void) {
  setup_libcall_runtime();
  set_error_item(itemstore_root(config.itemstore_ctx), ERR_RUNTIME_INVALIDARGS,
                 "prior trig error", NULL);
  VALUE_t result = call_math_unary(
      lc_math_sin, (VALUE_t){VALUE_int, {.i = 0}});
  ASSERT_EQ_INT(VALUE_float, result.type);
  assert_error(ERR_RUNTIME_INVALIDARGS,
               "Invalid arguments to library call. (prior trig error)");
  result = call_math_binary(lc_math_atan2,
                            (VALUE_t){VALUE_int, {.i = 1}},
                            (VALUE_t){VALUE_int, {.i = 1}});
  ASSERT_EQ_INT(VALUE_float, result.type);
  assert_error(ERR_RUNTIME_INVALIDARGS,
               "Invalid arguments to library call. (prior trig error)");
  teardown_libcall_runtime();
}

void test_math_float_result_stack_and_diagnostic_contract(void) {
  const struct {
    OP_t handler;
    VALUE_t args[2];
    size_t arg_count;
    bool undefined;
    double expected;
  } cases[] = {
      {lc_math_sqrt, {{VALUE_int, {.i = 4}}}, 1, false, 2.0},
      {lc_math_pow, {{VALUE_int, {.i = 2}}, {VALUE_int, {.i = 3}}},
       2, false, 8.0},
      {lc_math_log, {{VALUE_int, {.i = 1}}}, 1, false, 0.0},
      {lc_math_exp, {{VALUE_int, {.i = 0}}}, 1, false, 1.0},
      {lc_math_sin, {{VALUE_float, {.f = 0.0}}}, 1, false, 0.0},
      {lc_math_atan2, {{VALUE_int, {.i = 0}}, {VALUE_int, {.i = 1}}},
       2, false, 0.0},
      {lc_math_pow, {{VALUE_int, {.i = -1}}, {VALUE_float, {.f = 0.5}}},
       2, true, 0.0},
      {lc_math_pow, {{VALUE_float, {.f = 1e308}}, {VALUE_int, {.i = 2}}},
       2, true, 0.0},
      {lc_math_exp, {{VALUE_int, {.i = 1000}}}, 1, true, 0.0},
  };
  setup_libcall_runtime();
  for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
    set_error_item(itemstore_root(config.itemstore_ctx), ERR_RUNTIME_INVALIDARGS,
                   "prior result error", NULL);
    char *sentinel = strdup("preserved lower stack value");
    ASSERT_NOT_NULL(sentinel);
    push_stack(config.vm->stack, (VALUE_t){VALUE_str, {.s = sentinel}});
    for (size_t j = 0; j < cases[i].arg_count; ++j) {
      push_stack(config.vm->stack, cases[i].args[j]);
    }
    uint8_t continuation[2] = {0};
    ASSERT_TRUE(cases[i].handler(test_ctx(), &continuation[1],
                                itemstore_root(config.itemstore_ctx)) ==
                &continuation[1]);
    ASSERT_EQ_INT(2, size_stack(config.vm->stack));
    VALUE_t result = pop_stack(config.vm->stack);
    if (cases[i].undefined) {
      ASSERT_EQ_INT(VALUE_nil, result.type);
      assert_error(ERR_RUNTIME_UNDEFINED, errmsg[ERR_RUNTIME_UNDEFINED]);
    } else {
      ASSERT_EQ_INT(VALUE_float, result.type);
      ASSERT_TRUE(result.f == cases[i].expected);
      assert_error(ERR_RUNTIME_INVALIDARGS,
                   "Invalid arguments to library call. (prior result error)");
    }
    value_free(&result);
    VALUE_t preserved = pop_stack(config.vm->stack);
    ASSERT_EQ_INT(VALUE_str, preserved.type);
    ASSERT_TRUE(preserved.s == sentinel);
    ASSERT_TRUE(strcmp(preserved.s, "preserved lower stack value") == 0);
    value_free(&preserved);
    ASSERT_EQ_INT(0, size_stack(config.vm->stack));
  }
  teardown_libcall_runtime();
}
