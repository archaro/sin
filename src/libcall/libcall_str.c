#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "floatconv.h"
#include "item.h"
#include "libcall_common.h"
#include "libcall_handlers.h"
#include "runtime_context.h"
#include "stack.h"
#include "string_limits.h"

static uint8_t *lc_str_invalid_top(RuntimeContext *ctx, uint8_t *nextop,
                                   const char *detail) {
  VALUE_t val = pop_stack(ctx->vm->stack);
  FREE_STR(val);
  return lc_invalid_args_detail_return(ctx, nextop, VALUE_NIL, detail);
}

static VALUE_t *lc_str_peek_string(RuntimeContext *ctx) {
  VALUE_t *val = peek_stack(ctx->vm->stack);
  return (val && val->type == VALUE_str) ? val : NULL;
}

static void str_ltrim_inplace(char *s) {
  char *start = s;
  while (*start && isspace((unsigned char)*start)) start++;
  if (start != s) memmove(s, start, strlen(start) + 1);
}

static void str_rtrim_inplace(char *s) {
  size_t len = strlen(s);
  while (len > 0 && isspace((unsigned char)s[len - 1])) len--;
  s[len] = '\0';
}

static void str_trim_inplace(char *s) {
  char *start = s;
  while (*start && isspace((unsigned char)*start)) start++;

  char *scan = start;
  char *end = start;
  while (*scan) {
    if (!isspace((unsigned char)*scan)) end = scan + 1;
    scan++;
  }

  size_t len = (size_t)(end - start);
  if (start != s && len > 0) memmove(s, start, len);
  s[len] = '\0';
}

static bool str_find_popped(VALUE_t haystack, VALUE_t needle,
                            const char **match) {
  if (haystack.type != VALUE_str || needle.type != VALUE_str) {
    VALUE_t args[] = {needle, haystack};
    lc_cleanup_values(args, sizeof(args) / sizeof(args[0]));
    return false;
  }
  *match = strstr(haystack.s, needle.s);
  return true;
}

static bool str_equal_casei(const char *left, const char *right) {
  while (*left && *right) {
    unsigned char l = (unsigned char)*left;
    unsigned char r = (unsigned char)*right;
    if (l != r) {
      if (l >= 'A' && l <= 'Z') l = (unsigned char)(l + ('a' - 'A'));
      if (r >= 'A' && r <= 'Z') r = (unsigned char)(r + ('a' - 'A'));
      if (l != r) return false;
    }
    left++;
    right++;
  }
  return *left == *right;
}

static uint8_t *lc_str_affix(RuntimeContext *ctx, uint8_t *nextop,
                             bool suffix, const char *detail) {
  VALUE_t needle = pop_stack(ctx->vm->stack);
  VALUE_t haystack = pop_stack(ctx->vm->stack);

  if (haystack.type != VALUE_str || needle.type != VALUE_str) {
    VALUE_t args[] = {needle, haystack};
    lc_cleanup_values(args, sizeof(args) / sizeof(args[0]));
    return lc_invalid_args_detail_return(ctx, nextop, VALUE_FALSE, detail);
  }

  size_t haystack_len = strlen(haystack.s);
  size_t needle_len = strlen(needle.s);
  bool matched = false;
  if (needle_len <= haystack_len) {
    const char *start = suffix ? haystack.s + haystack_len - needle_len
        : haystack.s;
    matched = memcmp(start, needle.s, needle_len) == 0;
  }

  FREE_STR(needle);
  FREE_STR(haystack);
  push_stack(ctx->vm->stack, matched ? VALUE_TRUE : VALUE_FALSE);
  return nextop;
}

