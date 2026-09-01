#include <math.h>
#include <stdint.h>

#include "libcall_common.h"
#include "libcall_handlers.h"
#include "stack.h"

static uint8_t *lc_math_undefined_return(RuntimeContext *ctx,
                                         uint8_t *nextop) {
  set_error_item(ctx ? itemstore_root(ctx->itemstore) : NULL,
                 ERR_RUNTIME_UNDEFINED, NULL,
                 ctx ? ctx->current_item : NULL);
  push_stack(ctx->vm->stack, VALUE_NIL);
  return nextop;
}

uint8_t *lc_math_abs(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item) {
  VALUE_t value;
  (void)item;

  value = pop_stack(ctx->vm->stack);
  if (value.type == VALUE_int) {
    if (value.i == INT64_MIN) {
      value_free(&value);
      return lc_math_undefined_return(ctx, nextop);
    }

    int64_t result = value.i < 0 ? -value.i : value.i;
    value_free(&value);
    push_stack(ctx->vm->stack, (VALUE_t){VALUE_int, {.i = result}});
    return nextop;
  }

  if (value.type == VALUE_float) {
    double result = fabs(value.f);
    bool representable = isfinite(result);
    value_free(&value);
    if (!representable) {
      return lc_math_undefined_return(ctx, nextop);
    }
    push_stack(ctx->vm->stack, (VALUE_t){VALUE_float, {.f = result}});
    return nextop;
  }

  value_free(&value);
  return lc_invalid_args_detail_return(ctx, nextop, VALUE_NIL,
                                       "math.abs expects an integer or float");
}
