#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "list.h"
#include "list_internal.h"
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

static SIN_LIST_t *make_string_list(size_t count) {
  VALUE_t *values = count == 0 ? NULL : calloc(count, sizeof(*values));
  SIN_LIST_t *list;
  if (count != 0 && !values) return NULL;
  for (size_t i = 0; i < count; ++i) {
    char text[32];
    (void)snprintf(text, sizeof(text), "rhs-string-%zu", i);
    values[i] = (VALUE_t){VALUE_str, {.s = strdup(text)}};
    if (!values[i].s) {
      for (size_t j = 0; j < i; ++j) value_free(&values[j]);
      free(values);
      return NULL;
    }
  }
  list = sin_list_build_owned(values, count);
  free(values);
  return list;
}

static void assert_list_iterator_order(size_t count) {
  SIN_LIST_t *list = make_int_list(count);
  SIN_LIST_ITER_t iter;
  const VALUE_t *values = NULL;
  const SIN_LIST_NODE *leaf = NULL;
  size_t span_count = 0;
  size_t seen = 0;
  size_t full_leaves = count <= 32u ? 0u : (count - 1u) / 32u;
  size_t expected_nodes = 0;
  size_t level = full_leaves;
  while (level > 1u) {
    level = (level + 31u) / 32u;
    expected_nodes += level;
  }
  ASSERT_NOT_NULL(list);
  sin_list_test_reset_traversal_stats();
  ASSERT_TRUE(sin_list_iter_init(&iter, list));
  alloc_test_fail_after(0);
  while (sin_list_iter_next(&iter, &values, &span_count, &leaf)) {
    ASSERT_TRUE(values != NULL);
    ASSERT_TRUE(leaf != NULL);
    for (size_t i = 0; i < span_count; ++i) {
      ASSERT_EQ_INT((long long)seen, values[i].i);
      ++seen;
    }
  }
  alloc_test_fail_after(-1);
  ASSERT_EQ_INT((long long)count, seen);
  ASSERT_EQ_INT((long long)((count + 31u) / 32u),
                sin_list_test_traversal_stats().leaf_visits);
  ASSERT_EQ_INT((long long)expected_nodes,
                sin_list_test_traversal_stats().node_visits);
  ASSERT_EQ_INT((long long)count,
                sin_list_test_traversal_stats().values_yielded);
  sin_list_release(list);
}

static void assert_rhs_root_leaves_shared(const SIN_LIST_t *result,
                                          const SIN_LIST_t *right) {
  SIN_LIST_ITER_t right_iter;
  SIN_LIST_ITER_t result_iter;
  const VALUE_t *right_values = NULL;
  const VALUE_t *result_values = NULL;
  const SIN_LIST_NODE *right_leaf = NULL;
  const SIN_LIST_NODE *result_leaf = NULL;
  size_t right_count = 0;
  size_t result_count = 0;
  size_t right_root_count = sin_list_count(right);
  size_t right_tail_count = right_root_count <= 32u
      ? right_root_count
      : ((right_root_count - 1u) % 32u) + 1u;
  size_t span_count = 0;
  if (right_root_count == 0) return;
  right_root_count -= right_tail_count;
  ASSERT_TRUE(sin_list_iter_init(&right_iter, right));
  ASSERT_TRUE(sin_list_iter_init(&result_iter, result));
  while (right_count < right_root_count) {
    ASSERT_TRUE(sin_list_iter_next(&right_iter, &right_values, &span_count,
                                   &right_leaf));
    ASSERT_TRUE(span_count <= right_root_count - right_count);
    ASSERT_TRUE(sin_list_iter_next(&result_iter, &result_values, &result_count,
                                   &result_leaf));
    while (result_leaf != right_leaf) {
      ASSERT_TRUE(sin_list_iter_next(&result_iter, &result_values, &result_count,
                                     &result_leaf));
    }
    ASSERT_TRUE(result_leaf == right_leaf);
    right_count += span_count;
  }
}

