#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <math.h>

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
  const char *names[] = {"length", "get", "append", "set", "concat", "slice",
                         "islist", "reverse", "asc", "desc"};
  const uint8_t arities[] = {1, 2, 2, 3, 2, 3, 1, 1, 1, 1};
  OP_t handlers[] = {lc_list_length, lc_list_get, lc_list_append,
                     lc_list_set, lc_list_concat, lc_list_slice,
                     lc_list_islist, lc_list_reverse, lc_list_asc, lc_list_desc};
  for (size_t i = 0; i < 10; ++i) {
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

  ITEM_t *context_root = make_root_item("list_context");
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
      "result.e = list.concat{#[1], #[2]}; result.f = list.slice{#[1, 2], 0, 1}; "
      "result.g = list.islist{#[1]}; result.h = list.islist{1}; "
      "result.i = list.reverse{#[1, 2, 3]}; result.j = list.asc{#[3, 1, 2]}; "
      "result.k = list.desc{#[1, 3, 2]};")}};
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
  ITEM_t *g = find_item(itemstore_root(config.itemstore_ctx), "result.g");
  ITEM_t *h = find_item(itemstore_root(config.itemstore_ctx), "result.h");
  ITEM_t *i = find_item(itemstore_root(config.itemstore_ctx), "result.i");
  ITEM_t *j = find_item(itemstore_root(config.itemstore_ctx), "result.j");
  ITEM_t *k = find_item(itemstore_root(config.itemstore_ctx), "result.k");
  ASSERT_NOT_NULL(a);
  ASSERT_NOT_NULL(b);
  ASSERT_NOT_NULL(c);
  ASSERT_NOT_NULL(d);
  ASSERT_NOT_NULL(e);
  ASSERT_NOT_NULL(f);
  ASSERT_NOT_NULL(g);
  ASSERT_NOT_NULL(h);
  ASSERT_NOT_NULL(i);
  ASSERT_NOT_NULL(j);
  ASSERT_NOT_NULL(k);
  ASSERT_EQ_INT(2, item_value(a)->i);
  ASSERT_EQ_INT(1, item_value(b)->i);
  const int pair[] = {1, 2};
  const int replaced[] = {3, 2};
  const int singleton[] = {1};
  assert_list_ints(item_value(c), pair, 2);
  assert_list_ints(item_value(d), replaced, 2);
  assert_list_ints(item_value(e), pair, 2);
  assert_list_ints(item_value(f), singleton, 1);
  ASSERT_EQ_INT(VALUE_bool, item_value(g)->type);
  ASSERT_EQ_INT(1, item_value(g)->i);
  ASSERT_EQ_INT(VALUE_bool, item_value(h)->type);
  ASSERT_EQ_INT(0, item_value(h)->i);
  const int reversed[] = {3, 2, 1};
  const int ascending[] = {1, 2, 3};
  const int descending[] = {3, 2, 1};
  assert_list_ints(item_value(i), reversed, 3);
  assert_list_ints(item_value(j), ascending, 3);
  assert_list_ints(item_value(k), descending, 3);
  teardown_libcall_runtime();
}

void test_list_libcall_islist(void) {
  setup_libcall_runtime();
  set_error_item(itemstore_root(config.itemstore_ctx), ERR_NETWORK_ERROR,
                 "prior error", NULL);

  SIN_LIST_t *nonempty = list_of_ints(1, 2);
  SIN_LIST_t *empty = sin_list_build_owned(NULL, 0);
  ASSERT_NOT_NULL(nonempty);
  ASSERT_NOT_NULL(empty);

  VALUE_t cases[] = {
      VALUE_NIL,
      {VALUE_bool, {.i = 1}},
      {VALUE_int, {.i = 42}},
      {VALUE_float, {.f = 3.5}},
      {VALUE_str, {.s = strdup("hello")}},
      {VALUE_itemref, {.itemref = sin_itemref_create("root.child")}},
      {VALUE_list, {.list = sin_list_retain(nonempty)}},
      {VALUE_list, {.list = sin_list_retain(empty)}},
  };
  const int expected[] = {0, 0, 0, 0, 0, 0, 1, 1};
  size_t count = sizeof(cases) / sizeof(cases[0]);

  for (size_t i = 0; i < count; ++i) {
    push_stack(config.vm->stack, cases[i]);
    (void)lc_list_islist(test_ctx(), NULL, NULL);
    VALUE_t result = pop_stack(config.vm->stack);
    ASSERT_EQ_INT(VALUE_bool, result.type);
    ASSERT_EQ_INT(expected[i], result.i);
  }

  ITEM_t *error = find_item(itemstore_root(config.itemstore_ctx), "error");
  ASSERT_NOT_NULL(error);
  ASSERT_EQ_INT(ERR_NETWORK_ERROR, item_value(error)->i);

  sin_list_release(nonempty);
  sin_list_release(empty);
  teardown_libcall_runtime();
}

