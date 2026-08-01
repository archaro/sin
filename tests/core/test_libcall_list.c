#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

#include "config.h"
#include "error.h"
#include "itemref.h"
#include "libcall.h"
#include "libcall_handlers.h"
#include "libcall_list.h"
#include "list.h"
#include "memory.h"
#include "stack.h"
#include "test_assert.h"
#include "test_helpers.h"
#include "shared/test_libcall_support.h"

extern CONFIG_t config;

static SIN_LIST_t *list_of_ints(int a, int b) {
  VALUE_t values[2] = {{VALUE_int, {.i = a}}, {VALUE_int, {.i = b}}};
  return sin_list_build_owned(values, 2);
}

static VALUE_t list_get_result(SIN_LIST_t *list, int64_t index) {
  push_stack(config.vm->stack,
             (VALUE_t){VALUE_list, {.list = sin_list_retain(list)}});
  push_stack(config.vm->stack, (VALUE_t){VALUE_int, {.i = index}});
  (void)lc_list_get(test_ctx(), NULL, NULL);
  return pop_stack(config.vm->stack);
}

static void assert_list_ints(const VALUE_t *value, const int *expected,
                             size_t count) {
  ASSERT_EQ_INT(VALUE_list, value->type);
  ASSERT_EQ_INT(count, sin_list_count(value->list));
  for (size_t i = 0; i < count; ++i) {
    const VALUE_t *element = sin_list_get(value->list, i);
    ASSERT_NOT_NULL(element);
    ASSERT_EQ_INT(VALUE_int, element->type);
    ASSERT_EQ_INT(expected[i], element->i);
  }
}

static void assert_invalid_list_call(OP_t handler, VALUE_t *args,
                                     size_t arg_count,
                                     const char *expected_detail) {
  for (size_t i = 0; i < arg_count; ++i) {
    push_stack(config.vm->stack, args[i]);
    args[i] = VALUE_NIL;
  }
  (void)handler(test_ctx(), NULL, itemstore_root(config.itemstore_ctx));
  VALUE_t result = pop_stack(config.vm->stack);
  ASSERT_EQ_INT(VALUE_nil, result.type);
  assert_invalid_args_detail_contains(expected_detail);
}

void test_list_libcall_registry_contract(void) {
  uint8_t lib_index = 0, call_index = 0;
  uint8_t args = 0;
  const char *names[] = {"length", "get", "append", "set", "concat", "slice"};
  const uint8_t arities[] = {1, 2, 2, 3, 2, 3};
  OP_t handlers[] = {lc_list_length, lc_list_get, lc_list_append,
                     lc_list_set, lc_list_concat, lc_list_slice};
  for (size_t i = 0; i < 6; ++i) {
    ASSERT_TRUE(libcall_lookup_pair("list", names[i], &lib_index, &call_index, &args));
    ASSERT_EQ_INT(5, lib_index);
    ASSERT_EQ_INT(i, call_index);
    ASSERT_EQ_INT(arities[i], args);
    ASSERT_TRUE(libcall_func_pair(lib_index, call_index) == handlers[i]);
  }
}

