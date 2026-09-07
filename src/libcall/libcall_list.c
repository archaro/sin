#include <stdint.h>
#include <stddef.h>
#include <limits.h>
#include <math.h>

#include "libcall_common.h"
#include "libcall_handlers.h"
#include "list.h"
#include "memory.h"
#include "stack.h"

static bool list_index(VALUE_t value, size_t *out) {
  if (value.type != VALUE_int || value.i < 0) return false;
  if ((uint64_t)value.i > (uint64_t)SIZE_MAX) return false;
  *out = (size_t)value.i;
  return true;
}

static uint8_t *list_range_nil(RuntimeContext *ctx, uint8_t *nextop) {
  push_stack(ctx->vm->stack, VALUE_NIL);
  return nextop;
}

typedef enum {
  LIST_SORT_DOMAIN_EMPTY,
  LIST_SORT_DOMAIN_NUMERIC,
  LIST_SORT_DOMAIN_STRING,
  LIST_SORT_DOMAIN_BOOL
} LIST_SORT_DOMAIN_e;

typedef enum {
  LIST_SORT_VALID,
  LIST_SORT_INVALID,
  LIST_SORT_UNDEFINED
} LIST_SORT_VALIDATION_e;

static LIST_SORT_VALIDATION_e list_sort_validate(
    const SIN_LIST_t *list, LIST_SORT_DOMAIN_e *domain, bool *numeric_float) {
  size_t count;
  if (!list || !domain || !numeric_float) return LIST_SORT_INVALID;
  *numeric_float = false;
  count = sin_list_count(list);
  if (count == 0) {
    *domain = LIST_SORT_DOMAIN_EMPTY;
    return LIST_SORT_VALID;
  }
  const VALUE_t *first = sin_list_get(list, 0);
  if (!first) return LIST_SORT_INVALID;
  if (first->type == VALUE_int || first->type == VALUE_float) {
    *domain = LIST_SORT_DOMAIN_NUMERIC;
  } else if (first->type == VALUE_str) {
    *domain = LIST_SORT_DOMAIN_STRING;
  } else if (first->type == VALUE_bool) {
    *domain = LIST_SORT_DOMAIN_BOOL;
  } else {
    return LIST_SORT_INVALID;
  }
  for (size_t i = 0; i < count; ++i) {
    const VALUE_t *value = sin_list_get(list, i);
    if (!value) return LIST_SORT_INVALID;
    if (value->type == VALUE_float && isnan(value->f)) {
      return LIST_SORT_UNDEFINED;
    }
    if (value->type == VALUE_float) *numeric_float = true;
    if ((*domain == LIST_SORT_DOMAIN_NUMERIC &&
         (value->type == VALUE_int || value->type == VALUE_float)) ||
        (*domain == LIST_SORT_DOMAIN_STRING && value->type == VALUE_str) ||
        (*domain == LIST_SORT_DOMAIN_BOOL && value->type == VALUE_bool)) {
      continue;
    }
    return LIST_SORT_INVALID;
  }
  return LIST_SORT_VALID;
}

static int list_string_order(const char *left, const char *right) {
  const unsigned char *lhs = (const unsigned char *)(left ? left : "");
  const unsigned char *rhs = (const unsigned char *)(right ? right : "");
  while (*lhs != '\0' && *lhs == *rhs) {
    ++lhs;
    ++rhs;
  }
  return (*lhs > *rhs) - (*lhs < *rhs);
}

static bool list_sort_order(const VALUE_t *left, const VALUE_t *right,
                            LIST_SORT_DOMAIN_e domain, bool numeric_float,
                            int *comparison) {
  if (domain == LIST_SORT_DOMAIN_STRING) {
    *comparison = list_string_order(left->s, right->s);
    return true;
  }
  if (domain == LIST_SORT_DOMAIN_NUMERIC && numeric_float) {
    double lhs = left->type == VALUE_int ? (double)left->i : left->f;
    double rhs = right->type == VALUE_int ? (double)right->i : right->f;
    *comparison = (lhs > rhs) - (lhs < rhs);
    return true;
  }
  return value_order(left, right, comparison);
}

static SIN_LIST_t *list_clone_in_order(const SIN_LIST_t *source, bool reverse) {
  size_t count;
  VALUE_t *values;
  SIN_LIST_t *result;
  if (!source) return NULL;
  count = sin_list_count(source);
  if (count == 0) return sin_list_build_owned(NULL, 0);
  values = alloc_calloc(count, sizeof(*values));
  if (!values) return NULL;
  for (size_t i = 0; i < count; ++i) {
    size_t source_index = reverse ? count - i - 1u : i;
    const VALUE_t *value = sin_list_get(source, source_index);
    if (!value || !value_clone_fallible(value, &values[i])) {
      lc_cleanup_values(values, count);
      free(values);
      return NULL;
    }
  }
  result = sin_list_build_owned(values, count);
  free(values);
  return result;
}

