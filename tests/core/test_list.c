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
