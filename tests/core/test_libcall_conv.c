#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "compiler/compiler_pipeline.h"
#include "config.h"
#include "error.h"
#include "itemref.h"
#include "libcall.h"
#include "libcall_handlers.h"
#include "list.h"
#include "stack.h"
#include "test_assert.h"

#include "shared/test_libcall_support.h"

extern CONFIG_t config;

static VALUE_t call_bool(VALUE_t value) {
  push_stack(config.vm->stack, value);
  (void)lc_conv_bool(test_ctx(), NULL, NULL);
  return pop_stack(config.vm->stack);
}

static VALUE_t call_int(VALUE_t value) {
  push_stack(config.vm->stack, value);
  (void)lc_conv_int(test_ctx(), NULL, NULL);
  return pop_stack(config.vm->stack);
}

static void assert_bool(VALUE_t value, int expected) {
  ASSERT_EQ_INT(VALUE_bool, value.type);
  ASSERT_EQ_INT(expected, value.i);
}

static void assert_int(VALUE_t value, int64_t expected) {
  ASSERT_EQ_INT(VALUE_int, value.type);
  ASSERT_EQ_INT(expected, value.i);
}

void test_conv_bool_registry_contract(void) {
  uint8_t lib_index = 0;
  uint8_t call_index = 0;
  uint8_t args = 0;
  size_t count = 0;

  while (libcalls[count].libname != NULL) count++;
  ASSERT_EQ_INT(98, count);
  ASSERT_TRUE(libcall_lookup_pair("conv", "bool", &lib_index, &call_index,
                                 &args));
  ASSERT_EQ_INT(9, lib_index);
  ASSERT_EQ_INT(0, call_index);
  ASSERT_EQ_INT(1, args);
  ASSERT_TRUE(libcall_func_pair(lib_index, call_index) == lc_conv_bool);
  ASSERT_TRUE(libcall_pair_arg_count(lib_index, call_index, &args));
  ASSERT_EQ_INT(1, args);

  ASSERT_TRUE(libcall_lookup_pair("conv", "int", &lib_index, &call_index,
                                 &args));
  ASSERT_EQ_INT(9, lib_index);
  ASSERT_EQ_INT(1, call_index);
  ASSERT_EQ_INT(1, args);
  ASSERT_TRUE(libcall_func_pair(lib_index, call_index) == lc_conv_int);
  ASSERT_TRUE(libcall_pair_arg_count(lib_index, call_index, &args));
  ASSERT_EQ_INT(1, args);
}

void test_conv_int_registry_contract(void) {
  uint8_t lib_index = 0;
  uint8_t call_index = 0;
  uint8_t args = 0;

  ASSERT_TRUE(libcall_lookup_pair("conv", "int", &lib_index, &call_index,
                                 &args));
  ASSERT_EQ_INT(9, lib_index);
  ASSERT_EQ_INT(1, call_index);
  ASSERT_EQ_INT(1, args);
  ASSERT_TRUE(libcall_func_pair(lib_index, call_index) == lc_conv_int);
  ASSERT_TRUE(libcall_pair_arg_count(lib_index, call_index, &args));
  ASSERT_EQ_INT(1, args);
}

void test_conv_int_converts_values(void) {
  setup_libcall_runtime();

  assert_int(call_int((VALUE_t){VALUE_int, {.i = INT64_MIN}}), INT64_MIN);
  assert_int(call_int((VALUE_t){VALUE_int, {.i = INT64_MAX}}), INT64_MAX);
  assert_int(call_int((VALUE_t){VALUE_float, {.f = 12.75}}), 12);
  assert_int(call_int((VALUE_t){VALUE_float, {.f = -12.75}}), -12);
  assert_int(call_int((VALUE_t){VALUE_float, {.f = -0.5}}), 0);
  assert_int(call_int((VALUE_t){VALUE_float, {.f = -0x1p63}}), INT64_MIN);
  assert_int(call_int((VALUE_t){VALUE_float, {.f = 0x1p63 - 1024.0}}),
             INT64_MAX - 1023);
  assert_int(call_int(VALUE_FALSE), 0);
  assert_int(call_int(VALUE_TRUE), 1);
  assert_int(call_int(VALUE_NIL), 0);

  const char *valid[] = {"123", "-123", "+123", "9223372036854775807",
                         "-9223372036854775808"};
  const int64_t expected[] = {123, -123, 123, INT64_MAX, INT64_MIN};
  for (size_t i = 0; i < sizeof(valid) / sizeof(valid[0]); i++) {
    VALUE_t value = {VALUE_str, {.s = strdup(valid[i])}};
    ASSERT_NOT_NULL(value.s);
    assert_int(call_int(value), expected[i]);
  }
  ASSERT_EQ_INT(0, size_stack(config.vm->stack));
  teardown_libcall_runtime();
}