static void list_merge_sort(VALUE_t *values, VALUE_t *scratch, size_t first,
                            size_t last, LIST_SORT_DOMAIN_e domain,
                            bool numeric_float, bool descending) {
  size_t middle;
  size_t left;
  size_t right;
  size_t out;

  if (last - first < 2u) return;
  middle = first + (last - first) / 2u;
  list_merge_sort(values, scratch, first, middle, domain, numeric_float,
                  descending);
  list_merge_sort(values, scratch, middle, last, domain, numeric_float,
                  descending);
  left = first;
  right = middle;
  out = first;
  while (left < middle && right < last) {
    int comparison = 0;
    (void)list_sort_order(&values[left], &values[right], domain, numeric_float,
                          &comparison);
    if ((!descending && comparison <= 0) ||
        (descending && comparison >= 0)) {
      scratch[out++] = values[left++];
    } else {
      scratch[out++] = values[right++];
    }
  }
  while (left < middle) scratch[out++] = values[left++];
  while (right < last) scratch[out++] = values[right++];
  for (size_t i = first; i < last; ++i) values[i] = scratch[i];
}

static SIN_LIST_t *list_sorted_clone(const SIN_LIST_t *source,
                                     LIST_SORT_DOMAIN_e domain,
                                     bool numeric_float, bool descending) {
  size_t count = sin_list_count(source);
  VALUE_t *values;
  VALUE_t *scratch;
  SIN_LIST_t *result;
  if (count == 0) return sin_list_build_owned(NULL, 0);
  values = alloc_calloc(count, sizeof(*values));
  if (!values) return NULL;
  for (size_t i = 0; i < count; ++i) {
    const VALUE_t *value = sin_list_get(source, i);
    if (!value || !value_clone_fallible(value, &values[i])) {
      lc_cleanup_values(values, count);
      free(values);
      return NULL;
    }
  }
  scratch = alloc_calloc(count, sizeof(*scratch));
  if (!scratch) {
    lc_cleanup_values(values, count);
    free(values);
    return NULL;
  }
  list_merge_sort(values, scratch, 0, count, domain, numeric_float, descending);
  free(scratch);
  result = sin_list_build_owned(values, count);
  free(values);
  return result;
}

uint8_t *lc_list_length(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item) {
  (void)item;
  VALUE_t list = pop_stack(ctx->vm->stack);
  if (list.type != VALUE_list || !list.list) {
    value_free(&list);
    return lc_invalid_args_detail_return(ctx, nextop, VALUE_NIL,
        "list.length expects a list");
  }
  size_t count = sin_list_count(list.list);
  value_free(&list);
  push_stack(ctx->vm->stack, (VALUE_t){VALUE_int, {.i = (int64_t)count}});
  return nextop;
}

uint8_t *lc_list_islist(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item) {
  (void)item;
  VALUE_t value = pop_stack(ctx->vm->stack);
  int is_list = (value.type == VALUE_list && value.list != NULL) ? 1 : 0;
  value_free(&value);
  push_stack(ctx->vm->stack, (VALUE_t){VALUE_bool, {.i = is_list}});
  return nextop;
}

uint8_t *lc_list_get(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item) {
  (void)item;
  VALUE_t index = pop_stack(ctx->vm->stack);
  VALUE_t list = pop_stack(ctx->vm->stack);
  size_t at;
  const VALUE_t *source;
  VALUE_t result = VALUE_NIL;
  if (list.type != VALUE_list || !list.list || index.type != VALUE_int) {
    VALUE_t args[] = {index, list};
    lc_cleanup_values(args, 2);
    return lc_invalid_args_detail_return(ctx, nextop, VALUE_NIL,
        "list.get expects a list and an in-range non-negative integer index");
  }
  if (!list_index(index, &at) || at >= sin_list_count(list.list)) {
    value_free(&index);
    value_free(&list);
    return list_range_nil(ctx, nextop);
  }
  source = sin_list_get(list.list, at);
  if (source) (void)value_clone_fallible(source, &result);
  value_free(&index);
  value_free(&list);
  push_stack(ctx->vm->stack, result);
  return nextop;
}

uint8_t *lc_list_append(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item) {
  (void)item;
  VALUE_t value = pop_stack(ctx->vm->stack);
  VALUE_t list = pop_stack(ctx->vm->stack);
  SIN_LIST_t *result;
  if (list.type != VALUE_list || !list.list) {
    VALUE_t args[] = {value, list};
    lc_cleanup_values(args, 2);
    return lc_invalid_args_detail_return(ctx, nextop, VALUE_NIL,
        "list.append expects a list and a value");
  }
  result = sin_list_append(list.list, &value);
  value_free(&value);
  value_free(&list);
  if (!result) return list_range_nil(ctx, nextop);
  push_stack(ctx->vm->stack, (VALUE_t){VALUE_list, {.list = result}});
  return nextop;
}