void test_list_libcall_valid_operations_and_ownership(void) {
  const int left_values[] = {1, 2};
  const int appended_values[] = {1, 2, 9};
  const int concatenated_values[] = {1, 2, 3, 4};
  setup_libcall_runtime();
  SIN_LIST_t *left = list_of_ints(1, 2);
  SIN_LIST_t *right = list_of_ints(3, 4);
  ASSERT_NOT_NULL(left);
  ASSERT_NOT_NULL(right);
  push_stack(config.vm->stack,
             (VALUE_t){VALUE_list, {.list = sin_list_retain(left)}});
  (void)lc_list_length(test_ctx(), NULL, NULL);
  VALUE_t length = pop_stack(config.vm->stack);
  ASSERT_EQ_INT(VALUE_int, length.type);
  ASSERT_EQ_INT(2, length.i);
  push_stack(config.vm->stack,
             (VALUE_t){VALUE_list,
                       {.list = sin_list_build_owned(NULL, 0)}});
  (void)lc_list_length(test_ctx(), NULL, NULL);
  length = pop_stack(config.vm->stack);
  ASSERT_EQ_INT(VALUE_int, length.type);
  ASSERT_EQ_INT(0, length.i);

  VALUE_t got = list_get_result(left, 1);
  ASSERT_EQ_INT(VALUE_int, got.type);
  ASSERT_EQ_INT(2, got.i);
  push_stack(config.vm->stack,
             (VALUE_t){VALUE_list, {.list = sin_list_retain(left)}});
  push_stack(config.vm->stack, (VALUE_t){VALUE_int, {.i = 9}});
  (void)lc_list_append(test_ctx(), NULL, NULL);
  VALUE_t appended = pop_stack(config.vm->stack);
  assert_list_ints(&appended, appended_values, 3);
  ASSERT_EQ_INT(2, sin_list_count(left));
  value_free(&appended);
  push_stack(config.vm->stack,
             (VALUE_t){VALUE_list, {.list = sin_list_retain(left)}});
  push_stack(config.vm->stack, (VALUE_t){VALUE_int, {.i = 0}});
  push_stack(config.vm->stack, (VALUE_t){VALUE_int, {.i = 7}});
  (void)lc_list_set(test_ctx(), NULL, NULL);
  VALUE_t set = pop_stack(config.vm->stack);
  ASSERT_EQ_INT(VALUE_list, set.type);
  ASSERT_EQ_INT(7, sin_list_get(set.list, 0)->i);
  ASSERT_EQ_INT(1, sin_list_get(left, 0)->i);
  value_free(&set);
  push_stack(config.vm->stack,
             (VALUE_t){VALUE_list, {.list = sin_list_retain(left)}});
  push_stack(config.vm->stack,
             (VALUE_t){VALUE_list, {.list = sin_list_retain(right)}});
  (void)lc_list_concat(test_ctx(), NULL, NULL);
  VALUE_t concat = pop_stack(config.vm->stack);
  assert_list_ints(&concat, concatenated_values, 4);
  ASSERT_EQ_INT(2, sin_list_count(left));
  ASSERT_EQ_INT(2, sin_list_count(right));
  value_free(&concat);
  push_stack(config.vm->stack,
             (VALUE_t){VALUE_list, {.list = sin_list_retain(left)}});
  push_stack(config.vm->stack, (VALUE_t){VALUE_int, {.i = 1}});
  push_stack(config.vm->stack, (VALUE_t){VALUE_int, {.i = 1}});
  (void)lc_list_slice(test_ctx(), NULL, NULL);
  VALUE_t slice = pop_stack(config.vm->stack);
  ASSERT_EQ_INT(VALUE_list, slice.type);
  ASSERT_EQ_INT(2, sin_list_get(slice.list, 0)->i);
  value_free(&slice);

  SIN_LIST_t *empty = sin_list_build_owned(NULL, 0);
  ASSERT_NOT_NULL(empty);
  VALUE_t missing = list_get_result(empty, 0);
  ASSERT_EQ_INT(VALUE_nil, missing.type);
  push_stack(config.vm->stack,
             (VALUE_t){VALUE_list, {.list = sin_list_retain(empty)}});
  push_stack(config.vm->stack, (VALUE_t){VALUE_int, {.i = 5}});
  (void)lc_list_append(test_ctx(), NULL, NULL);
  VALUE_t appended_to_empty = pop_stack(config.vm->stack);
  const int five[] = {5};
  assert_list_ints(&appended_to_empty, five, 1);
  ASSERT_EQ_INT(0, sin_list_count(empty));
  value_free(&appended_to_empty);
  push_stack(config.vm->stack,
             (VALUE_t){VALUE_list, {.list = sin_list_retain(empty)}});
  push_stack(config.vm->stack, (VALUE_t){VALUE_int, {.i = 0}});
  push_stack(config.vm->stack, (VALUE_t){VALUE_int, {.i = 0}});
  (void)lc_list_slice(test_ctx(), NULL, NULL);
  VALUE_t empty_slice = pop_stack(config.vm->stack);
  ASSERT_EQ_INT(VALUE_list, empty_slice.type);
  ASSERT_EQ_INT(0, sin_list_count(empty_slice.list));
  value_free(&empty_slice);
  push_stack(config.vm->stack,
             (VALUE_t){VALUE_list, {.list = sin_list_retain(left)}});
  push_stack(config.vm->stack,
             (VALUE_t){VALUE_list, {.list = sin_list_retain(empty)}});
  (void)lc_list_concat(test_ctx(), NULL, NULL);
  VALUE_t same_values = pop_stack(config.vm->stack);
  assert_list_ints(&same_values, left_values, 2);
  value_free(&same_values);

  VALUE_t complex_values[3] = {
      {VALUE_str, {.s = strdup("owned")}},
      {VALUE_list, {.list = list_of_ints(8, 9)}},
      {VALUE_itemref, {.itemref = sin_itemref_create("root.child")}}};
  ASSERT_NOT_NULL(complex_values[0].s);
  ASSERT_NOT_NULL(complex_values[1].list);
  ASSERT_NOT_NULL(complex_values[2].itemref);
  SIN_LIST_t *nested_handle = complex_values[1].list;
  SIN_ITEMREF_t *ref_handle = complex_values[2].itemref;
  SIN_LIST_t *complex = sin_list_build_owned(complex_values, 3);
  ASSERT_NOT_NULL(complex);
  VALUE_t owned_string = list_get_result(complex, 0);
  VALUE_t owned_nested = list_get_result(complex, 1);
  VALUE_t owned_ref = list_get_result(complex, 2);
  ASSERT_TRUE(owned_nested.list == nested_handle);
  ASSERT_TRUE(owned_ref.itemref == ref_handle);
  sin_list_release(complex);
  ASSERT_EQ_INT(VALUE_str, owned_string.type);
  ASSERT_TRUE(strcmp(owned_string.s, "owned") == 0);
  ASSERT_EQ_INT(VALUE_list, owned_nested.type);
  ASSERT_EQ_INT(9, sin_list_get(owned_nested.list, 1)->i);
  ASSERT_EQ_INT(VALUE_itemref, owned_ref.type);
  ASSERT_TRUE(strcmp(sin_itemref_path(owned_ref.itemref), "root.child") == 0);
  value_free(&owned_string);
  value_free(&owned_nested);
  value_free(&owned_ref);

  sin_list_release(empty);
  sin_list_release(left);
  sin_list_release(right);
  teardown_libcall_runtime();
}

