// Value conversion libcalls.

// Licensed under the MIT License - see LICENSE file for details.

#include <stdint.h>

#include "libcall_common.h"
#include "libcall_handlers.h"
#include "stack.h"

uint8_t *lc_conv_bool(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item) {
  (void)item;
  VALUE_t value = pop_stack(ctx->vm->stack);
  bool truthy = value_is_truthy(&value) != 0;
  value_free(&value);
  push_stack(ctx->vm->stack, truthy ? VALUE_TRUE : VALUE_FALSE);
  return nextop;
}
