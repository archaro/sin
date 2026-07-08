#include <ctype.h>

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
