#include <ctype.h>

#include "item.h"
#include "libcall_handlers.h"
#include "runtime_context.h"
#include "stack.h"

uint8_t *lc_str_capitalise(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item) {
  // If the value on the top of the stack is a string, capitalise the
  // first letter.  Otherwise pop the top of the stack and push nil.
  (void)item;

  if (ctx->vm->stack->stack[ctx->vm->stack->current].type == VALUE_str) {
    ctx->vm->stack->stack[ctx->vm->stack->current].s[0] =
                        toupper(ctx->vm->stack->stack[ctx->vm->stack->current].s[0]);
  } else {
    pop_stack(ctx->vm->stack);
    push_stack(ctx->vm->stack, VALUE_NIL);
  }
  return nextop;
}

uint8_t *lc_str_upper(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item) {
  // If the value on the top of the stack is a string, make it
  // uppercase.  Otherwise pop the top of the stack and push nil.
  (void)item;

  if (ctx->vm->stack->stack[ctx->vm->stack->current].type == VALUE_str) {
    char *c = ctx->vm->stack->stack[ctx->vm->stack->current].s;
    while (*c) {
      *c = toupper(*c);
      c++;
    }
  } else {
    pop_stack(ctx->vm->stack);
    push_stack(ctx->vm->stack, VALUE_NIL);
  }
  return nextop;
}

uint8_t *lc_str_lower(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item) {
  // If the value on the top of the stack is a string, make it
  // lowercase.  Otherwise pop the top of the stack and push nil.
  (void)item;

  if (ctx->vm->stack->stack[ctx->vm->stack->current].type == VALUE_str) {
    char *c = ctx->vm->stack->stack[ctx->vm->stack->current].s;
    while (*c) {
      *c = tolower(*c);
      c++;
    }
  } else {
    pop_stack(ctx->vm->stack);
    push_stack(ctx->vm->stack, VALUE_NIL);
  }
  return nextop;
}
