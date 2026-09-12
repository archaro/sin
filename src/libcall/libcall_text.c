// Text-oriented library calls.

// Licensed under the MIT License - see LICENSE file for details.

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "libcall_common.h"
#include "libcall_handlers.h"
#include "list.h"
#include "memory.h"
#include "runtime_context.h"
#include "stack.h"
#include "string_limits.h"

static void text_free_parts(VALUE_t *parts, size_t count) {
  if (parts) lc_cleanup_values(parts, count);
  free(parts);
}

static uint8_t *text_split_failure(RuntimeContext *ctx, uint8_t *nextop,
                                   VALUE_t text, VALUE_t separator,
                                   VALUE_t *parts, size_t count) {
  text_free_parts(parts, count);
  value_free(&text);
  value_free(&separator);
  push_stack(ctx->vm->stack, VALUE_NIL);
  return nextop;
}

static const char *text_find_separator(const char *cursor, const char *end,
                                       const char *separator,
                                       size_t separator_len) {
  if (separator_len == 1u) {
    for (const char *candidate = cursor; candidate < end; ++candidate) {
      if (*candidate == separator[0]) return candidate;
    }
    return NULL;
  }

  for (const char *candidate = cursor; candidate < end; ++candidate) {
    if ((size_t)(end - candidate) >= separator_len &&
        *candidate == separator[0] &&
        memcmp(candidate, separator, separator_len) == 0) {
      return candidate;
    }
  }
  return NULL;
}

static bool text_split_count(const char *text, size_t text_len,
                             const char *separator, size_t separator_len,
                             size_t *count) {
  const char *cursor = text;
  const char *end = text + text_len;
  size_t found = 0;

  while (cursor < end) {
    const char *match = text_find_separator(cursor, end, separator,
                                            separator_len);
    const char *part_end = match ? match : end;
    if (part_end > cursor) {
      if (found == SIN_LIST_MAX_ELEMENTS) return false;
      ++found;
    }
    if (!match) break;
    cursor = match + separator_len;
  }
  if (count) *count = found;
  return true;
}

uint8_t *lc_text_split(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item) {
  // Consume text and separator, with separator on top. The separator is a
  // literal, case-sensitive, non-overlapping substring; empty fields are
  // omitted. Returned strings are copies so input cleanup is independent.
  (void)item;

  VALUE_t separator = pop_stack(ctx->vm->stack);
  VALUE_t text = pop_stack(ctx->vm->stack);
  if (text.type != VALUE_str || separator.type != VALUE_str ||
      !text.s || !separator.s) {
    VALUE_t args[] = {separator, text};
    lc_cleanup_values(args, sizeof(args) / sizeof(args[0]));
    return lc_invalid_args_nil_return(ctx, nextop,
        "text.split text and separator must be strings");
  }

  size_t text_len = strlen(text.s);
  size_t separator_len = strlen(separator.s);
  if (separator_len == 0) {
    VALUE_t *parts = alloc_calloc(1, sizeof(*parts));
    if (!parts) return text_split_failure(ctx, nextop, text, separator, NULL, 0);
    size_t copy_len;
    if (alloc_add_overflow(text_len, 1, &copy_len) ||
        copy_len > (size_t)SIN_MAX_STRING_BYTES + 1u) {
      return text_split_failure(ctx, nextop, text, separator, parts, 0);
    }
    parts[0].type = VALUE_str;
    parts[0].s = alloc_malloc(copy_len);
    if (!parts[0].s) {
      return text_split_failure(ctx, nextop, text, separator, parts, 1);
    }
    memcpy(parts[0].s, text.s, copy_len);
    value_free(&text);
    value_free(&separator);
    SIN_LIST_t *result = sin_list_build_owned(parts, 1);
    if (!result) return text_split_failure(ctx, nextop, VALUE_NIL, VALUE_NIL,
                                           parts, 1);
    free(parts);
    push_stack(ctx->vm->stack, (VALUE_t){VALUE_list, {.list = result}});
    return nextop;
  }

  size_t count = 0;
  if (!text_split_count(text.s, text_len, separator.s, separator_len, &count))
    return text_split_failure(ctx, nextop, text, separator, NULL, 0);

  if (count == 0) {
    value_free(&text);
    value_free(&separator);
    SIN_LIST_t *result = sin_list_build_owned(NULL, 0);
    if (!result) {
      push_stack(ctx->vm->stack, VALUE_NIL);
      return nextop;
    }
    push_stack(ctx->vm->stack, (VALUE_t){VALUE_list, {.list = result}});
    return nextop;
  }

  VALUE_t *parts = alloc_calloc(count, sizeof(*parts));
  if (!parts) return text_split_failure(ctx, nextop, text, separator, NULL, 0);

  const char *cursor = text.s;
  const char *end = text.s + text_len;
  size_t part_index = 0;
  while (cursor < end) {
    const char *match = text_find_separator(cursor, end, separator.s,
                                            separator_len);
    const char *part_end = match ? match : end;
    if (part_end > cursor) {
      size_t part_len = (size_t)(part_end - cursor);
      size_t allocation_size;
      if (alloc_add_overflow(part_len, 1, &allocation_size) ||
          allocation_size > (size_t)SIN_MAX_STRING_BYTES + 1u) {
        return text_split_failure(ctx, nextop, text, separator, parts,
                                  part_index);
      }
      parts[part_index].type = VALUE_str;
      parts[part_index].s = alloc_malloc(allocation_size);
      if (!parts[part_index].s) {
        return text_split_failure(ctx, nextop, text, separator, parts,
                                  part_index + 1);
      }
      memcpy(parts[part_index].s, cursor, part_len);
      parts[part_index].s[part_len] = '\0';
      ++part_index;
    }
    if (!match) break;
    cursor = match + separator_len;
  }

  value_free(&text);
  value_free(&separator);
  SIN_LIST_t *result = sin_list_build_owned(parts, count);
  if (!result) return text_split_failure(ctx, nextop, VALUE_NIL, VALUE_NIL,
                                         parts, count);
  free(parts);
  push_stack(ctx->vm->stack, (VALUE_t){VALUE_list, {.list = result}});
  return nextop;
}