void test_list_libcall_invalid_types_and_ranges(void) {
  setup_libcall_runtime();
  SIN_LIST_t *list = list_of_ints(1, 2);
  ASSERT_NOT_NULL(list);
  set_error_item(itemstore_root(config.itemstore_ctx), ERR_NETWORK_ERROR,
                 "prior error", NULL);
  VALUE_t ret = list_get_result(list, -1);
  ASSERT_EQ_INT(VALUE_nil, ret.type);
  ITEM_t *error = find_item(itemstore_root(config.itemstore_ctx), "error");
  ASSERT_NOT_NULL(error);
  ASSERT_EQ_INT(ERR_NETWORK_ERROR, item_value(error)->i);
  ret = list_get_result(list, INT64_MAX);
  ASSERT_EQ_INT(VALUE_nil, ret.type);
  push_stack(config.vm->stack,
             (VALUE_t){VALUE_list, {.list = sin_list_retain(list)}});
  push_stack(config.vm->stack, (VALUE_t){VALUE_int, {.i = 2}});
  push_stack(config.vm->stack, (VALUE_t){VALUE_int, {.i = 7}});
  (void)lc_list_set(test_ctx(), NULL, NULL);
  ret = pop_stack(config.vm->stack);
  ASSERT_EQ_INT(VALUE_nil, ret.type);
  push_stack(config.vm->stack,
             (VALUE_t){VALUE_list, {.list = sin_list_retain(list)}});
  push_stack(config.vm->stack, (VALUE_t){VALUE_int, {.i = 1}});
  push_stack(config.vm->stack, (VALUE_t){VALUE_int, {.i = INT64_MAX}});
  (void)lc_list_slice(test_ctx(), NULL, NULL);
  ret = pop_stack(config.vm->stack);
  ASSERT_EQ_INT(VALUE_nil, ret.type);
  ASSERT_EQ_INT(ERR_NETWORK_ERROR, item_value(error)->i);

  VALUE_t get_args[] = {
      {VALUE_list, {.list = list_of_ints(1, 2)}},
      {VALUE_float, {.f = 0.0}}};
  VALUE_t append_args[] = {
      {VALUE_int, {.i = 1}},
      {VALUE_int, {.i = 2}}};
  VALUE_t set_args[] = {
      {VALUE_list, {.list = list_of_ints(1, 2)}},
      {VALUE_float, {.f = 0.0}},
      {VALUE_int, {.i = 3}}};
  VALUE_t concat_args[] = {
      {VALUE_list, {.list = list_of_ints(1, 2)}},
      {VALUE_int, {.i = 3}}};
  VALUE_t slice_args[] = {
      {VALUE_list, {.list = list_of_ints(1, 2)}},
      {VALUE_int, {.i = 0}},
      {VALUE_float, {.f = 1.0}}};
  ASSERT_NOT_NULL(get_args[0].list);
  ASSERT_NOT_NULL(set_args[0].list);
  ASSERT_NOT_NULL(concat_args[0].list);
  ASSERT_NOT_NULL(slice_args[0].list);
  assert_invalid_list_call(lc_list_get, get_args, 2, "list.get");
  assert_invalid_list_call(lc_list_append, append_args, 2, "list.append");
  assert_invalid_list_call(lc_list_set, set_args, 3, "list.set");
  assert_invalid_list_call(lc_list_concat, concat_args, 2, "list.concat");
  assert_invalid_list_call(lc_list_slice, slice_args, 3, "list.slice");

  SIN_LIST_t *failure_left = list_of_ints(1, 2);
  SIN_LIST_t *failure_right = list_of_ints(3, 4);
  ASSERT_NOT_NULL(failure_left);
  ASSERT_NOT_NULL(failure_right);
  push_stack(config.vm->stack,
             (VALUE_t){VALUE_list, {.list = sin_list_retain(failure_left)}});
  push_stack(config.vm->stack,
             (VALUE_t){VALUE_list, {.list = sin_list_retain(failure_right)}});
  alloc_test_fail_after(0);
  (void)lc_list_concat(test_ctx(), NULL, NULL);
  alloc_test_fail_after(-1);
  ret = pop_stack(config.vm->stack);
  ASSERT_EQ_INT(VALUE_nil, ret.type);
  ASSERT_EQ_INT(2, sin_list_count(failure_left));
  ASSERT_EQ_INT(2, sin_list_count(failure_right));

  push_stack(config.vm->stack,
             (VALUE_t){VALUE_list, {.list = sin_list_retain(failure_left)}});
  push_stack(config.vm->stack, (VALUE_t){VALUE_int, {.i = 0}});
  push_stack(config.vm->stack, (VALUE_t){VALUE_int, {.i = 1}});
  alloc_test_fail_after(0);
  (void)lc_list_slice(test_ctx(), NULL, NULL);
  alloc_test_fail_after(-1);
  ret = pop_stack(config.vm->stack);
  ASSERT_EQ_INT(VALUE_nil, ret.type);
  ASSERT_EQ_INT(2, sin_list_count(failure_left));
  sin_list_release(failure_left);
  sin_list_release(failure_right);

  ITEM_t *context_root = make_root_item("list-context");
  ASSERT_NOT_NULL(context_root);
  ITEM_t *caller = test_item_set_value(
      context_root, "caller", (VALUE_t){VALUE_int, {.i = 1}});
  ASSERT_NOT_NULL(caller);
  RuntimeContext context = *test_ctx();
  context.itemstore = itemstore_owner(context_root);
  context.current_item = caller;
  push_stack(config.vm->stack, (VALUE_t){VALUE_int, {.i = 1}});
  (void)lc_list_length(&context, NULL, context_root);
  ret = pop_stack(config.vm->stack);
  ASSERT_EQ_INT(VALUE_nil, ret.type);
  error = find_item(context_root, "error");
  ASSERT_NOT_NULL(error);
  ASSERT_EQ_INT(ERR_RUNTIME_INVALIDARGS, item_value(error)->i);
  ITEM_t *message = find_item(context_root, "error.msg");
  ASSERT_NOT_NULL(message);
  ASSERT_TRUE(strstr(item_value(message)->s, "list.length") != NULL);
  ITEM_t *provenance = find_item(context_root, "error.item");
  ASSERT_NOT_NULL(provenance);
  ASSERT_EQ_INT(VALUE_str, item_value(provenance)->type);
  ASSERT_TRUE(strcmp(item_value(provenance)->s, "caller") == 0);
  destroy_item(context_root);
  sin_list_release(list);
  teardown_libcall_runtime();
}