void test_conv_int_rejects_invalid_values_and_boundaries(void) {
  setup_libcall_runtime();

  const double invalid_floats[] = {NAN, INFINITY, -INFINITY, 0x1p63,
                                   -0x1p63 - 2048.0};
  for (size_t i = 0; i < sizeof(invalid_floats) / sizeof(invalid_floats[0]);
       i++) {
    VALUE_t result = call_int((VALUE_t){VALUE_float, {.f = invalid_floats[i]}});
    ASSERT_EQ_INT(VALUE_nil, result.type);
    ITEM_t *error = find_item(itemstore_root(config.itemstore_ctx), "error");
    ASSERT_NOT_NULL(error);
    ASSERT_EQ_INT(ERR_RUNTIME_INVALIDARGS, item_value(error)->i);
  }

  const char *invalid_strings[] = {"", "123h", " 123", "123 ",
                                   "9223372036854775808",
                                   "-9223372036854775809"};
  for (size_t i = 0; i < sizeof(invalid_strings) / sizeof(invalid_strings[0]);
       i++) {
    VALUE_t value = {VALUE_str, {.s = strdup(invalid_strings[i])}};
    ASSERT_NOT_NULL(value.s);
    VALUE_t result = call_int(value);
    ASSERT_EQ_INT(VALUE_nil, result.type);
    ITEM_t *error = find_item(itemstore_root(config.itemstore_ctx), "error");
    ASSERT_NOT_NULL(error);
    ASSERT_EQ_INT(ERR_RUNTIME_INVALIDARGS, item_value(error)->i);
  }

  SIN_LIST_t *list = sin_list_build_owned(NULL, 0);
  SIN_ITEMREF_t *itemref = sin_itemref_create("root.child");
  ASSERT_NOT_NULL(list);
  ASSERT_NOT_NULL(itemref);
  VALUE_t result = call_int((VALUE_t){VALUE_list, {.list = list}});
  ASSERT_EQ_INT(VALUE_nil, result.type);
  result = call_int((VALUE_t){VALUE_itemref, {.itemref = itemref}});
  ASSERT_EQ_INT(VALUE_nil, result.type);
  ASSERT_EQ_INT(0, size_stack(config.vm->stack));
  teardown_libcall_runtime();
}

void test_conv_int_consumes_values_and_preserves_diagnostics(void) {
  setup_libcall_runtime();
  set_error_item(itemstore_root(config.itemstore_ctx), ERR_NETWORK_ERROR,
                 "prior diagnostic", NULL);
  VALUE_t owned = {VALUE_str, {.s = strdup("17")}};
  ASSERT_NOT_NULL(owned.s);
  assert_int(call_int(owned), 17);
  ITEM_t *error = find_item(itemstore_root(config.itemstore_ctx), "error");
  ASSERT_NOT_NULL(error);
  ASSERT_EQ_INT(ERR_NETWORK_ERROR, item_value(error)->i);
  ASSERT_EQ_INT(0, size_stack(config.vm->stack));
  teardown_libcall_runtime();
}

void test_conv_int_source_integration_and_arity(void) {
  setup_libcall_runtime();
  VALUE_t source = {VALUE_str, {.s = strdup(
      "result.a = conv.int{3.9}; result.b = conv.int{true}; "
      "result.c = conv.int{\"-8\"}; result.d = conv.int{nil};")}};
  ASSERT_NOT_NULL(source.s);
  push_stack(config.vm->stack, source);
  (void)lc_sys_compile(test_ctx(), NULL, NULL);
  VALUE_t compiled = pop_stack(config.vm->stack);
  assert_bool(compiled, 1);
  ITEM_t *a = find_item(itemstore_root(config.itemstore_ctx), "result.a");
  ITEM_t *b = find_item(itemstore_root(config.itemstore_ctx), "result.b");
  ITEM_t *c = find_item(itemstore_root(config.itemstore_ctx), "result.c");
  ITEM_t *d = find_item(itemstore_root(config.itemstore_ctx), "result.d");
  ASSERT_NOT_NULL(a);
  ASSERT_NOT_NULL(b);
  ASSERT_NOT_NULL(c);
  ASSERT_NOT_NULL(d);
  assert_int(*item_value(a), 3);
  assert_int(*item_value(b), 1);
  assert_int(*item_value(c), -8);
  assert_int(*item_value(d), 0);

  const char *invalid[] = {"conv.int;", "conv.int{1, 2};"};
  for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i++) {
    OUTPUT_t *out = NULL;
    CompilerDiagnostic diag;
    compiler_diag_init(&diag);
    ASSERT_TRUE(compile_source_to_bytecode_diag(invalid[i], strlen(invalid[i]),
                                               &out, &diag) != 0);
    ASSERT_TRUE(out == NULL);
    ASSERT_EQ_INT(DIAG_PHASE_LOWER, diag.phase);
    ASSERT_NOT_NULL(diag.message);
    ASSERT_TRUE(strstr(diag.message, "invalid libcall argument count") != NULL);
    compiler_diag_reset(&diag);
  }
  teardown_libcall_runtime();
}

