#include <ctype.h>
#include <string.h>

#include "item.h"
#include "libcall_common.h"
#include "libcall_handlers.h"
#include "runtime_context.h"
#include "stack.h"

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