void test_list_leaf_iterator_boundaries_and_observability(void) {
  const size_t sizes[] = {0u, 1u, 31u, 32u, 33u, 1024u, 1056u,
                          SIN_LIST_MAX_ELEMENTS};
  for (size_t i = 0; i < sizeof(sizes) / sizeof(sizes[0]); ++i) {
    assert_list_iterator_order(sizes[i]);
  }
}

void test_list_equality_iterator_fast_paths_and_early_exit(void) {
  SIN_LIST_t *base = make_int_list(1056);
  SIN_LIST_t *first;
  SIN_LIST_t *middle;
  SIN_LIST_t *last;
  SIN_LIST_t *nested_left;
  SIN_LIST_t *nested_right;
  SIN_LIST_t *inner_left;
  SIN_LIST_t *inner_right;
  VALUE_t replacement = {VALUE_int, {.i = -1}};
  VALUE_t nested_left_value;
  VALUE_t nested_right_value;
  ASSERT_NOT_NULL(base);

  sin_list_test_reset_traversal_stats();
  ASSERT_TRUE(sin_list_equal(base, base));
  ASSERT_EQ_INT(0, sin_list_test_traversal_stats().leaf_visits);

  first = sin_list_set(base, 0, &replacement);
  middle = sin_list_set(base, 512, &replacement);
  last = sin_list_set(base, 1055, &replacement);
  ASSERT_NOT_NULL(first);
  ASSERT_NOT_NULL(middle);
  ASSERT_NOT_NULL(last);
  sin_list_test_reset_traversal_stats();
  ASSERT_TRUE(!sin_list_equal(base, first));
  ASSERT_EQ_INT(1, sin_list_test_traversal_stats().value_comparisons);
  ASSERT_EQ_INT(0, sin_list_test_traversal_stats().shared_leaf_skips);
  sin_list_test_reset_traversal_stats();
  ASSERT_TRUE(!sin_list_equal(base, middle));
  ASSERT_EQ_INT(1, sin_list_test_traversal_stats().value_comparisons);
  ASSERT_EQ_INT(16, sin_list_test_traversal_stats().shared_leaf_skips);
  sin_list_test_reset_traversal_stats();
  ASSERT_TRUE(!sin_list_equal(base, last));
  ASSERT_EQ_INT(32, sin_list_test_traversal_stats().value_comparisons);
  ASSERT_EQ_INT(32, sin_list_test_traversal_stats().shared_leaf_skips);

  inner_left = make_int_list(33);
  inner_right = make_int_list(33);
  ASSERT_NOT_NULL(inner_left);
  ASSERT_NOT_NULL(inner_right);
  nested_left_value = (VALUE_t){VALUE_list, {.list = inner_left}};
  nested_right_value = (VALUE_t){VALUE_list, {.list = inner_right}};
  nested_left = sin_list_build_owned(&nested_left_value, 1);
  nested_right = sin_list_build_owned(&nested_right_value, 1);
  ASSERT_NOT_NULL(nested_left);
  ASSERT_NOT_NULL(nested_right);
  ASSERT_TRUE(sin_list_equal(nested_left, nested_right));
  sin_list_release(nested_left);
  sin_list_release(nested_right);
  sin_list_release(first);
  sin_list_release(middle);
  sin_list_release(last);
  sin_list_release(base);
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

void test_list_concat_shares_rhs_leaves(void) {
  const size_t left_sizes[] = {31u, 32u, 33u, 1023u, 1024u, 1025u};
  const size_t right_sizes[] = {31u, 32u, 33u, 1023u, 1024u, 1025u};
  for (size_t left_index = 0;
       left_index < sizeof(left_sizes) / sizeof(left_sizes[0]); ++left_index) {
    for (size_t right_index = 0;
         right_index < sizeof(right_sizes) / sizeof(right_sizes[0]);
         ++right_index) {
      SIN_LIST_t *left = make_int_list(left_sizes[left_index]);
      SIN_LIST_t *right = make_int_list(right_sizes[right_index]);
      SIN_LIST_t *result;
      bool observe_cursor = left_sizes[left_index] == 31u &&
                            right_sizes[right_index] == 1025u;
      ASSERT_NOT_NULL(left);
      ASSERT_NOT_NULL(right);
      if (observe_cursor) sin_list_test_reset_traversal_stats();
      result = sin_list_concat(left, right);
      ASSERT_NOT_NULL(result);
      ASSERT_EQ_INT((long long)(left_sizes[left_index] + right_sizes[right_index]),
                    sin_list_count(result));
      for (size_t i = 0; i < left_sizes[left_index]; ++i)
        ASSERT_EQ_INT((long long)i, sin_list_get(result, i)->i);
      for (size_t i = 0; i < right_sizes[right_index]; ++i)
        ASSERT_EQ_INT((long long)i,
                      sin_list_get(result, left_sizes[left_index] + i)->i);
      if (left_sizes[left_index] % 32u == 0u)
        assert_rhs_root_leaves_shared(result, right);
      if (observe_cursor) {
        SIN_LIST_TRAVERSAL_STATS_t stats = sin_list_test_traversal_stats();
        ASSERT_EQ_INT(33, stats.leaf_visits);
        ASSERT_EQ_INT(1025, stats.values_yielded);
        ASSERT_EQ_INT(1, stats.node_visits);
      }
      if ((left_index + right_index) % 2u == 0u) {
        sin_list_release(result);
        sin_list_release(right);
      } else {
        sin_list_release(right);
        sin_list_release(result);
      }
      sin_list_release(left);
    }
  }

  SIN_LIST_t *acc = make_int_list(32);
  SIN_LIST_t *piece = make_int_list(32);
  ASSERT_NOT_NULL(acc);
  ASSERT_NOT_NULL(piece);
  for (size_t i = 0; i < 256u; ++i) {
    SIN_LIST_t *next = sin_list_concat(acc, piece);
    ASSERT_NOT_NULL(next);
    ASSERT_TRUE(sin_list_depth(next) <= SIN_LIST_MAX_DEPTH);
    ASSERT_EQ_INT((long long)((i + 2u) * 32u), sin_list_count(next));
    ASSERT_EQ_INT(0, sin_list_get(next, 0)->i);
    ASSERT_EQ_INT(31, sin_list_get(next, sin_list_count(next) - 1u)->i);
    SIN_LIST_ITER_t iter;
    const VALUE_t *values = NULL;
    const SIN_LIST_NODE *leaf = NULL;
    size_t span = 0;
    size_t seen = 0;
    ASSERT_TRUE(sin_list_iter_init(&iter, next));
    while (sin_list_iter_next(&iter, &values, &span, &leaf)) seen += span;
    ASSERT_EQ_INT((long long)((i + 2u) * 32u), seen);
    sin_list_release(acc);
    acc = next;
  }
  sin_list_release(acc);
  sin_list_release(piece);

  SIN_LIST_t *self_base = make_int_list(1024);
  SIN_LIST_t *self_result;
  ASSERT_NOT_NULL(self_base);
  self_result = sin_list_concat(self_base, self_base);
  ASSERT_NOT_NULL(self_result);
  ASSERT_EQ_INT(2048, sin_list_count(self_result));
  ASSERT_EQ_INT(0, sin_list_get(self_result, 0)->i);
  ASSERT_EQ_INT(1023, sin_list_get(self_result, 1024u - 1u)->i);
  ASSERT_EQ_INT(0, sin_list_get(self_result, 1024u)->i);
  assert_rhs_root_leaves_shared(self_result, self_base);
  sin_list_release(self_base);
  ASSERT_EQ_INT(1023, sin_list_get(self_result, 1024u - 1u)->i);
  sin_list_release(self_result);

  const size_t failure_left[] = {31u, 1024u};
  const size_t failure_right[] = {1025u, 1024u};
  for (size_t shape = 0; shape < 2u; ++shape) {
    SIN_LIST_t *left = make_int_list(failure_left[shape]);
    SIN_LIST_t *right = make_int_list(failure_right[shape]);
    bool saw_failure = false;
    bool saw_success_after_failure = false;
    ASSERT_NOT_NULL(left);
    ASSERT_NOT_NULL(right);
    for (long fail_at = 0; fail_at < 512; ++fail_at) {
      SIN_LIST_t *result;
      alloc_test_fail_after(fail_at);
      result = sin_list_concat(left, right);
      alloc_test_fail_after(-1);
      if (result) {
        if (saw_failure) saw_success_after_failure = true;
        sin_list_release(result);
      } else {
        saw_failure = true;
      }
      ASSERT_EQ_INT((long long)failure_left[shape], sin_list_count(left));
      ASSERT_EQ_INT((long long)failure_right[shape], sin_list_count(right));
      ASSERT_EQ_INT(0, sin_list_get(left, 0)->i);
      ASSERT_EQ_INT((long long)(failure_left[shape] - 1u),
                    sin_list_get(left, failure_left[shape] - 1u)->i);
      ASSERT_EQ_INT(0, sin_list_get(right, 0)->i);
      ASSERT_EQ_INT((long long)(failure_right[shape] - 1u),
                    sin_list_get(right, failure_right[shape] - 1u)->i);
    }
    ASSERT_TRUE(saw_failure);
    ASSERT_TRUE(saw_success_after_failure);
    sin_list_release(left);
    sin_list_release(right);
  }

  SIN_LIST_t *string_inner = make_int_list(2);
  SIN_LIST_t *string_source = make_string_list(65);
  VALUE_t nested_string_value = {
    VALUE_list, {.list = sin_list_retain(string_inner)}
  };
  SIN_LIST_t *string_right = sin_list_set(string_source, 64,
                                           &nested_string_value);
  SIN_LIST_t *string_left = make_int_list(31);
  bool string_saw_failure = false;
  bool string_saw_success_after_failure = false;
  ASSERT_NOT_NULL(string_inner);
  ASSERT_NOT_NULL(string_source);
  ASSERT_NOT_NULL(string_right);
  ASSERT_NOT_NULL(string_left);
  value_free(&nested_string_value);
  sin_list_release(string_source);
  for (long fail_at = 0; fail_at < 512; ++fail_at) {
    SIN_LIST_t *result;
    alloc_test_fail_after(fail_at);
    result = sin_list_concat(string_left, string_right);
    alloc_test_fail_after(-1);
    if (result) {
      if (string_saw_failure) string_saw_success_after_failure = true;
      if (fail_at % 2 == 0) {
        sin_list_release(string_left);
        sin_list_release(result);
      } else {
        sin_list_release(result);
        sin_list_release(string_left);
      }
      string_left = make_int_list(31);
      ASSERT_NOT_NULL(string_left);
    } else {
      string_saw_failure = true;
    }
    ASSERT_EQ_INT(31, sin_list_count(string_left));
    ASSERT_EQ_INT(65, sin_list_count(string_right));
    for (size_t i = 0; i < 64u; ++i) {
      char expected[32];
      (void)snprintf(expected, sizeof(expected), "rhs-string-%zu", i);
      ASSERT_EQ_INT(VALUE_str, sin_list_get(string_right, i)->type);
      ASSERT_TRUE(strcmp(sin_list_get(string_right, i)->s, expected) == 0);
    }
    ASSERT_EQ_INT(VALUE_list, sin_list_get(string_right, 64)->type);
    ASSERT_TRUE(sin_list_get(string_right, 64)->list == string_inner);
  }
  ASSERT_TRUE(string_saw_failure);
  ASSERT_TRUE(string_saw_success_after_failure);
  sin_list_release(string_left);
  sin_list_release(string_right);
  sin_list_release(string_inner);

  SIN_LIST_t *inner = make_int_list(2);
  VALUE_t left_values[2] = {
    {VALUE_str, {.s = strdup("left")}},
    {VALUE_list, {.list = sin_list_retain(inner)}}
  };
  VALUE_t right_values[2] = {
    {VALUE_str, {.s = strdup("right")}},
    {VALUE_list, {.list = sin_list_retain(inner)}}
  };
  SIN_LIST_t *nested_left = sin_list_build_owned(left_values, 2);
  SIN_LIST_t *nested_right = sin_list_build_owned(right_values, 2);
  SIN_LIST_t *nested_result;
  ASSERT_NOT_NULL(inner);
  ASSERT_NOT_NULL(nested_left);
  ASSERT_NOT_NULL(nested_right);
  nested_result = sin_list_concat(nested_left, nested_right);
  ASSERT_NOT_NULL(nested_result);
  sin_list_release(nested_left);
  sin_list_release(nested_right);
  ASSERT_EQ_INT(VALUE_str, sin_list_get(nested_result, 0)->type);
  ASSERT_TRUE(strcmp(sin_list_get(nested_result, 0)->s, "left") == 0);
  ASSERT_EQ_INT(VALUE_str, sin_list_get(nested_result, 2)->type);
  ASSERT_TRUE(strcmp(sin_list_get(nested_result, 2)->s, "right") == 0);
  ASSERT_TRUE(sin_list_get(nested_result, 1)->list == inner);
  ASSERT_TRUE(sin_list_get(nested_result, 3)->list == inner);
  sin_list_release(nested_result);
  sin_list_release(inner);
}

void test_list_slice_shares_aligned_leaves_and_boundaries(void) {
  const size_t starts[] = {0u, 1u, 31u, 32u, 33u};
  const size_t lengths[] = {0u, 1u, 31u, 32u, 33u};
  SIN_LIST_t *source = make_int_list(1056);
  SIN_LIST_t *empty = sin_list_build_owned(NULL, 0);
  SIN_LIST_t *full;
  SIN_LIST_t *aligned;
  SIN_LIST_t *short_source;
  SIN_LIST_t *short_slice;
  SIN_LIST_t *subtree_source;
  SIN_LIST_t *subtree_slice;
  SIN_LIST_t *nested_source;
  SIN_LIST_t *unaligned_nested;
  SIN_LIST_t *nested_inner = make_int_list(2);
  VALUE_t nested_value;
  bool saw_failure = false;
  bool saw_success_after_failure = false;
  bool saw_fragment_failure = false;
  bool saw_fragment_success_after_failure = false;
  ASSERT_NOT_NULL(source);
  ASSERT_NOT_NULL(empty);
  ASSERT_NOT_NULL(nested_inner);

  full = sin_list_slice(source, 0, sin_list_count(source));
  ASSERT_TRUE(full == source);
  sin_list_release(full);
  alloc_test_fail_after(0);
  full = sin_list_slice(source, 0, sin_list_count(source));
  alloc_test_fail_after(-1);
  ASSERT_TRUE(full == source);
  sin_list_release(full);
  full = sin_list_slice(empty, 0, 0);
  ASSERT_TRUE(full == empty);
  sin_list_release(full);
  for (size_t i = 0; i < sizeof(starts) / sizeof(starts[0]); ++i) {
    for (size_t j = 0; j < sizeof(lengths) / sizeof(lengths[0]); ++j) {
      size_t start = starts[i];
      size_t length = lengths[j];
      SIN_LIST_t *slice;
      if (start + length > sin_list_count(source)) continue;
      slice = sin_list_slice(source, start, length);
      ASSERT_NOT_NULL(slice);
      ASSERT_EQ_INT((long long)length, sin_list_count(slice));
      for (size_t k = 0; k < length; ++k)
        ASSERT_EQ_INT((long long)(start + k), sin_list_get(slice, k)->i);
      sin_list_release(slice);
    }
  }

  aligned = sin_list_slice(source, 32u, 992u);
  ASSERT_NOT_NULL(aligned);
  {
    SIN_LIST_ITER_t source_iter;
    SIN_LIST_ITER_t slice_iter;
    const VALUE_t *source_values = NULL;
    const VALUE_t *slice_values = NULL;
    const SIN_LIST_NODE *source_leaf = NULL;
    const SIN_LIST_NODE *slice_leaf = NULL;
    size_t source_span = 0;
    size_t slice_span = 0;
    ASSERT_TRUE(sin_list_iter_init(&source_iter, source));
    ASSERT_TRUE(sin_list_iter_init(&slice_iter, aligned));
    ASSERT_TRUE(sin_list_iter_next(&source_iter, &source_values,
                                   &source_span, &source_leaf));
    ASSERT_EQ_INT(32, source_span);
    while (sin_list_iter_next(&slice_iter, &slice_values, &slice_span,
                              &slice_leaf)) {
      ASSERT_TRUE(sin_list_iter_next(&source_iter, &source_values,
                                     &source_span, &source_leaf));
      ASSERT_TRUE(slice_leaf == source_leaf);
      ASSERT_EQ_INT((long long)source_span, slice_span);
    }
  }
  for (size_t i = 0; i < 992u; ++i)
    ASSERT_EQ_INT((long long)(32u + i), sin_list_get(aligned, i)->i);
  sin_list_release(aligned);

  sin_list_test_reset_traversal_stats();
  aligned = sin_list_slice(source, 31u, 992u);
  ASSERT_NOT_NULL(aligned);
  ASSERT_EQ_INT(31, sin_list_get(aligned, 0)->i);
  ASSERT_EQ_INT(1022, sin_list_get(aligned, 991u)->i);
  ASSERT_EQ_INT(32, sin_list_test_traversal_stats().leaf_visits);
  ASSERT_EQ_INT(1024, sin_list_test_traversal_stats().values_yielded);
  ASSERT_EQ_INT(1, sin_list_test_traversal_stats().node_visits);
  sin_list_release(aligned);
  sin_list_test_reset_traversal_stats();

  nested_value = (VALUE_t){VALUE_list, {.list = sin_list_retain(nested_inner)}};
  nested_source = sin_list_set(source, 64u, &nested_value);
  value_free(&nested_value);
  ASSERT_NOT_NULL(nested_source);
  unaligned_nested = sin_list_slice(nested_source, 63u, 4u);
  ASSERT_NOT_NULL(unaligned_nested);
  ASSERT_TRUE(sin_list_get(unaligned_nested, 1u)->list == nested_inner);
  aligned = sin_list_slice(nested_source, 32u, 64u);
  ASSERT_NOT_NULL(aligned);
  ASSERT_TRUE(sin_list_get(aligned, 32u)->list == nested_inner);
  sin_list_release(nested_source);
  ASSERT_TRUE(sin_list_get(unaligned_nested, 1u)->list == nested_inner);
  ASSERT_TRUE(sin_list_get(aligned, 32u)->list == nested_inner);
  sin_list_release(unaligned_nested);
  sin_list_release(aligned);
  sin_list_release(nested_inner);

  short_source = make_int_list(65);
  ASSERT_NOT_NULL(short_source);
  short_slice = sin_list_slice(short_source, 64u, 1u);
  ASSERT_NOT_NULL(short_slice);
  {
    SIN_LIST_ITER_t source_iter;
    SIN_LIST_ITER_t slice_iter;
    const VALUE_t *values = NULL;
    const SIN_LIST_NODE *source_leaf = NULL;
    const SIN_LIST_NODE *slice_leaf = NULL;
    size_t span = 0;
    ASSERT_TRUE(sin_list_iter_init(&source_iter, short_source));
    ASSERT_TRUE(sin_list_iter_init(&slice_iter, short_slice));
    ASSERT_TRUE(sin_list_iter_next(&source_iter, &values, &span,
                                   &source_leaf));
    ASSERT_EQ_INT(32, span);
    ASSERT_TRUE(sin_list_iter_next(&source_iter, &values, &span,
                                   &source_leaf));
    ASSERT_EQ_INT(32, span);
    ASSERT_TRUE(sin_list_iter_next(&source_iter, &values, &span,
                                   &source_leaf));
    ASSERT_EQ_INT(1, span);
    ASSERT_TRUE(sin_list_iter_next(&slice_iter, &values, &span,
                                   &slice_leaf));
    ASSERT_TRUE(source_leaf == slice_leaf);
  }
  sin_list_release(short_source);
  ASSERT_EQ_INT(64, sin_list_get(short_slice, 0)->i);
  sin_list_release(short_slice);

  subtree_source = make_int_list(2080);
  ASSERT_NOT_NULL(subtree_source);
  subtree_slice = sin_list_slice(subtree_source, 1024u, 1056u);
  ASSERT_NOT_NULL(subtree_slice);
  ASSERT_TRUE(sin_list_test_root_shares_source_range(
      subtree_slice, subtree_source, 1024u, 1024u));
  ASSERT_EQ_INT(1024, sin_list_get(subtree_slice, 0)->i);
  ASSERT_EQ_INT(2079, sin_list_get(subtree_slice, 1055u)->i);
  sin_list_release(subtree_source);
  ASSERT_EQ_INT(1024, sin_list_get(subtree_slice, 0)->i);
  ASSERT_EQ_INT(2079, sin_list_get(subtree_slice, 1055u)->i);
  sin_list_release(subtree_slice);
  subtree_source = make_int_list(2080);
  ASSERT_NOT_NULL(subtree_source);
  {
    bool saw_exact_failure = false;
    bool saw_exact_success_after_failure = false;
    for (long fail_at = 0; fail_at < 32; ++fail_at) {
      SIN_LIST_t *slice;
      alloc_test_fail_after(fail_at);
      slice = sin_list_slice(subtree_source, 1024u, 1056u);
      alloc_test_fail_after(-1);
      if (slice) {
        if (saw_exact_failure) saw_exact_success_after_failure = true;
        sin_list_release(slice);
      } else {
        saw_exact_failure = true;
      }
    }
    ASSERT_TRUE(saw_exact_failure);
    ASSERT_TRUE(saw_exact_success_after_failure);
  }
  sin_list_release(subtree_source);

  SIN_LIST_t *height_source = make_int_list(4096);
  SIN_LIST_t *height_slice;
  SIN_LIST_t *height_copy;
  SIN_LIST_t *height_appended;
  SIN_LIST_t *height_set;
  SIN_LIST_t *height_piece = make_int_list(1);
  SIN_LIST_t *height_concat;
  VALUE_t height_replacement = {VALUE_int, {.i = -7}};
  bool saw_height_failure = false;
  bool saw_height_success_after_failure = false;
  ASSERT_NOT_NULL(height_source);
  ASSERT_NOT_NULL(height_piece);
  height_slice = sin_list_slice(height_source, 32u, 1088u);
  ASSERT_NOT_NULL(height_slice);
  ASSERT_EQ_INT(1088, sin_list_count(height_slice));
  for (size_t i = 0; i < 1088u; ++i)
    ASSERT_EQ_INT((long long)(32u + i), sin_list_get(height_slice, i)->i);
  height_copy = sin_list_slice(height_source, 32u, 1088u);
  ASSERT_NOT_NULL(height_copy);
  ASSERT_TRUE(sin_list_equal(height_slice, height_copy));
  sin_list_release(height_copy);
  height_appended = sin_list_append(height_slice, &height_replacement);
  ASSERT_NOT_NULL(height_appended);
  ASSERT_EQ_INT(1089, sin_list_count(height_appended));
  ASSERT_EQ_INT(-7, sin_list_get(height_appended, 1088u)->i);
  height_set = sin_list_set(height_slice, 100u, &height_replacement);
  ASSERT_NOT_NULL(height_set);
  ASSERT_EQ_INT(-7, sin_list_get(height_set, 100u)->i);
  height_concat = sin_list_concat(height_slice, height_piece);
  ASSERT_NOT_NULL(height_concat);
  ASSERT_EQ_INT(1089, sin_list_count(height_concat));
  ASSERT_EQ_INT(0, sin_list_get(height_concat, 1088u)->i);
  {
    SIN_LIST_ITER_t iter;
    const VALUE_t *values = NULL;
    const SIN_LIST_NODE *leaf = NULL;
    size_t span = 0;
    size_t seen = 0;
    ASSERT_TRUE(sin_list_iter_init(&iter, height_slice));
    while (sin_list_iter_next(&iter, &values, &span, &leaf)) seen += span;
    ASSERT_EQ_INT(1088, seen);
  }
  for (long fail_at = 0; fail_at < 256; ++fail_at) {
    SIN_LIST_t *slice;
    alloc_test_fail_after(fail_at);
    slice = sin_list_slice(height_source, 32u, 1088u);
    alloc_test_fail_after(-1);
    if (slice) {
      if (saw_height_failure) saw_height_success_after_failure = true;
      sin_list_release(slice);
    } else {
      saw_height_failure = true;
    }
  }
  ASSERT_TRUE(saw_height_failure);
  ASSERT_TRUE(saw_height_success_after_failure);
  sin_list_release(height_concat);
  sin_list_release(height_set);
  sin_list_release(height_appended);
  sin_list_release(height_piece);
  sin_list_release(height_source);
  ASSERT_EQ_INT(32, sin_list_get(height_slice, 0)->i);
  ASSERT_EQ_INT(1119, sin_list_get(height_slice, 1087u)->i);
  sin_list_release(height_slice);

  SIN_LIST_t *group_source = make_int_list(32896);
  SIN_LIST_t *group_slice;
  SIN_LIST_t *group_appended;
  ASSERT_NOT_NULL(group_source);
  group_slice = sin_list_slice(group_source, 32u, 32832u);
  ASSERT_NOT_NULL(group_slice);
  ASSERT_EQ_INT(32832, sin_list_count(group_slice));
  ASSERT_EQ_INT(32, sin_list_get(group_slice, 0)->i);
  ASSERT_EQ_INT(32863, sin_list_get(group_slice, 32831u)->i);
  group_appended = sin_list_append(group_slice, &height_replacement);
  ASSERT_NOT_NULL(group_appended);
  ASSERT_EQ_INT(32833, sin_list_count(group_appended));
  {
    SIN_LIST_ITER_t iter;
    const VALUE_t *values = NULL;
    const SIN_LIST_NODE *leaf = NULL;
    size_t span = 0;
    size_t seen = 0;
    ASSERT_TRUE(sin_list_iter_init(&iter, group_slice));
    while (sin_list_iter_next(&iter, &values, &span, &leaf)) seen += span;
    ASSERT_EQ_INT(32832, seen);
  }
  sin_list_release(group_source);
  ASSERT_EQ_INT(32, sin_list_get(group_slice, 0)->i);
  ASSERT_EQ_INT(32863, sin_list_get(group_slice, 32831u)->i);
  sin_list_release(group_appended);
  sin_list_release(group_slice);

  for (long fail_at = 0; fail_at < 256; ++fail_at) {
    SIN_LIST_t *slice;
    alloc_test_fail_after(fail_at);
    slice = sin_list_slice(source, 32u, 992u);
    alloc_test_fail_after(-1);
    if (slice) {
      if (saw_failure) saw_success_after_failure = true;
      sin_list_release(slice);
    } else {
      saw_failure = true;
    }
    ASSERT_EQ_INT(1056, sin_list_count(source));
    ASSERT_EQ_INT(0, sin_list_get(source, 0)->i);
    ASSERT_EQ_INT(1055, sin_list_get(source, 1055)->i);
  }
  ASSERT_TRUE(saw_failure);
  ASSERT_TRUE(saw_success_after_failure);
  SIN_LIST_t *fragment_source = make_string_list(65);
  ASSERT_NOT_NULL(fragment_source);
  for (long fail_at = 0; fail_at < 128; ++fail_at) {
    SIN_LIST_t *slice;
    alloc_test_fail_after(fail_at);
    slice = sin_list_slice(fragment_source, 1u, 33u);
    alloc_test_fail_after(-1);
    if (slice) {
      if (saw_fragment_failure) saw_fragment_success_after_failure = true;
      sin_list_release(slice);
    } else {
      saw_fragment_failure = true;
    }
    ASSERT_EQ_INT(65, sin_list_count(fragment_source));
    ASSERT_TRUE(strcmp(sin_list_get(fragment_source, 1)->s,
                       "rhs-string-1") == 0);
  }
  ASSERT_TRUE(saw_fragment_failure);
  ASSERT_TRUE(saw_fragment_success_after_failure);
  sin_list_release(fragment_source);
  ASSERT_TRUE(sin_list_slice(source, 1057u, 0) == NULL);
  ASSERT_TRUE(sin_list_slice(source, 1056u, 1) == NULL);
  ASSERT_TRUE(sin_list_slice(source, 0, SIN_LIST_MAX_ELEMENTS + 1u) == NULL);
  ASSERT_TRUE(sin_list_slice(NULL, 0, 0) == NULL);
  sin_list_release(empty);
  sin_list_release(source);
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