void test_list_libcall_source_integration(void) {
  setup_libcall_runtime();
  VALUE_t source = {VALUE_str, {.s = strdup(
      "result.a = list.length{#[1, 2]}; result.b = list.get{#[1, 2], 0}; "
      "result.c = list.append{#[1], 2}; result.d = list.set{#[1, 2], 0, 3}; "
      "result.e = list.concat{#[1], #[2]}; result.f = list.slice{#[1, 2], 0, 1};")}};
  push_stack(config.vm->stack, source);
  (void)lc_sys_compile(test_ctx(), NULL, NULL);
  VALUE_t compiled = pop_stack(config.vm->stack);
  ASSERT_EQ_INT(VALUE_bool, compiled.type);
  ASSERT_EQ_INT(1, compiled.i);
  ITEM_t *a = find_item(itemstore_root(config.itemstore_ctx), "result.a");
  ITEM_t *b = find_item(itemstore_root(config.itemstore_ctx), "result.b");
  ITEM_t *c = find_item(itemstore_root(config.itemstore_ctx), "result.c");
  ITEM_t *d = find_item(itemstore_root(config.itemstore_ctx), "result.d");
  ITEM_t *e = find_item(itemstore_root(config.itemstore_ctx), "result.e");
  ITEM_t *f = find_item(itemstore_root(config.itemstore_ctx), "result.f");
  ASSERT_NOT_NULL(a);
  ASSERT_NOT_NULL(b);
  ASSERT_NOT_NULL(c);
  ASSERT_NOT_NULL(d);
  ASSERT_NOT_NULL(e);
  ASSERT_NOT_NULL(f);
  ASSERT_EQ_INT(2, item_value(a)->i);
  ASSERT_EQ_INT(1, item_value(b)->i);
  const int pair[] = {1, 2};
  const int replaced[] = {3, 2};
  const int singleton[] = {1};
  assert_list_ints(item_value(c), pair, 2);
  assert_list_ints(item_value(d), replaced, 2);
  assert_list_ints(item_value(e), pair, 2);
  assert_list_ints(item_value(f), singleton, 1);
  teardown_libcall_runtime();
}
