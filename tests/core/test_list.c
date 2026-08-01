#include <stdlib.h>
#include <string.h>

#include "list.h"
#include "memory.h"
#include "test_assert.h"
#include "value.h"
#include "itemref.h"
#include "string_limits.h"

static SIN_LIST_t *make_int_list(size_t count) {
  VALUE_t *values = count == 0 ? NULL : calloc(count, sizeof(*values));
  SIN_LIST_t *list;
  if (count != 0 && !values) return NULL;
  for (size_t i = 0; i < count; ++i) values[i] = (VALUE_t){VALUE_int, {.i = (int64_t)i}};
  list = sin_list_build_owned(values, count);
  free(values);
  return list;
}

void test_list_basic_ownership_and_access(void) {
  SIN_LIST_t *empty = sin_list_build_owned(NULL, 0);
  VALUE_t empty_value;
  VALUE_t *values = calloc(3, sizeof(*values));
  SIN_LIST_t *small;
  VALUE_t clone;
  VALUE_t moved = VALUE_NIL;
  SIN_ITEMREF_t *ref;
  ASSERT_NOT_NULL(empty);
  ASSERT_EQ_INT(0, sin_list_count(empty));
  ASSERT_EQ_INT(1, sin_list_depth(empty));
  empty_value = (VALUE_t){VALUE_list, {.list = empty}};
  ASSERT_TRUE(!value_is_truthy(&empty_value));
  ASSERT_TRUE(strcmp(value_type_name(VALUE_list), "list") == 0);
  char debug[32];
  ASSERT_TRUE(strcmp(value_debug_string(&empty_value, debug, sizeof(debug)),
                     "#[]") == 0);
  char text[16];
  const char *borrowed = NULL;
  size_t length = 0;
  ASSERT_EQ_INT(VALUE_TEXT_UNKNOWN_TYPE,
                value_plain_text(&empty_value, VALUE_TEXT_NIL_LITERAL, text,
                                 sizeof(text), &borrowed, &length));
  VALUE_t one_value = {VALUE_int, {.i = 1}};
  SIN_LIST_t *singleton = sin_list_append(empty, &one_value);
  ASSERT_NOT_NULL(singleton);
  ASSERT_EQ_INT(1, sin_list_count(singleton));
  ASSERT_EQ_INT(1, sin_list_get(singleton, 0)->i);
  sin_list_release(singleton);

  ref = sin_itemref_create("root.child");
  ASSERT_NOT_NULL(ref);
  values[0] = (VALUE_t){VALUE_int, {.i = 7}};
  values[1] = (VALUE_t){VALUE_str, {.s = strdup("hello")}};
  values[2] = (VALUE_t){VALUE_itemref, {.itemref = ref}};
  small = sin_list_build_owned(values, 3);
  ASSERT_NOT_NULL(small);
  ASSERT_EQ_INT(VALUE_nil, values[0].type);
  ASSERT_EQ_INT(VALUE_nil, values[1].type);
  ASSERT_EQ_INT(VALUE_nil, values[2].type);
  ASSERT_EQ_INT(3, sin_list_count(small));
  ASSERT_EQ_INT(1, sin_list_depth(small));
  ASSERT_TRUE(value_is_truthy(&(VALUE_t){VALUE_list, {.list = small}}));
  ASSERT_EQ_INT(7, sin_list_get(small, 0)->i);
  ASSERT_TRUE(strcmp(sin_list_get(small, 1)->s, "hello") == 0);
  ASSERT_TRUE(strcmp(sin_itemref_path(sin_list_get(small, 2)->itemref),
                     "root.child") == 0);
  ASSERT_TRUE(sin_list_get(small, 3) == NULL);

  clone = value_clone(&(VALUE_t){VALUE_list, {.list = small}});
  ASSERT_EQ_INT(VALUE_list, clone.type);
  ASSERT_TRUE(clone.list == small);
  value_free(&empty_value);
  value_move(&moved, &clone);
  ASSERT_EQ_INT(VALUE_nil, clone.type);
  ASSERT_TRUE(value_is_truthy(&moved));
  value_free(&moved);
  sin_list_release(small);
  free(values);
}

