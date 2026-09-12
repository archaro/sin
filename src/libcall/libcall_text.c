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
