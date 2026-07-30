#include <stdint.h>
#include <stddef.h>
#include <limits.h>

#include "libcall_common.h"
#include "libcall_handlers.h"
#include "list.h"
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