void test_conv_bool_uses_runtime_truthiness(void) {
  VALUE_t nonempty_values[] = {{VALUE_int, {.i = 1}}};
  setup_libcall_runtime();

  assert_bool(call_bool(VALUE_NIL), 0);
  assert_bool(call_bool(VALUE_FALSE), 0);
  assert_bool(call_bool(VALUE_TRUE), 1);
  assert_bool(call_bool((VALUE_t){VALUE_int, {.i = 0}}), 0);
  assert_bool(call_bool((VALUE_t){VALUE_int, {.i = -7}}), 1);
  assert_bool(call_bool((VALUE_t){VALUE_float, {.f = 0.0}}), 0);
  assert_bool(call_bool((VALUE_t){VALUE_float, {.f = -0.0}}), 0);
  assert_bool(call_bool((VALUE_t){VALUE_float, {.f = NAN}}), 1);
  assert_bool(call_bool((VALUE_t){VALUE_float, {.f = 2.5}}), 1);

  VALUE_t empty_string = {VALUE_str, {.s = strdup("")}};
  VALUE_t nonempty_string = {VALUE_str, {.s = strdup("value")}};
  ASSERT_NOT_NULL(empty_string.s);
  ASSERT_NOT_NULL(nonempty_string.s);
  assert_bool(call_bool(empty_string), 0);
  assert_bool(call_bool(nonempty_string), 1);

  SIN_LIST_t *empty = sin_list_build_owned(NULL, 0);
  SIN_LIST_t *nonempty = sin_list_build_owned(nonempty_values, 1);
  ASSERT_NOT_NULL(empty);
  ASSERT_NOT_NULL(nonempty);
  assert_bool(call_bool((VALUE_t){VALUE_list, {.list = empty}}), 0);
  assert_bool(call_bool((VALUE_t){VALUE_list, {.list = nonempty}}), 1);

  SIN_ITEMREF_t *itemref = sin_itemref_create("root.child");
  ASSERT_NOT_NULL(itemref);
  assert_bool(call_bool((VALUE_t){VALUE_itemref, {.itemref = itemref}}), 1);

  /* The helper's default path keeps malformed/unknown values non-truthy and
   * does not turn this any-value conversion into an invalid-argument error. */
  assert_bool(call_bool((VALUE_t){VALUE_list, {.list = NULL}}), 0);
  assert_bool(call_bool((VALUE_t){(VALUE_e)99, {.i = 1}}), 0);
  ASSERT_EQ_INT(0, size_stack(config.vm->stack));
  teardown_libcall_runtime();
}

void test_conv_bool_consumes_values_and_preserves_diagnostics(void) {
  setup_libcall_runtime();
  set_error_item(itemstore_root(config.itemstore_ctx), ERR_NETWORK_ERROR,
                 "prior diagnostic", NULL);
  VALUE_t owned = {VALUE_str, {.s = strdup("owned")}};
  ASSERT_NOT_NULL(owned.s);
  assert_bool(call_bool(owned), 1);
  ITEM_t *error = find_item(itemstore_root(config.itemstore_ctx), "error");
  ASSERT_NOT_NULL(error);
  ASSERT_EQ_INT(ERR_NETWORK_ERROR, item_value(error)->i);
  ASSERT_EQ_INT(0, size_stack(config.vm->stack));
  teardown_libcall_runtime();
}

void test_conv_bool_source_integration_and_arity(void) {
  setup_libcall_runtime();
  VALUE_t source = {VALUE_str, {.s = strdup(
      "result.a = conv.bool{nil}; result.b = conv.bool{0}; "
      "result.c = conv.bool{\"value\"}; result.d = conv.bool{#[1]};")}};
  ASSERT_NOT_NULL(source.s);
  push_stack(config.vm->stack, source);
  (void)lc_sys_compile(test_ctx(), NULL, NULL);
  VALUE_t compiled = pop_stack(config.vm->stack);
  assert_bool(compiled, 1);
  ITEM_t *a = find_item(itemstore_root(config.itemstore_ctx), "result.a");
  ITEM_t *b = find_item(itemstore_root(config.itemstore_ctx), "result.b");
  ITEM_t *c = find_item(itemstore_root(config.itemstore_ctx), "result.c");
  ITEM_t *d = find_item(itemstore_root(config.itemstore_ctx), "result.d");
  ASSERT_NOT_NULL(a);
  ASSERT_NOT_NULL(b);
  ASSERT_NOT_NULL(c);
  ASSERT_NOT_NULL(d);
  assert_bool(*item_value(a), 0);
  assert_bool(*item_value(b), 0);
  assert_bool(*item_value(c), 1);
  assert_bool(*item_value(d), 1);

  const char *invalid[] = {"conv.bool;", "conv.bool{1, 2};"};
  for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i++) {
    OUTPUT_t *out = NULL;
    CompilerDiagnostic diag;
    compiler_diag_init(&diag);
    ASSERT_TRUE(compile_source_to_bytecode_diag(invalid[i], strlen(invalid[i]),
                                               &out, &diag) != 0);
    ASSERT_TRUE(out == NULL);
    ASSERT_EQ_INT(DIAG_PHASE_LOWER, diag.phase);
    ASSERT_NOT_NULL(diag.message);
    ASSERT_TRUE(strstr(diag.message, "invalid libcall argument count") != NULL);
    compiler_diag_reset(&diag);
  }
  teardown_libcall_runtime();
}