static uint8_t *lc_str_pad(RuntimeContext *ctx, uint8_t *nextop, bool left,
                           const char *detail) {
  VALUE_t width = pop_stack(ctx->vm->stack);
  VALUE_t text = pop_stack(ctx->vm->stack);

  if (text.type != VALUE_str || width.type != VALUE_int || width.i <= 0) {
    VALUE_t args[] = {width, text};
    lc_cleanup_values(args, sizeof(args) / sizeof(args[0]));
    return lc_invalid_args_detail_return(ctx, nextop, VALUE_NIL, detail);
  }

  size_t text_len = strlen(text.s);
  uint64_t width_u = (uint64_t)width.i;
  if (width_u > (uint64_t)SIN_MAX_STRING_BYTES) {
    FREE_STR(text);
    push_stack(ctx->vm->stack, VALUE_NIL);
    return nextop;
  }
  if (width_u <= (uint64_t)text_len) {
    push_stack(ctx->vm->stack, text);
    return nextop;
  }
  if (width_u > (uint64_t)SIZE_MAX - 1) {
    FREE_STR(text);
    push_stack(ctx->vm->stack, VALUE_NIL);
    return nextop;
  }

  size_t out_len = (size_t)width_u;
  char *out = malloc(out_len + 1);
  if (!out) {
    FREE_STR(text);
    push_stack(ctx->vm->stack, VALUE_NIL);
    return nextop;
  }

  size_t pad_len = out_len - text_len;
  if (left) {
    memset(out, ' ', pad_len);
    memcpy(out + pad_len, text.s, text_len);
  } else {
    memcpy(out, text.s, text_len);
    memset(out + text_len, ' ', pad_len);
  }
  out[out_len] = '\0';

  FREE_STR(text);
  push_stack(ctx->vm->stack, (VALUE_t){VALUE_str, {.s = out}});
  return nextop;
}

uint8_t *lc_str_capitalise(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item) {
  // Mutate the string on top of the stack by uppercasing its first byte.
  // Invalid input is consumed and replaced with nil.
  (void)item;

  VALUE_t *val = lc_str_peek_string(ctx);
  if (val) {
    val->s[0] = (char)toupper((unsigned char)val->s[0]);
    return nextop;
  }
  return lc_str_invalid_top(ctx, nextop, "str.capitalise text must be a string");
}

uint8_t *lc_str_upper(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item) {
  // Mutate the string on top of the stack by uppercasing each byte.
  // Invalid input is consumed and replaced with nil.
  (void)item;

  VALUE_t *val = lc_str_peek_string(ctx);
  if (val) {
    char *c = val->s;
    while (*c) {
      *c = (char)toupper((unsigned char)*c);
      c++;
    }
    return nextop;
  }
  return lc_str_invalid_top(ctx, nextop, "str.upper text must be a string");
}

uint8_t *lc_str_lower(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item) {
  // Mutate the string on top of the stack by lowercasing each byte.
  // Invalid input is consumed and replaced with nil.
  (void)item;

  VALUE_t *val = lc_str_peek_string(ctx);
  if (val) {
    char *c = val->s;
    while (*c) {
      *c = (char)tolower((unsigned char)*c);
      c++;
    }
    return nextop;
  }
  return lc_str_invalid_top(ctx, nextop, "str.lower text must be a string");
}

uint8_t *lc_str_len(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item) {
  // Consume the string on top of the stack and push its byte length, excluding
  // the terminating null byte. Invalid input is consumed and replaced with nil.
  (void)item;

  if (lc_str_peek_string(ctx)) {
    VALUE_t val = pop_stack(ctx->vm->stack);
    size_t len = strlen(val.s);
    FREE_STR(val);
    val.type = VALUE_int;
    val.i = (int64_t)len;
    push_stack(ctx->vm->stack, val);
    return nextop;
  }
  return lc_str_invalid_top(ctx, nextop, "str.len text must be a string");
}

uint8_t *lc_str_trim(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item) {
  // Mutate the string on top of the stack by trimming leading and trailing
  // whitespace bytes. Invalid input is consumed and replaced with nil.
  (void)item;

  VALUE_t *val = lc_str_peek_string(ctx);
  if (val) {
    str_trim_inplace(val->s);
    return nextop;
  }
  return lc_str_invalid_top(ctx, nextop, "str.trim text must be a string");
}

uint8_t *lc_str_ltrim(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item) {
  // Mutate the string on top of the stack by trimming leading whitespace bytes.
  // Invalid input is consumed and replaced with nil.
  (void)item;

  VALUE_t *val = lc_str_peek_string(ctx);
  if (val) {
    str_ltrim_inplace(val->s);
    return nextop;
  }
  return lc_str_invalid_top(ctx, nextop, "str.ltrim text must be a string");
}

uint8_t *lc_str_rtrim(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item) {
  // Mutate the string on top of the stack by trimming trailing whitespace bytes.
  // Invalid input is consumed and replaced with nil.
  (void)item;

  VALUE_t *val = lc_str_peek_string(ctx);
  if (val) {
    str_rtrim_inplace(val->s);
    return nextop;
  }
  return lc_str_invalid_top(ctx, nextop, "str.rtrim text must be a string");
}