void test_list_rendering_contract(void) {
  char *text = NULL;
  size_t length = 0;
  SIN_LIST_t *empty = sin_list_build_owned(NULL, 0);
  VALUE_t empty_value = {VALUE_list, {.list = empty}};
  ASSERT_NOT_NULL(empty);
  ASSERT_EQ_INT(VALUE_TEXT_OK, value_render_text(
      &empty_value, VALUE_TEXT_NIL_LITERAL, &text, &length));
  ASSERT_TRUE(strcmp(text, "#[]") == 0);
  free(text);
  VALUE_t nested = {VALUE_str, {.s = strdup("\"\\\n\t\r\b\f\001\177")}};
  SIN_ITEMREF_t *ref = sin_itemref_create("players.fred");
  VALUE_t inner_elem = {VALUE_int, {.i = 7}};
  SIN_LIST_t *inner = sin_list_build_owned(&inner_elem, 1);
  ASSERT_NOT_NULL(ref);
  ASSERT_NOT_NULL(inner);
  VALUE_t elems[] = {
    nested, VALUE_NIL, VALUE_TRUE,
    (VALUE_t){VALUE_float, {.f = 3.5}},
    (VALUE_t){VALUE_itemref, {.itemref = ref}},
    (VALUE_t){VALUE_list, {.list = sin_list_retain(inner)}}
  };
  SIN_LIST_t *list = sin_list_build_owned(elems, 6);
  VALUE_t value = {VALUE_list, {.list = list}};
  ASSERT_NOT_NULL(list);
  ASSERT_EQ_INT(VALUE_TEXT_OK, value_render_text(
      &value, VALUE_TEXT_NIL_LITERAL, &text, &length));
  ASSERT_TRUE(strcmp(text, "#[\"\\\"\\\\\\n\\t\\r\\b\\f\\x01\\x7F\", nil, true, 3.5, &players.fred, #[7]]") == 0);
  char debug_full[256];
  ASSERT_TRUE(strcmp(value_debug_string(&value, debug_full,
                                        sizeof(debug_full)), text) == 0);
  free(text);
  ASSERT_EQ_INT(VALUE_TEXT_NIL, value_render_text(
      &VALUE_NIL, VALUE_TEXT_NIL_OMIT, &text, &length));
  ASSERT_EQ_INT(VALUE_TEXT_OK, value_render_text(
      &(VALUE_t){VALUE_str, {.s = "plain"}}, VALUE_TEXT_NIL_LITERAL,
      &text, &length));
  ASSERT_TRUE(strcmp(text, "plain") == 0);
  free(text);
  ASSERT_EQ_INT(VALUE_TEXT_MALFORMED, value_render_text(
      &(VALUE_t){VALUE_list, {.list = NULL}}, VALUE_TEXT_NIL_LITERAL,
      &text, &length));
  ASSERT_EQ_INT(VALUE_TEXT_MALFORMED, value_render_text(
      &(VALUE_t){VALUE_itemref, {.itemref = NULL}}, VALUE_TEXT_NIL_LITERAL,
      &text, &length));
  VALUE_t unknown = {(VALUE_e)99, {.i = 0}};
  ASSERT_EQ_INT(VALUE_TEXT_UNKNOWN_TYPE, value_render_text(
      &unknown, VALUE_TEXT_NIL_LITERAL, &text, &length));
  SIN_LIST_t *unknown_list = sin_list_build_owned(&unknown, 1);
  VALUE_t unknown_value = {VALUE_list, {.list = unknown_list}};
  ASSERT_NOT_NULL(unknown_list);
  ASSERT_EQ_INT(VALUE_TEXT_UNKNOWN_TYPE, value_render_text(
      &unknown_value, VALUE_TEXT_NIL_LITERAL, &text, &length));
  sin_list_release(unknown_list);
  char *growth_text = malloc(96);
  ASSERT_NOT_NULL(growth_text);
  memset(growth_text, 'g', 95);
  growth_text[95] = '\0';
  VALUE_t growth_elem = {VALUE_str, {.s = growth_text}};
  SIN_LIST_t *growth_list = sin_list_build_owned(&growth_elem, 1);
  VALUE_t growth_value = {VALUE_list, {.list = growth_list}};
  ASSERT_NOT_NULL(growth_list);
  alloc_test_fail_after(1);
  ASSERT_EQ_INT(VALUE_TEXT_ALLOCATION_ERROR, value_render_text(
      &growth_value, VALUE_TEXT_NIL_LITERAL, &text, &length));
  ASSERT_TRUE(text == NULL);
  alloc_test_fail_after(-1);
  sin_list_release(growth_list);
  VALUE_t depth_value = VALUE_NIL;
  for (size_t depth = 0; depth < SIN_LIST_MAX_DEPTH; depth++) {
    SIN_LIST_t *depth_list = sin_list_build_owned(&depth_value, 1);
    ASSERT_NOT_NULL(depth_list);
    depth_value = (VALUE_t){VALUE_list, {.list = depth_list}};
  }
  ASSERT_EQ_INT(SIN_LIST_MAX_DEPTH, sin_list_depth(depth_value.list));
  ASSERT_EQ_INT(VALUE_TEXT_OK, value_render_text(
      &depth_value, VALUE_TEXT_NIL_LITERAL, &text, &length));
  free(text);
  SIN_LIST_t *depth_seed = sin_list_build_owned(NULL, 0);
  ASSERT_NOT_NULL(depth_seed);
  SIN_LIST_t *too_deep = sin_list_append(depth_seed, &depth_value);
  ASSERT_TRUE(too_deep == NULL);
  sin_list_release(depth_seed);
  value_free(&depth_value);
  char debug[8];
  ASSERT_TRUE(strcmp(value_debug_string(&value, debug, sizeof(debug)),
                     "<trunc>") == 0);
  size_t long_len = SIN_MAX_STRING_BYTES;
  char *long_text = malloc(long_len + 1u);
  ASSERT_NOT_NULL(long_text);
  memset(long_text, '"', long_len);
  long_text[long_len] = '\0';
  VALUE_t long_elem = {VALUE_str, {.s = long_text}};
  SIN_LIST_t *long_list = sin_list_build_owned(&long_elem, 1);
  VALUE_t long_value = {VALUE_list, {.list = long_list}};
  ASSERT_NOT_NULL(long_list);
  ASSERT_EQ_INT(VALUE_TEXT_OUTPUT_LIMIT, value_render_text(
      &long_value, VALUE_TEXT_NIL_LITERAL, &text, &length));
  ASSERT_TRUE(text == NULL);
  sin_list_release(long_list);
  VALUE_t alloc_elem = {VALUE_int, {.i = 1}};
  SIN_LIST_t *alloc_list = sin_list_build_owned(&alloc_elem, 1);
  VALUE_t alloc_value = {VALUE_list, {.list = alloc_list}};
  ASSERT_NOT_NULL(alloc_list);
  alloc_test_fail_after(0);
  ASSERT_EQ_INT(VALUE_TEXT_ALLOCATION_ERROR, value_render_text(
      &alloc_value, VALUE_TEXT_NIL_LITERAL, &text, &length));
  ASSERT_TRUE(text == NULL);
  alloc_test_fail_after(-1);
  sin_list_release(alloc_list);
  sin_list_release(inner);
  sin_list_release(empty);
  sin_list_release(list);
}

