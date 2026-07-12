#include <ctype.h>
#include <string.h>

#include "item.h"
#include "libcall_common.h"
#include "libcall_handlers.h"
#include "runtime_context.h"
#include "stack.h"

uint8_t *lc_str_capitalise(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item) {
  // If the value on the top of the stack is a string, capitalise the
  // first letter. Otherwise report invalid arguments and return nil.
  (void)item;

  if (ctx->vm->stack->stack[ctx->vm->stack->current].type == VALUE_str) {
    ctx->vm->stack->stack[ctx->vm->stack->current].s[0] =
        (char)toupper((unsigned char)ctx->vm->stack->stack[ctx->vm->stack->current].s[0]);
  } else {
    VALUE_t val = pop_stack(ctx->vm->stack);
    FREE_STR(val);
    return lc_invalid_args_detail_return(ctx, nextop, VALUE_NIL,
        "str.capitalise text must be a string");
  }
  return nextop;
}

uint8_t *lc_str_upper(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item) {
  // If the value on the top of the stack is a string, make it
  // uppercase. Otherwise report invalid arguments and return nil.
  (void)item;

  if (ctx->vm->stack->stack[ctx->vm->stack->current].type == VALUE_str) {
    char *c = ctx->vm->stack->stack[ctx->vm->stack->current].s;
    while (*c) {
      *c = (char)toupper((unsigned char)*c);
      c++;
    }
  } else {
    VALUE_t val = pop_stack(ctx->vm->stack);
    FREE_STR(val);
    return lc_invalid_args_detail_return(ctx, nextop, VALUE_NIL,
        "str.upper text must be a string");
  }
  return nextop;
}

uint8_t *lc_str_lower(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item) {
  // If the value on the top of the stack is a string, make it
  // lowercase. Otherwise report invalid arguments and return nil.
  (void)item;

  if (ctx->vm->stack->stack[ctx->vm->stack->current].type == VALUE_str) {
    char *c = ctx->vm->stack->stack[ctx->vm->stack->current].s;
    while (*c) {
      *c = (char)tolower((unsigned char)*c);
      c++;
    }
  } else {
    VALUE_t val = pop_stack(ctx->vm->stack);
    FREE_STR(val);
    return lc_invalid_args_detail_return(ctx, nextop, VALUE_NIL,
        "str.lower text must be a string");
  }
  return nextop;
}

uint8_t *lc_str_len(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item) {
  // If the value on the top of the stack is a string, return its length
  // (excluding the terminating null character)
  // If the value is anything else, return nil.
  // In any case, always pop the top of the stack and push the result.
  (void)item;

  if (ctx->vm->stack->stack[ctx->vm->stack->current].type == VALUE_str) {
    VALUE_t val = pop_stack(ctx->vm->stack);
    size_t len = strlen(val.s);
    FREE_STR(val);
    val.type = VALUE_int;
    val.i = (int64_t)len;
    push_stack(ctx->vm->stack, val);
  } else {
    VALUE_t val = pop_stack(ctx->vm->stack);
    FREE_STR(val);
    return lc_invalid_args_detail_return(ctx, nextop, VALUE_NIL,
        "str.len text must be a string");
  }
  return nextop;
}

uint8_t *lc_str_trim(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item) {
  // If the value on the top of the stack is a string, trim whitespace
  // at both ends.
  // If the value is anything else, return nil.
  // In any case, always pop the top of the stack and push the result.
  (void)item;

  if (ctx->vm->stack->stack[ctx->vm->stack->current].type == VALUE_str) {
    // Note we are peeking, not popping!
    VALUE_t *val = peek_stack(ctx->vm->stack);
    char *result;
    char *begin = val->s;
    char *end;
    while (*begin && isspace((unsigned char)*begin)) begin++;
    if (*begin == '\0') {
      // All spaces - result is empty string
      result = strdup("");
    } else {
      // Trimmed the left, now trim the right.
      end = begin + strlen((const char *)begin) - 1;
      while (isspace((unsigned char)*end)) end--;
      *(end+1) = '\0'; // Mutation is ok, we are about to free this string.
      result = strdup(begin);
    }
    free(val->s);
    val->s = result;
    // No need to push - we haven't popped the stack, only peeked at it.
  } else {
    VALUE_t val = pop_stack(ctx->vm->stack);
    FREE_STR(val);
    return lc_invalid_args_detail_return(ctx, nextop, VALUE_NIL,
        "str.trim text must be a string");
  }
  return nextop;
}

uint8_t *lc_str_ltrim(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item) {
  // If the value on the top of the stack is a string, trim whitespace to left.
  // If the value is anything else, return nil.
  // In any case, always pop the top of the stack and push the result.
  (void)item;

  if (ctx->vm->stack->stack[ctx->vm->stack->current].type == VALUE_str) {
    // Note we are peeking, not popping!
    VALUE_t *val = peek_stack(ctx->vm->stack);
    char *result;
    const char *begin = val->s;
    while (*begin && isspace((unsigned char)*begin)) begin++;
    // This will create the correct string, even if it is empty ("").
    result = strdup(begin);
    free(val->s);
    val->s = result;
    // No need to push - we haven't popped the stack, only peeked at it.
  } else {
    VALUE_t val = pop_stack(ctx->vm->stack);
    FREE_STR(val);
    return lc_invalid_args_detail_return(ctx, nextop, VALUE_NIL,
        "str.ltrim text must be a string");
  }
  return nextop;
}

uint8_t *lc_str_rtrim(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item) {
  // If the value on the top of the stack is a string, trim whitespace to right.
  // If the value is anything else, return nil.
  // In any case, always pop the top of the stack and push the result.
  (void)item;

  if (ctx->vm->stack->stack[ctx->vm->stack->current].type == VALUE_str) {
    // Note we are peeking, not popping!
    VALUE_t *val = peek_stack(ctx->vm->stack);
    char *result;
    size_t len = strlen(val->s);
    while (len > 0 && isspace((unsigned char)val->s[len - 1]))
      len--;
    val->s[len] = '\0'; // Mutation is ok, we are about to free this string.
    result = strdup(val->s);
    free(val->s);
    val->s = result;
    // No need to push - we haven't popped the stack, only peeked at it.
  } else {
    VALUE_t val = pop_stack(ctx->vm->stack);
    FREE_STR(val);
    return lc_invalid_args_detail_return(ctx, nextop, VALUE_NIL,
        "str.rtrim text must be a string");
  }
  return nextop;
}

uint8_t *lc_str_substr(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item) {
  // The stack contains 3 values:
  // top: (len) the length of the substring to return
  // top-1: (start) the starting offset of the substring
  // top-2: (text) the text value to extract the substring from
  // All three values should be consumed
  // If text is not a string, push nil.
  // Otherwise:
  // If start is greater than the length of the text, push an empty string.
  // If len < 1, push nil.
  // If start + len is greater than the length of the text from start, push
  //   a new string from start to the end of the original string.
  // Otherwise push a new string that is the requested substring.
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