uint8_t *lc_str_substr(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item) {
  // Consume text, start, and len from the stack and push a new byte substring.
  // Stack order is text, start, len, with len on top. Invalid input is consumed
  // and replaced with nil.
  (void)item;

  VALUE_t len = pop_stack(ctx->vm->stack);
  VALUE_t start = pop_stack(ctx->vm->stack);
  VALUE_t text = pop_stack(ctx->vm->stack);

  if (text.type != VALUE_str || start.type != VALUE_int || len.type != VALUE_int) {
    VALUE_t args[] = {len, start, text};
    lc_cleanup_values(args, sizeof(args) / sizeof(args[0]));
    return lc_invalid_args_detail_return(ctx, nextop, VALUE_NIL,
        "str.substr text must be a string and start/len must be integers");
  }

  if (start.i < 0) {
    VALUE_t args[] = {len, start, text};
    lc_cleanup_values(args, sizeof(args) / sizeof(args[0]));
    return lc_invalid_args_detail_return(ctx, nextop, VALUE_NIL,
        "str.substr start must be non-negative");
  }

  if (len.i < 1) {
    FREE_STR(text);
    push_stack(ctx->vm->stack, VALUE_NIL);
    return nextop;
  }

  const char *src = text.s;
  size_t text_len = strlen(src);
  size_t start_pos = (size_t)start.i;

  if (start_pos > text_len) {
    FREE_STR(text);
    VALUE_t ret = {VALUE_str, {.s = malloc(1)}};
    if (!ret.s) {
      push_stack(ctx->vm->stack, VALUE_NIL);
      return nextop;
    }
    ret.s[0] = '\0';
    push_stack(ctx->vm->stack, ret);
    return nextop;
  }

  size_t available = text_len - start_pos;
  uint64_t requested_u64 = (uint64_t)len.i;
  size_t out_len = requested_u64 > (uint64_t)available
      ? available
      : (size_t)requested_u64;

  char *out = malloc(out_len + 1);
  if (!out) {
    FREE_STR(text);
    push_stack(ctx->vm->stack, VALUE_NIL);
    return nextop;
  }
  memcpy(out, src + start_pos, out_len);
  out[out_len] = '\0';

  FREE_STR(text);
  VALUE_t ret = {VALUE_str, {.s = out}};
  push_stack(ctx->vm->stack, ret);
  return nextop;
}

uint8_t *lc_str_find(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item) {
  // Consume haystack and needle from the stack and push the index of the
  // needle, or -1 if the needle is not found..
  // Stack order is haystack, needle, with needle on top.
  // Invalid input is consumed and replaced with nil.
  (void)item;

  // Instructions:
  // Always pop both arguments.
  // If haystack or needle are not string values, push nil.  Otherwise:
  // If needle is found in haystack, push the offset at which it is found.
  // If needle is not found in haystack, push -1

  VALUE_t needle = pop_stack(ctx->vm->stack);
  VALUE_t haystack = pop_stack(ctx->vm->stack);
  const char *match = NULL;
  if (!str_find_popped(haystack, needle, &match)) {
    return lc_invalid_args_detail_return(ctx, nextop, VALUE_NIL,
        "str.find haystack and needle must be strings");
  }

  VALUE_t ret = {VALUE_int, {.i = -1}};
  if (match) {
    ret.i = (int64_t)(match - haystack.s);
  }
  FREE_STR(needle);
  FREE_STR(haystack);
  push_stack(ctx->vm->stack, ret);
  return nextop;
}

uint8_t *lc_str_contains(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item) {
  // Consume haystack and needle from the stack and push true if the needle
  // is found, false if not.
  // Stack order is haystack, needle, with needle on top.
  // Invalid input is consumed and replaced with false.
  (void)item;

  // Instructions:
  // This is essentially the same as lc_str_find, and differs only in
  // what is pushed onto the stack at the end.
  // Always pop both arguments.
  // If haystack or needle are not string values, push false.  Otherwise:
  // If needle is found in haystack, push true.
  // If needle is not found in haystack, push false

  VALUE_t needle = pop_stack(ctx->vm->stack);
  VALUE_t haystack = pop_stack(ctx->vm->stack);
  const char *match = NULL;
  if (!str_find_popped(haystack, needle, &match)) {
    return lc_invalid_args_detail_return(ctx, nextop, VALUE_FALSE,
        "str.contains haystack and needle must be strings");
  }

  VALUE_t ret = match ? VALUE_TRUE : VALUE_FALSE;
  FREE_STR(needle);
  FREE_STR(haystack);
  push_stack(ctx->vm->stack, ret);
  return nextop;
}