static VALUE_t call_list_unary(OP_t handler, VALUE_t value) {
  push_stack(config.vm->stack, value);
  (void)handler(test_ctx(), NULL, itemstore_root(config.itemstore_ctx));
  return pop_stack(config.vm->stack);
}

static void assert_value_payload(const VALUE_t *actual, const VALUE_t *expected);

static void assert_list_values(const VALUE_t *value, const VALUE_t *expected,
                               size_t count) {
  ASSERT_EQ_INT(VALUE_list, value->type);
  ASSERT_EQ_INT(count, sin_list_count(value->list));
  for (size_t i = 0; i < count; ++i) {
    const VALUE_t *actual = sin_list_get(value->list, i);
    ASSERT_NOT_NULL(actual);
    assert_value_payload(actual, &expected[i]);
  }
}

static void assert_value_payload(const VALUE_t *actual, const VALUE_t *expected) {
  ASSERT_EQ_INT(expected->type, actual->type);
  switch (actual->type) {
    case VALUE_nil:
      break;
    case VALUE_int:
    case VALUE_bool:
      ASSERT_EQ_INT(expected->i, actual->i);
      break;
    case VALUE_float:
      ASSERT_TRUE(expected->f == actual->f);
      break;
    case VALUE_str:
      ASSERT_TRUE(strcmp(expected->s ? expected->s : "",
                         actual->s ? actual->s : "") == 0);
      break;
    case VALUE_itemref:
      ASSERT_TRUE(strcmp(sin_itemref_path(expected->itemref),
                         sin_itemref_path(actual->itemref)) == 0);
      break;
    case VALUE_list:
      ASSERT_EQ_INT(sin_list_count(expected->list), sin_list_count(actual->list));
      for (size_t i = 0; i < sin_list_count(actual->list); ++i) {
        const VALUE_t *nested_actual = sin_list_get(actual->list, i);
        const VALUE_t *nested_expected = sin_list_get(expected->list, i);
        ASSERT_NOT_NULL(nested_actual);
        ASSERT_NOT_NULL(nested_expected);
        assert_value_payload(nested_actual, nested_expected);
      }
      break;
  }
}

