// Shared libcall helpers.

// Licensed under the MIT License - see LICENSE file for details.

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#include "error.h"
#include "item.h"
#include "runtime_context.h"
#include "stack.h"
#include "value.h"

static inline bool lc_value_is_type(VALUE_t v, VALUE_e type) {
  return value_is_type(&v, type);
}

static inline uint8_t *lc_invalid_args_return(RuntimeContext *ctx, uint8_t *nextop, VALUE_t ret) {
  set_error_item(ctx ? ctx->itemroot : NULL, ERR_RUNTIME_INVALIDARGS,
                         NULL, ctx ? ctx->current_item : NULL);
  push_stack(ctx->vm->stack, ret);
  return nextop;
}

static inline uint8_t *lc_invalid_args_detail_return(RuntimeContext *ctx, uint8_t *nextop, VALUE_t ret, const char *detail) {
  set_error_item(ctx ? ctx->itemroot : NULL, ERR_RUNTIME_INVALIDARGS,
                         detail, ctx ? ctx->current_item : NULL);
  push_stack(ctx->vm->stack, ret);
  return nextop;
}

static inline void lc_cleanup_values(VALUE_t *values, size_t count) {
  for (size_t i = 0; i < count; i++) {
    value_free(&values[i]);
  }
}