uint8_t *lc_str_startswith(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item) {
  // Consume haystack and needle from the stack and push true if the needle
  // is found at the start of the haystack, false if not.
  // Stack order is haystack, needle, with needle on top.
  // Invalid input is consumed and replaced with false.
  (void)item;

  // Instructions:
  // If the needle is longer than the haystack, return false.
  // If the needle is the empty string, return true.
  // If the needle is not equal to the start of the haystack, return false.
  // Otherwise return true.

  return lc_str_affix(ctx, nextop, false,
      "str.startswith haystack and needle must be strings");
}

uint8_t *lc_str_endswith(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item) {
  // Consume haystack and needle from the stack and push true if the needle
  // is found at the end of the haystack, false if not.
  // Stack order is haystack, needle, with needle on top.
  // Invalid input is consumed and replaced with false.
  (void)item;

  // Instructions:
  // If the needle is longer than the haystack, return false.
  // If the needle is the empty string, return true.
  // If the needle is not equal to the end of the haystack, return false.
  // Otherwise return true.

  return lc_str_affix(ctx, nextop, true,
      "str.endswith haystack and needle must be strings");
}

uint8_t *lc_str_eqcasei(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item) {
  // Consume two strings from the stack and push true if they match
  // (case-insensitive), otherwise false.
  // Stack order is left, right, with right on top.
  // Invalid input is consumed and replaced with false.
  (void)item;

  // Instructions:
  // If either value is not a string, return false.
  // If the strings are a case-insensitive match return true.
  // Otherwise return false.

  VALUE_t right = pop_stack(ctx->vm->stack);
  VALUE_t left = pop_stack(ctx->vm->stack);

  if (left.type != VALUE_str || right.type != VALUE_str) {
    VALUE_t args[] = {right, left};
    lc_cleanup_values(args, sizeof(args) / sizeof(args[0]));
    return lc_invalid_args_detail_return(ctx, nextop, VALUE_FALSE,
        "str.eqcasei arguments must be strings");
  }

  VALUE_t ret = str_equal_casei(left.s, right.s) ? VALUE_TRUE : VALUE_FALSE;
  FREE_STR(right);
  FREE_STR(left);
  push_stack(ctx->vm->stack, ret);
  return nextop;
}

uint8_t *lc_str_valtostr(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item) {
  // Convert the value on top of the stack to string text. Strings are already
  // stack-owned string values, so they are passed through unchanged.
  (void)item;

  VALUE_t *top = peek_stack(ctx->vm->stack);
  if (!top) return nextop;
  if (top->type == VALUE_str) return nextop;

  VALUE_t val = pop_stack(ctx->vm->stack);
  char *out = NULL;
  switch (val.type) {
    case VALUE_int: {
      char buffer[22];
      int len = snprintf(buffer, sizeof(buffer), "%ld", val.i);
      if (len > 0) out = strdup(buffer);
      break;
    }
    case VALUE_float:
      out = sin_format_binary64(val.f);
      break;
    case VALUE_bool:
      out = strdup(val.i ? "true" : "false");
      break;
    case VALUE_nil:
      out = strdup("nil");
      break;
    case VALUE_str:
      break;
  }

  if (!out) {
    push_stack(ctx->vm->stack, VALUE_NIL);
    return nextop;
  }
  push_stack(ctx->vm->stack, (VALUE_t){VALUE_str, {.s = out}});
  return nextop;
}