void test_list_libcall_ordering(void) {
  setup_libcall_runtime();
  VALUE_t result = VALUE_NIL;

  SIN_LIST_t *nested = list_of_ints(8, 9);
  VALUE_t heterogeneous_values[] = {
      {VALUE_int, {.i = 1}},
      {VALUE_str, {.s = strdup("text")}},
      {VALUE_bool, {.i = 1}},
      VALUE_NIL,
      {VALUE_list, {.list = sin_list_retain(nested)}},
      {VALUE_itemref, {.itemref = sin_itemref_create("root.child")}}};
  SIN_LIST_t *nested_handle = heterogeneous_values[4].list;
  SIN_ITEMREF_t *ref_handle = heterogeneous_values[5].itemref;
  SIN_LIST_t *heterogeneous = sin_list_build_owned(heterogeneous_values,
                                                   sizeof(heterogeneous_values) /
                                                       sizeof(heterogeneous_values[0]));
  ASSERT_NOT_NULL(heterogeneous);
  VALUE_t reversed = call_list_unary(
      lc_list_reverse,
      (VALUE_t){VALUE_list, {.list = sin_list_retain(heterogeneous)}});
  VALUE_t expected_reversed[] = {
      {VALUE_itemref, {.itemref = ref_handle}},
      {VALUE_list, {.list = nested_handle}},
      VALUE_NIL,
      {VALUE_bool, {.i = 1}},
      {VALUE_str, {.s = "text"}},
      {VALUE_int, {.i = 1}}};
  assert_list_values(&reversed, expected_reversed,
                     sizeof(expected_reversed) / sizeof(expected_reversed[0]));
  ASSERT_EQ_INT(6, sin_list_count(heterogeneous));
  ASSERT_EQ_INT(VALUE_int, sin_list_get(heterogeneous, 0)->type);
  ASSERT_EQ_INT(1, sin_list_get(heterogeneous, 0)->i);
  value_free(&reversed);
  sin_list_release(nested);
  sin_list_release(heterogeneous);

  SIN_LIST_t *reverse_empty = sin_list_build_owned(NULL, 0);
  ASSERT_NOT_NULL(reverse_empty);
  reversed = call_list_unary(
      lc_list_reverse, (VALUE_t){VALUE_list, {.list = sin_list_retain(reverse_empty)}});
  assert_list_values(&reversed, NULL, 0);
  value_free(&reversed);
  sin_list_release(reverse_empty);

  VALUE_t reverse_singleton_value = {VALUE_int, {.i = 42}};
  SIN_LIST_t *reverse_singleton = sin_list_build_owned(&reverse_singleton_value, 1);
  ASSERT_NOT_NULL(reverse_singleton);
  reversed = call_list_unary(
      lc_list_reverse,
      (VALUE_t){VALUE_list, {.list = sin_list_retain(reverse_singleton)}});
  const VALUE_t reverse_singleton_expected[] = {{VALUE_int, {.i = 42}}};
  assert_list_values(&reversed, reverse_singleton_expected, 1);
  value_free(&reversed);
  sin_list_release(reverse_singleton);

  SIN_LIST_t *numbers;
  VALUE_t number_values[] = {
      {VALUE_int, {.i = 3}},
      {VALUE_float, {.f = 1.5}},
      {VALUE_int, {.i = 1}},
      {VALUE_float, {.f = 1.0}},
      {VALUE_int, {.i = -2}},
      {VALUE_float, {.f = INFINITY}}};
  numbers = sin_list_build_owned(number_values,
                                 sizeof(number_values) / sizeof(number_values[0]));
  ASSERT_NOT_NULL(numbers);
  VALUE_t expected_ascending[] = {
      {VALUE_int, {.i = -2}}, {VALUE_int, {.i = 1}},
      {VALUE_float, {.f = 1.0}}, {VALUE_float, {.f = 1.5}},
      {VALUE_int, {.i = 3}}, {VALUE_float, {.f = INFINITY}}};
  VALUE_t sorted = call_list_unary(
      lc_list_asc, (VALUE_t){VALUE_list, {.list = sin_list_retain(numbers)}});
  assert_list_values(&sorted, expected_ascending,
                     sizeof(expected_ascending) / sizeof(expected_ascending[0]));
  value_free(&sorted);
  VALUE_t expected_descending[] = {
      {VALUE_float, {.f = INFINITY}}, {VALUE_int, {.i = 3}},
      {VALUE_float, {.f = 1.5}}, {VALUE_int, {.i = 1}},
      {VALUE_float, {.f = 1.0}}, {VALUE_int, {.i = -2}}};
  sorted = call_list_unary(
      lc_list_desc, (VALUE_t){VALUE_list, {.list = sin_list_retain(numbers)}});
  assert_list_values(&sorted, expected_descending,
                     sizeof(expected_descending) / sizeof(expected_descending[0]));
  value_free(&sorted);
  ASSERT_EQ_INT(3, sin_list_get(numbers, 0)->i);
  sin_list_release(numbers);

  VALUE_t promoted_ascending_values[] = {
      {VALUE_int, {.i = INT64_C(9007199254740993)}},
      {VALUE_int, {.i = INT64_C(9007199254740992)}},
      {VALUE_float, {.f = 9007199254740992.0}}};
  SIN_LIST_t *promoted_ascending =
      sin_list_build_owned(promoted_ascending_values, 3);
  ASSERT_NOT_NULL(promoted_ascending);
  const VALUE_t promoted_ascending_expected[] = {
      {VALUE_int, {.i = INT64_C(9007199254740993)}},
      {VALUE_int, {.i = INT64_C(9007199254740992)}},
      {VALUE_float, {.f = 9007199254740992.0}}};
  sorted = call_list_unary(
      lc_list_asc,
      (VALUE_t){VALUE_list, {.list = sin_list_retain(promoted_ascending)}});
  assert_list_values(&sorted, promoted_ascending_expected, 3);
  value_free(&sorted);

  VALUE_t promoted_descending_values[] = {
      {VALUE_int, {.i = INT64_C(9007199254740992)}},
      {VALUE_int, {.i = INT64_C(9007199254740993)}},
      {VALUE_float, {.f = 9007199254740992.0}}};
  SIN_LIST_t *promoted_descending =
      sin_list_build_owned(promoted_descending_values, 3);
  ASSERT_NOT_NULL(promoted_descending);
  const VALUE_t promoted_descending_expected[] = {
      {VALUE_int, {.i = INT64_C(9007199254740992)}},
      {VALUE_int, {.i = INT64_C(9007199254740993)}},
      {VALUE_float, {.f = 9007199254740992.0}}};
  sorted = call_list_unary(
      lc_list_desc,
      (VALUE_t){VALUE_list, {.list = sin_list_retain(promoted_descending)}});
  assert_list_values(&sorted, promoted_descending_expected, 3);
  value_free(&sorted);
  sin_list_release(promoted_ascending);
  sin_list_release(promoted_descending);

  VALUE_t exact_integer_values[] = {
      {VALUE_int, {.i = INT64_C(9007199254740993)}},
      {VALUE_int, {.i = INT64_C(9007199254740992)}}};
  SIN_LIST_t *exact_integers = sin_list_build_owned(exact_integer_values, 2);
  ASSERT_NOT_NULL(exact_integers);
  const VALUE_t exact_integer_ascending[] = {
      {VALUE_int, {.i = INT64_C(9007199254740992)}},
      {VALUE_int, {.i = INT64_C(9007199254740993)}}};
  const VALUE_t exact_integer_descending[] = {
      {VALUE_int, {.i = INT64_C(9007199254740993)}},
      {VALUE_int, {.i = INT64_C(9007199254740992)}}};
  sorted = call_list_unary(
      lc_list_asc,
      (VALUE_t){VALUE_list, {.list = sin_list_retain(exact_integers)}});
  assert_list_values(&sorted, exact_integer_ascending, 2);
  value_free(&sorted);
  sorted = call_list_unary(
      lc_list_desc,
      (VALUE_t){VALUE_list, {.list = sin_list_retain(exact_integers)}});
  assert_list_values(&sorted, exact_integer_descending, 2);
  value_free(&sorted);
  sin_list_release(exact_integers);

  VALUE_t bool_values[] = {{VALUE_bool, {.i = 1}}, {VALUE_bool, {.i = 0}},
                           {VALUE_bool, {.i = 1}}};
  SIN_LIST_t *bools = sin_list_build_owned(bool_values, 3);
  ASSERT_NOT_NULL(bools);
  sorted = call_list_unary(
      lc_list_asc, (VALUE_t){VALUE_list, {.list = sin_list_retain(bools)}});
  VALUE_t expected_bools[] = {{VALUE_bool, {.i = 0}}, {VALUE_bool, {.i = 1}},
                              {VALUE_bool, {.i = 1}}};
  assert_list_values(&sorted, expected_bools, 3);
  value_free(&sorted);
  sin_list_release(bools);

  VALUE_t string_values[] = {{VALUE_str, {.s = strdup("a")}},
                             {VALUE_str, {.s = strdup("A")}},
                             {VALUE_str, {.s = strdup("aa")}},
                             {VALUE_str, {.s = strdup("a")}}};
  SIN_LIST_t *strings = sin_list_build_owned(string_values, 4);
  ASSERT_NOT_NULL(strings);
  sorted = call_list_unary(
      lc_list_asc, (VALUE_t){VALUE_list, {.list = sin_list_retain(strings)}});
  VALUE_t expected_strings[] = {{VALUE_str, {.s = "A"}},
                                {VALUE_str, {.s = "a"}},
                                {VALUE_str, {.s = "a"}},
                                {VALUE_str, {.s = "aa"}}};
  assert_list_values(&sorted, expected_strings, 4);
  value_free(&sorted);
  sin_list_release(strings);

  SIN_LIST_t *empty = sin_list_build_owned(NULL, 0);
  ASSERT_NOT_NULL(empty);
  sorted = call_list_unary(
      lc_list_asc, (VALUE_t){VALUE_list, {.list = sin_list_retain(empty)}});
  ASSERT_EQ_INT(VALUE_list, sorted.type);
  ASSERT_EQ_INT(0, sin_list_count(sorted.list));
  value_free(&sorted);
  sin_list_release(empty);

  VALUE_t singleton_int_value = {VALUE_int, {.i = 7}};
  SIN_LIST_t *singleton_int = sin_list_build_owned(
      &singleton_int_value, 1);
  ASSERT_NOT_NULL(singleton_int);
  const VALUE_t singleton_int_expected[] = {{VALUE_int, {.i = 7}}};
  const OP_t sort_handlers[] = {lc_list_asc, lc_list_desc};
  for (size_t i = 0; i < 2; ++i) {
    sorted = call_list_unary(
        sort_handlers[i],
        (VALUE_t){VALUE_list, {.list = sin_list_retain(singleton_int)}});
    assert_list_values(&sorted, singleton_int_expected, 1);
    value_free(&sorted);
  }
  sin_list_release(singleton_int);

  VALUE_t singleton_string_value = {VALUE_str, {.s = strdup("singleton")}};
  SIN_LIST_t *singleton_string = sin_list_build_owned(&singleton_string_value, 1);
  ASSERT_NOT_NULL(singleton_string);
  const VALUE_t singleton_string_expected[] = {{VALUE_str, {.s = "singleton"}}};
  for (size_t i = 0; i < 2; ++i) {
    sorted = call_list_unary(
        sort_handlers[i],
        (VALUE_t){VALUE_list, {.list = sin_list_retain(singleton_string)}});
    assert_list_values(&sorted, singleton_string_expected, 1);
    value_free(&sorted);
  }
  sin_list_release(singleton_string);

  VALUE_t singleton_bool_value = {VALUE_bool, {.i = 1}};
  SIN_LIST_t *singleton_bool = sin_list_build_owned(
      &singleton_bool_value, 1);
  ASSERT_NOT_NULL(singleton_bool);
  const VALUE_t singleton_bool_expected[] = {{VALUE_bool, {.i = 1}}};
  for (size_t i = 0; i < 2; ++i) {
    sorted = call_list_unary(
        sort_handlers[i],
        (VALUE_t){VALUE_list, {.list = sin_list_retain(singleton_bool)}});
    assert_list_values(&sorted, singleton_bool_expected, 1);
    value_free(&sorted);
  }
  sin_list_release(singleton_bool);

  VALUE_t large_values[40];
  int large_ascending[40];
  int large_descending[40];
  for (size_t i = 0; i < 40; ++i) {
    large_values[i] = (VALUE_t){VALUE_int, {.i = (int64_t)(39u - i)}};
    large_ascending[i] = (int)i;
    large_descending[i] = 39 - (int)i;
  }
  SIN_LIST_t *large = sin_list_build_owned(large_values, 40);
  ASSERT_NOT_NULL(large);
  sorted = call_list_unary(
      lc_list_asc, (VALUE_t){VALUE_list, {.list = sin_list_retain(large)}});
  assert_list_ints(&sorted, large_ascending, 40);
  value_free(&sorted);
  sorted = call_list_unary(
      lc_list_desc, (VALUE_t){VALUE_list, {.list = sin_list_retain(large)}});
  assert_list_ints(&sorted, large_descending, 40);
  value_free(&sorted);
  sin_list_release(large);

  VALUE_t invalid_values[] = {
      VALUE_NIL,
      {VALUE_itemref, {.itemref = sin_itemref_create("root.child")}},
      {VALUE_list, {.list = sin_list_build_owned(NULL, 0)}}};
  for (size_t i = 0; i < sizeof(invalid_values) / sizeof(invalid_values[0]); ++i) {
    VALUE_t one[] = {invalid_values[i]};
    SIN_LIST_t *invalid = sin_list_build_owned(one, 1);
    invalid_values[i] = VALUE_NIL;
    ASSERT_NOT_NULL(invalid);
    for (size_t handler_index = 0; handler_index < 2; ++handler_index) {
      result = call_list_unary(
          sort_handlers[handler_index],
          (VALUE_t){VALUE_list, {.list = sin_list_retain(invalid)}});
      ASSERT_EQ_INT(VALUE_nil, result.type);
      assert_invalid_args_detail_contains(handler_index == 0 ? "list.asc" :
                                                                  "list.desc");
    }
    sin_list_release(invalid);
  }

  const OP_t ordering_handlers[] = {lc_list_reverse, lc_list_asc, lc_list_desc};
  const char *ordering_names[] = {"list.reverse", "list.asc", "list.desc"};
  for (size_t i = 0; i < 3; ++i) {
    result = call_list_unary(ordering_handlers[i],
                             (VALUE_t){VALUE_list, {.list = NULL}});
    ASSERT_EQ_INT(VALUE_nil, result.type);
    assert_invalid_args_detail_contains(ordering_names[i]);
    result = call_list_unary(ordering_handlers[i],
                             (VALUE_t){VALUE_int, {.i = 1}});
    ASSERT_EQ_INT(VALUE_nil, result.type);
    assert_invalid_args_detail_contains(ordering_names[i]);
  }

  VALUE_t mixed[] = {{VALUE_int, {.i = 1}}, {VALUE_str, {.s = strdup("x")}}};
  SIN_LIST_t *mixed_list = sin_list_build_owned(mixed, 2);
  ASSERT_NOT_NULL(mixed_list);
  for (size_t handler_index = 0; handler_index < 2; ++handler_index) {
    result = call_list_unary(
        sort_handlers[handler_index],
        (VALUE_t){VALUE_list, {.list = sin_list_retain(mixed_list)}});
    ASSERT_EQ_INT(VALUE_nil, result.type);
    assert_invalid_args_detail_contains(handler_index == 0 ? "list.asc" :
                                                                "list.desc");
  }
  sin_list_release(mixed_list);

  VALUE_t nan_value = {VALUE_float, {.f = NAN}};
  SIN_LIST_t *nan_list = sin_list_build_owned(&nan_value, 1);
  ASSERT_NOT_NULL(nan_list);
  result = call_list_unary(
      lc_list_asc, (VALUE_t){VALUE_list, {.list = sin_list_retain(nan_list)}});
  ASSERT_EQ_INT(VALUE_nil, result.type);
  ITEM_t *error = find_item(itemstore_root(config.itemstore_ctx), "error");
  ASSERT_NOT_NULL(error);
  ASSERT_EQ_INT(ERR_RUNTIME_UNDEFINED, item_value(error)->i);
  sin_list_release(nan_list);

  result = call_list_unary(lc_list_asc, (VALUE_t){VALUE_int, {.i = 1}});
  ASSERT_EQ_INT(VALUE_nil, result.type);
  assert_invalid_args_detail_contains("list.asc");

  SIN_LIST_t *prior_error_list = list_of_ints(2, 1);
  ASSERT_NOT_NULL(prior_error_list);
  set_error_item(itemstore_root(config.itemstore_ctx), ERR_NETWORK_ERROR,
                 "prior error", NULL);
  for (size_t i = 0; i < 3; ++i) {
    result = call_list_unary(
        ordering_handlers[i],
        (VALUE_t){VALUE_list, {.list = sin_list_retain(prior_error_list)}});
    ASSERT_EQ_INT(VALUE_list, result.type);
    value_free(&result);
    ASSERT_EQ_INT(ERR_NETWORK_ERROR, item_value(error)->i);
  }
  sin_list_release(prior_error_list);

  VALUE_t clone_failure_values[] = {
      {VALUE_str, {.s = strdup("first")}},
      {VALUE_str, {.s = strdup("second")}}};
  SIN_LIST_t *clone_failure = sin_list_build_owned(clone_failure_values, 2);
  ASSERT_NOT_NULL(clone_failure);
  set_error_item(itemstore_root(config.itemstore_ctx), ERR_NETWORK_ERROR,
                 "prior error", NULL);
  const OP_t clone_failure_handlers[] = {
      lc_list_reverse, lc_list_asc, lc_list_desc, lc_list_asc, lc_list_desc};
  const long clone_failure_points[] = {3, 3, 3, 4, 5};
  for (size_t i = 0; i < 5; ++i) {
    alloc_test_fail_after(clone_failure_points[i]);
    result = call_list_unary(
        clone_failure_handlers[i],
        (VALUE_t){VALUE_list, {.list = sin_list_retain(clone_failure)}});
    alloc_test_fail_after(-1);
    ASSERT_EQ_INT(VALUE_nil, result.type);
    ASSERT_EQ_INT(ERR_NETWORK_ERROR, item_value(error)->i);
    ASSERT_EQ_INT(2, sin_list_count(clone_failure));
    ASSERT_EQ_INT(VALUE_str, sin_list_get(clone_failure, 0)->type);
    ASSERT_EQ_INT(VALUE_str, sin_list_get(clone_failure, 1)->type);
    ASSERT_TRUE(strcmp(sin_list_get(clone_failure, 0)->s, "first") == 0);
    ASSERT_TRUE(strcmp(sin_list_get(clone_failure, 1)->s, "second") == 0);
  }
  sin_list_release(clone_failure);

  SIN_LIST_t *build_failure = list_of_ints(2, 1);
  ASSERT_NOT_NULL(build_failure);
  for (size_t i = 1; i < 3; ++i) {
    alloc_test_fail_after(2);
    result = call_list_unary(
        ordering_handlers[i],
        (VALUE_t){VALUE_list, {.list = sin_list_retain(build_failure)}});
    alloc_test_fail_after(-1);
    ASSERT_EQ_INT(VALUE_nil, result.type);
    ASSERT_EQ_INT(ERR_NETWORK_ERROR, item_value(error)->i);
  }
  for (size_t i = 0; i < 3; ++i) {
    alloc_test_fail_after(i == 0 ? 2 : 3);
    result = call_list_unary(
        ordering_handlers[i],
        (VALUE_t){VALUE_list, {.list = sin_list_retain(build_failure)}});
    alloc_test_fail_after(-1);
    ASSERT_EQ_INT(VALUE_nil, result.type);
    ASSERT_EQ_INT(ERR_NETWORK_ERROR, item_value(error)->i);
  }
  sin_list_release(build_failure);

  SIN_LIST_t *failure = list_of_ints(4, 2);
  ASSERT_NOT_NULL(failure);
  set_error_item(itemstore_root(config.itemstore_ctx), ERR_NETWORK_ERROR,
                 "prior error", NULL);
  alloc_test_fail_after(0);
  result = call_list_unary(
      lc_list_reverse, (VALUE_t){VALUE_list, {.list = sin_list_retain(failure)}});
  alloc_test_fail_after(-1);
  ASSERT_EQ_INT(VALUE_nil, result.type);
  ASSERT_EQ_INT(ERR_NETWORK_ERROR, item_value(error)->i);
  ASSERT_EQ_INT(4, sin_list_get(failure, 0)->i);
  sin_list_release(failure);
  teardown_libcall_runtime();
}