static uint8_t *text_join_failure(RuntimeContext *ctx, uint8_t *nextop,
                                  VALUE_t list, VALUE_t separator) {
  value_free(&list);
  value_free(&separator);
  push_stack(ctx->vm->stack, VALUE_NIL);
  return nextop;
}

uint8_t *lc_text_join(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item) {
  // Consume a homogeneous list of strings and a separator, returning an
  // independently owned byte-string joined with separators between fields.
  // The list is first validated in full so a later malformed element wins
  // over any output-size failure found during the subsequent length pass.
  (void)item;

  VALUE_t separator = pop_stack(ctx->vm->stack);
  VALUE_t list = pop_stack(ctx->vm->stack);
  if (list.type != VALUE_list || !list.list || separator.type != VALUE_str ||
      !separator.s) {
    VALUE_t args[] = {separator, list};
    lc_cleanup_values(args, sizeof(args) / sizeof(args[0]));
    return lc_invalid_args_nil_return(ctx, nextop,
        "text.join list must contain only non-null strings and separator must be a string");
  }

  size_t count = sin_list_count(list.list);
  for (size_t i = 0; i < count; ++i) {
    const VALUE_t *value = sin_list_get(list.list, i);
    if (!value || value->type != VALUE_str || !value->s) {
      value_free(&list);
      value_free(&separator);
      return lc_invalid_args_nil_return(ctx, nextop,
          "text.join list must contain only non-null strings and separator must be a string");
    }
  }

  size_t separator_len = strlen(separator.s);
  size_t output_len = 0;
  for (size_t i = 0; i < count; ++i) {
    const VALUE_t *value = sin_list_get(list.list, i);
    size_t value_len = strlen(value->s);
    if (alloc_add_overflow(output_len, value_len, &output_len))
      return text_join_failure(ctx, nextop, list, separator);
  }
  if (count > 1u) {
    size_t separators_len = 0;
    if (alloc_mul_overflow(count - 1u, separator_len, &separators_len) ||
        alloc_add_overflow(output_len, separators_len, &output_len))
      return text_join_failure(ctx, nextop, list, separator);
  }
  if (output_len > SIN_MAX_STRING_BYTES)
    return text_join_failure(ctx, nextop, list, separator);

  size_t allocation_size = 0;
  if (alloc_add_overflow(output_len, 1u, &allocation_size))
    return text_join_failure(ctx, nextop, list, separator);
  char *output = alloc_malloc(allocation_size);
  if (!output) return text_join_failure(ctx, nextop, list, separator);

  char *cursor = output;
  for (size_t i = 0; i < count; ++i) {
    const VALUE_t *value = sin_list_get(list.list, i);
    size_t value_len = strlen(value->s);
    memcpy(cursor, value->s, value_len);
    cursor += value_len;
    if (i + 1u < count) {
      memcpy(cursor, separator.s, separator_len);
      cursor += separator_len;
    }
  }
  *cursor = '\0';

  value_free(&list);
  value_free(&separator);
  push_stack(ctx->vm->stack, (VALUE_t){VALUE_str, {.s = output}});
  return nextop;
}