void test_list_boundaries_persistence_and_equality(void) {
  SIN_LIST_t *list31 = make_int_list(31);
  SIN_LIST_t *list32 = make_int_list(32);
  SIN_LIST_t *list33 = make_int_list(33);
  SIN_LIST_t *large = make_int_list(1024);
  SIN_LIST_t *equal_left = make_int_list(65);
  SIN_LIST_t *equal_right = make_int_list(65);
  SIN_LIST_t *appended;
  SIN_LIST_t *set;
  SIN_LIST_t *nested_set;
  SIN_LIST_t *height2;
  SIN_LIST_t *concat_left;
  SIN_LIST_t *concat_right;
  SIN_LIST_t *concatenated;
  bool saw_concat_failure = false;
  bool saw_concat_success = false;
  VALUE_t value = {VALUE_int, {.i = 9999}};
  ASSERT_NOT_NULL(list31);
  ASSERT_NOT_NULL(list32);
  ASSERT_NOT_NULL(list33);
  ASSERT_NOT_NULL(large);
  ASSERT_NOT_NULL(equal_left);
  ASSERT_NOT_NULL(equal_right);
  ASSERT_EQ_INT(31, sin_list_count(list31));
  ASSERT_EQ_INT(32, sin_list_count(list32));
  ASSERT_EQ_INT(33, sin_list_count(list33));
  ASSERT_EQ_INT(0, sin_list_get(list33, 0)->i);
  ASSERT_EQ_INT(32, sin_list_get(list33, 32)->i);
  appended = sin_list_append(list33, &value);
  ASSERT_NOT_NULL(appended);
  ASSERT_EQ_INT(34, sin_list_count(appended));
  ASSERT_EQ_INT(32, sin_list_get(list33, 32)->i);
  ASSERT_EQ_INT(9999, sin_list_get(appended, 33)->i);
  set = sin_list_set(appended, 0, &value);
  ASSERT_NOT_NULL(set);
  ASSERT_EQ_INT(0, sin_list_get(appended, 0)->i);
  ASSERT_EQ_INT(9999, sin_list_get(set, 0)->i);
  ASSERT_TRUE(sin_list_equal(appended, appended));
  ASSERT_TRUE(!sin_list_equal(appended, set));
  ASSERT_TRUE(sin_list_equal(equal_left, equal_right));
  ASSERT_TRUE(!sin_list_equal(NULL, NULL));
  VALUE_t nested_value = {VALUE_list, {.list = sin_list_retain(list31)}};
  nested_set = sin_list_set(equal_left, 0, &nested_value);
  ASSERT_NOT_NULL(nested_set);
  ASSERT_TRUE(!sin_list_equal(nested_set, equal_right));
  value_free(&nested_value);
  ASSERT_TRUE(sin_list_set(appended, 34, &value) == NULL);

  sin_list_release(appended);
  appended = sin_list_append(large, &value);
  ASSERT_NOT_NULL(appended);
  height2 = sin_list_retain(appended);
  ASSERT_NOT_NULL(height2);
  for (size_t i = 0; i < 32; ++i) {
    SIN_LIST_t *next = sin_list_append(height2, &value);
    ASSERT_NOT_NULL(next);
    sin_list_release(height2);
    height2 = next;
  }
  ASSERT_EQ_INT(1057, sin_list_count(height2));
  ASSERT_EQ_INT(0, sin_list_get(height2, 0)->i);
  ASSERT_EQ_INT(9999, sin_list_get(height2, 1024)->i);
  ASSERT_EQ_INT(9999, sin_list_get(height2, 1056)->i);
  ASSERT_TRUE(sin_list_equal(height2, height2));

  concat_left = make_int_list(20);
  concat_right = make_int_list(1050);
  ASSERT_NOT_NULL(concat_left);
  ASSERT_NOT_NULL(concat_right);
  concatenated = sin_list_concat(concat_left, concat_right);
  ASSERT_NOT_NULL(concatenated);
  ASSERT_EQ_INT(1070, sin_list_count(concatenated));
  ASSERT_EQ_INT(0, sin_list_get(concatenated, 0)->i);
  ASSERT_EQ_INT(11, sin_list_get(concatenated, 31)->i);
  ASSERT_EQ_INT(12, sin_list_get(concatenated, 32)->i);
  ASSERT_EQ_INT(1035, sin_list_get(concatenated, 1055)->i);
  ASSERT_EQ_INT(1036, sin_list_get(concatenated, 1056)->i);
  ASSERT_EQ_INT(0, sin_list_get(concatenated, 20)->i);
  ASSERT_EQ_INT(1049, sin_list_get(concatenated, 1069)->i);
  sin_list_release(concatenated);
  concatenated = sin_list_concat(concat_left, concat_left);
  ASSERT_NOT_NULL(concatenated);
  ASSERT_EQ_INT(40, sin_list_count(concatenated));
  ASSERT_EQ_INT(19, sin_list_get(concatenated, 19)->i);
  ASSERT_EQ_INT(0, sin_list_get(concatenated, 20)->i);
  sin_list_release(concatenated);
  /* 128 attempts reach allocations in the later full-tail promotions. */
  for (long fail_at = 0; fail_at < 128; ++fail_at) {
    alloc_test_fail_after(fail_at);
    concatenated = sin_list_concat(concat_left, concat_right);
    alloc_test_fail_after(-1);
    if (concatenated) {
      saw_concat_success = true;
      sin_list_release(concatenated);
    } else {
      saw_concat_failure = true;
    }
    ASSERT_EQ_INT(20, sin_list_count(concat_left));
    ASSERT_EQ_INT(1050, sin_list_count(concat_right));
    ASSERT_EQ_INT(0, sin_list_get(concat_left, 0)->i);
    ASSERT_EQ_INT(1049, sin_list_get(concat_right, 1049)->i);
  }
  ASSERT_TRUE(saw_concat_failure);
  ASSERT_TRUE(saw_concat_success);
  sin_list_release(concat_left);
  sin_list_release(concat_right);

  sin_list_release(list31);
  sin_list_release(list32);
  sin_list_release(list33);
  sin_list_release(large);
  sin_list_release(equal_left);
  sin_list_release(equal_right);
  sin_list_release(nested_set);
  sin_list_release(appended);
  sin_list_release(set);
  sin_list_release(height2);
}