uint8_t *lc_str_replace(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item) {
  // Consume text, old, and new from the stack and replace every non-overlapping
  // occurrence of old in text with new. Stack order is text, old, new, with new
  // on top. Invalid input is consumed and replaced with nil.
  (void)item;

  VALUE_t new_text = pop_stack(ctx->vm->stack);
  VALUE_t old_text = pop_stack(ctx->vm->stack);
  VALUE_t text = pop_stack(ctx->vm->stack);

  if (text.type != VALUE_str || old_text.type != VALUE_str ||
      new_text.type != VALUE_str) {
    VALUE_t args[] = {new_text, old_text, text};
    lc_cleanup_values(args, sizeof(args) / sizeof(args[0]));
    return lc_invalid_args_detail_return(ctx, nextop, VALUE_NIL,
        "str.replace text, old, and new must be strings");
  }

  size_t old_len = strlen(old_text.s);
  if (old_len == 0) {
    FREE_STR(new_text);
    FREE_STR(old_text);
    push_stack(ctx->vm->stack, text);
    return nextop;
  }

  size_t text_len = strlen(text.s);
  size_t new_len = strlen(new_text.s);
  size_t count = 0;
  const char *scan = text.s;
  const char *match = NULL;
  while ((match = strstr(scan, old_text.s)) != NULL) {
    count++;
    scan = match + old_len;
  }

  size_t out_len = text_len;
  if (new_len > old_len) {
    size_t delta = new_len - old_len;
    if (count > (SIZE_MAX - text_len) / delta) {
      VALUE_t args[] = {new_text, old_text, text};
      lc_cleanup_values(args, sizeof(args) / sizeof(args[0]));
      push_stack(ctx->vm->stack, VALUE_NIL);
      return nextop;
    }
    out_len += count * delta;
  } else {
    out_len -= count * (old_len - new_len);
  }
  if (out_len > SIN_MAX_STRING_BYTES) {
    VALUE_t args[] = {new_text, old_text, text};
    lc_cleanup_values(args, sizeof(args) / sizeof(args[0]));
    push_stack(ctx->vm->stack, VALUE_NIL);
    return nextop;
  }

  char *out = malloc(out_len + 1);
  if (!out) {
    VALUE_t args[] = {new_text, old_text, text};
    lc_cleanup_values(args, sizeof(args) / sizeof(args[0]));
    push_stack(ctx->vm->stack, VALUE_NIL);
    return nextop;
  }

  char *dst = out;
  scan = text.s;
  while ((match = strstr(scan, old_text.s)) != NULL) {
    size_t prefix_len = (size_t)(match - scan);
    memcpy(dst, scan, prefix_len);
    dst += prefix_len;
    memcpy(dst, new_text.s, new_len);
    dst += new_len;
    scan = match + old_len;
  }
  strcpy(dst, scan);

  VALUE_t args[] = {new_text, old_text, text};
  lc_cleanup_values(args, sizeof(args) / sizeof(args[0]));
  push_stack(ctx->vm->stack, (VALUE_t){VALUE_str, {.s = out}});
  return nextop;
}

uint8_t *lc_str_repeat(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item) {
  // Consume text and count from the stack and push text repeated count times.
  // Stack order is text, count, with count on top. Invalid input is consumed
  // and replaced with nil.
  (void)item;

  VALUE_t count = pop_stack(ctx->vm->stack);
  VALUE_t text = pop_stack(ctx->vm->stack);

  if (text.type != VALUE_str || count.type != VALUE_int || count.i < 0) {
    VALUE_t args[] = {count, text};
    lc_cleanup_values(args, sizeof(args) / sizeof(args[0]));
    return lc_invalid_args_detail_return(ctx, nextop, VALUE_NIL,
        "str.repeat text must be a string and count must be a non-negative integer");
  }

  size_t text_len = strlen(text.s);
  size_t repeat_count = (size_t)count.i;
  if (text_len > 0 && (repeat_count > SIZE_MAX / text_len ||
      repeat_count > SIN_MAX_STRING_BYTES / text_len)) {
    FREE_STR(text);
    push_stack(ctx->vm->stack, VALUE_NIL);
    return nextop;
  }

  size_t out_len = text_len * repeat_count;
  char *out = malloc(out_len + 1);
  if (!out) {
    FREE_STR(text);
    push_stack(ctx->vm->stack, VALUE_NIL);
    return nextop;
  }

  char *dst = out;
  for (size_t i = 0; i < repeat_count; i++) {
    memcpy(dst, text.s, text_len);
    dst += text_len;
  }
  out[out_len] = '\0';

  FREE_STR(text);
  push_stack(ctx->vm->stack, (VALUE_t){VALUE_str, {.s = out}});
  return nextop;
}

uint8_t *lc_str_padleft(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item) {
  // Consume text and width from the stack and left-pad text with spaces up to
  // width bytes. Stack order is text, width, with width on top.
  (void)item;

  return lc_str_pad(ctx, nextop, true,
      "str.padleft text must be a string and width must be a positive integer");
}

uint8_t *lc_str_padright(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item) {
  // Consume text and width from the stack and right-pad text with spaces up to
  // width bytes. Stack order is text, width, with width on top.
  (void)item;

  return lc_str_pad(ctx, nextop, false,
      "str.padright text must be a string and width must be a positive integer");
}