uint8_t *lc_list_set(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item) {
  (void)item;
  VALUE_t value = pop_stack(ctx->vm->stack);
  VALUE_t index = pop_stack(ctx->vm->stack);
  VALUE_t list = pop_stack(ctx->vm->stack);
  size_t at;
  SIN_LIST_t *result;
  if (list.type != VALUE_list || !list.list || index.type != VALUE_int) {
    VALUE_t args[] = {value, index, list};
    lc_cleanup_values(args, 3);
    return lc_invalid_args_detail_return(ctx, nextop, VALUE_NIL,
        "list.set expects a list, an in-range non-negative integer index, and a value");
  }
  if (!list_index(index, &at) || at >= sin_list_count(list.list)) {
    value_free(&value); value_free(&index); value_free(&list);
    return list_range_nil(ctx, nextop);
  }
  result = sin_list_set(list.list, at, &value);
  value_free(&value);
  value_free(&index);
  value_free(&list);
  if (!result) return list_range_nil(ctx, nextop);
  push_stack(ctx->vm->stack, (VALUE_t){VALUE_list, {.list = result}});
  return nextop;
}

uint8_t *lc_list_concat(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item) {
  (void)item;
  VALUE_t right = pop_stack(ctx->vm->stack);
  VALUE_t left = pop_stack(ctx->vm->stack);
  SIN_LIST_t *result;
  if (left.type != VALUE_list || !left.list || right.type != VALUE_list || !right.list) {
    VALUE_t args[] = {right, left};
    lc_cleanup_values(args, 2);
    return lc_invalid_args_detail_return(ctx, nextop, VALUE_NIL,
        "list.concat expects two lists");
  }
  result = sin_list_concat(left.list, right.list);
  value_free(&left);
  value_free(&right);
  if (!result) return list_range_nil(ctx, nextop);
  push_stack(ctx->vm->stack, (VALUE_t){VALUE_list, {.list = result}});
  return nextop;
}

uint8_t *lc_list_slice(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item) {
  (void)item;
  VALUE_t length = pop_stack(ctx->vm->stack);
  VALUE_t start = pop_stack(ctx->vm->stack);
  VALUE_t list = pop_stack(ctx->vm->stack);
  size_t first, count;
  SIN_LIST_t *result;
  if (list.type != VALUE_list || !list.list || start.type != VALUE_int ||
      length.type != VALUE_int) {
    VALUE_t args[] = {length, start, list};
    lc_cleanup_values(args, 3);
    return lc_invalid_args_detail_return(ctx, nextop, VALUE_NIL,
        "list.slice expects a list and a non-negative in-range start and length");
  }
  if (!list_index(start, &first) || !list_index(length, &count) ||
      first > sin_list_count(list.list) || count > sin_list_count(list.list) - first) {
    value_free(&length); value_free(&start); value_free(&list);
    return list_range_nil(ctx, nextop);
  }
  result = sin_list_slice(list.list, first, count);
  value_free(&length);
  value_free(&start);
  value_free(&list);
  if (!result) return list_range_nil(ctx, nextop);
  push_stack(ctx->vm->stack, (VALUE_t){VALUE_list, {.list = result}});
  return nextop;
}

uint8_t *lc_list_reverse(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item) {
  (void)item;
  VALUE_t list = pop_stack(ctx->vm->stack);
  SIN_LIST_t *result;
  if (list.type != VALUE_list || !list.list) {
    value_free(&list);
    return lc_invalid_args_detail_return(ctx, nextop, VALUE_NIL,
        "list.reverse expects a list");
  }
  result = list_clone_in_order(list.list, true);
  value_free(&list);
  if (!result) return list_range_nil(ctx, nextop);
  push_stack(ctx->vm->stack, (VALUE_t){VALUE_list, {.list = result}});
  return nextop;
}

static uint8_t *lc_list_sort(RuntimeContext *ctx, uint8_t *nextop,
                             VALUE_t list, bool descending, const char *name) {
  LIST_SORT_DOMAIN_e domain;
  LIST_SORT_VALIDATION_e validation;
  bool numeric_float;
  SIN_LIST_t *result;
  if (list.type != VALUE_list || !list.list) {
    value_free(&list);
    return lc_invalid_args_detail_return(ctx, nextop, VALUE_NIL, name);
  }
  validation = list_sort_validate(list.list, &domain, &numeric_float);
  if (validation == LIST_SORT_UNDEFINED) {
    value_free(&list);
    return lc_undefined_nil_return(ctx, nextop);
  }
  if (validation != LIST_SORT_VALID) {
    value_free(&list);
    return lc_invalid_args_detail_return(ctx, nextop, VALUE_NIL, name);
  }
  result = list_sorted_clone(list.list, domain, numeric_float, descending);
  value_free(&list);
  if (!result) return list_range_nil(ctx, nextop);
  push_stack(ctx->vm->stack, (VALUE_t){VALUE_list, {.list = result}});
  return nextop;
}

uint8_t *lc_list_asc(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item) {
  (void)item;
  return lc_list_sort(ctx, nextop, pop_stack(ctx->vm->stack), false,
                      "list.asc expects a list of numbers, strings, or booleans");
}

uint8_t *lc_list_desc(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item) {
  (void)item;
  return lc_list_sort(ctx, nextop, pop_stack(ctx->vm->stack), true,
                      "list.desc expects a list of numbers, strings, or booleans");
}