void test_list_limits_invalid_inputs_and_failures(void) {
  SIN_LIST_t *nested = sin_list_build_owned(NULL, 0);
  VALUE_t element;
  SIN_LIST_t *next;
  VALUE_t *values;
  bool saw_build_failure = false;
  bool saw_update_failure = false;
  SIN_LIST_t *base = make_int_list(64);
  VALUE_t replacement = {VALUE_int, {.i = 42}};
  ASSERT_NOT_NULL(nested);
  for (size_t depth = 2; depth <= SIN_LIST_MAX_DEPTH; ++depth) {
    element = (VALUE_t){VALUE_list, {.list = sin_list_retain(nested)}};
    next = sin_list_build_owned(&element, 1);
    ASSERT_NOT_NULL(next);
    ASSERT_EQ_INT(depth, sin_list_depth(next));
    ASSERT_EQ_INT(VALUE_nil, element.type);
    sin_list_release(nested);
    nested = next;
  }
  element = (VALUE_t){VALUE_list, {.list = sin_list_retain(nested)}};
  ASSERT_TRUE(sin_list_build_owned(&element, 1) == NULL);
  ASSERT_EQ_INT(VALUE_nil, element.type);
  ASSERT_TRUE(sin_list_build_owned(NULL, 1) == NULL);
  values = calloc(SIN_LIST_MAX_ELEMENTS + 1u, sizeof(*values));
  ASSERT_NOT_NULL(values);
  values[0] = (VALUE_t){VALUE_int, {.i = 1}};
  ASSERT_TRUE(sin_list_build_owned(values, SIN_LIST_MAX_ELEMENTS + 1u) == NULL);
  ASSERT_EQ_INT(VALUE_int, values[0].type);
  value_free(&values[0]);
  free(values);

  for (long fail_at = 0; fail_at < 16; ++fail_at) {
    values = calloc(65, sizeof(*values));
    ASSERT_NOT_NULL(values);
    for (size_t i = 0; i < 65; ++i) values[i] = (VALUE_t){VALUE_int, {.i = (int64_t)i}};
    alloc_test_fail_after(fail_at);
    next = sin_list_build_owned(values, 65);
    alloc_test_fail_after(-1);
    if (!next) saw_build_failure = true;
    else sin_list_release(next);
    for (size_t i = 0; i < 65; ++i) ASSERT_EQ_INT(VALUE_nil, values[i].type);
    free(values);
  }
  for (long fail_at = 0; fail_at < 16; ++fail_at) {
    alloc_test_fail_after(fail_at);
    next = sin_list_append(base, &replacement);
    alloc_test_fail_after(-1);
    if (!next) saw_update_failure = true;
    else {
      sin_list_release(next);
    }
    if (!next) {
      ASSERT_EQ_INT(64, sin_list_count(base));
      ASSERT_EQ_INT(0, sin_list_get(base, 0)->i);
      ASSERT_EQ_INT(63, sin_list_get(base, 63)->i);
    }
  }
  for (long fail_at = 0; fail_at < 16; ++fail_at) {
    alloc_test_fail_after(fail_at);
    next = sin_list_set(base, 10, &replacement);
    alloc_test_fail_after(-1);
    if (!next) saw_update_failure = true;
    else {
      sin_list_release(next);
    }
    if (!next) {
      ASSERT_EQ_INT(64, sin_list_count(base));
      ASSERT_EQ_INT(0, sin_list_get(base, 0)->i);
      ASSERT_EQ_INT(63, sin_list_get(base, 63)->i);
      ASSERT_EQ_INT(10, sin_list_get(base, 10)->i);
    }
  }
  ASSERT_TRUE(saw_build_failure);
  ASSERT_TRUE(saw_update_failure);
  sin_list_release(base);
  sin_list_release(nested);
}
