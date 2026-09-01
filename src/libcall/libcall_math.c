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

typedef enum {
  MATH_ROUND_FLOOR,
  MATH_ROUND_CEIL,
  MATH_ROUND_NEAREST,
} MathRoundMode;

static uint8_t *lc_math_rounding(RuntimeContext *ctx, uint8_t *nextop,
                                 ITEM_t *item, MathRoundMode mode,
                                 const char *name) {
  VALUE_t value;
  double rounded = 0.0;
  (void)item;

  value = pop_stack(ctx->vm->stack);
  if (value.type == VALUE_int) {
    int64_t result = value.i;
    value_free(&value);
    push_stack(ctx->vm->stack, (VALUE_t){VALUE_int, {.i = result}});
    return nextop;
  }

  if (value.type != VALUE_float) {
    value_free(&value);
    return lc_invalid_args_detail_return(
        ctx, nextop, VALUE_NIL, name);
  }

  switch (mode) {
    case MATH_ROUND_FLOOR:
      rounded = floor(value.f);
      break;
    case MATH_ROUND_CEIL:
      rounded = ceil(value.f);
      break;
    case MATH_ROUND_NEAREST:
      rounded = round(value.f);
      break;
  }
  value_free(&value);

  /* The lower endpoint is representable as INT64_MIN; the upper endpoint
   * is one past INT64_MAX and must remain excluded before conversion. */
  if (!isfinite(rounded) || rounded < -0x1p63 || rounded >= 0x1p63) {
    return lc_math_undefined_return(ctx, nextop);
  }
  push_stack(ctx->vm->stack,
             (VALUE_t){VALUE_int, {.i = (int64_t)rounded}});
  return nextop;
}

uint8_t *lc_math_floor(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item) {
  return lc_math_rounding(ctx, nextop, item, MATH_ROUND_FLOOR,
                          "math.floor expects an integer or float");
}

uint8_t *lc_math_ceil(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item) {
  return lc_math_rounding(ctx, nextop, item, MATH_ROUND_CEIL,
                          "math.ceil expects an integer or float");
}

uint8_t *lc_math_round(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item) {
  return lc_math_rounding(ctx, nextop, item, MATH_ROUND_NEAREST,
                          "math.round expects an integer or float");
}

static uint8_t *lc_math_select(RuntimeContext *ctx, uint8_t *nextop,
                               ITEM_t *item, bool select_min) {
  VALUE_t right = pop_stack(ctx->vm->stack);
  VALUE_t left = pop_stack(ctx->vm->stack);
  (void)item;

  if ((left.type != VALUE_int && left.type != VALUE_float) ||
      (right.type != VALUE_int && right.type != VALUE_float)) {
    value_free(&left);
    value_free(&right);
    return lc_invalid_args_detail_return(
        ctx, nextop, VALUE_NIL,
        select_min ? "math.min expects two integer or float arguments"
                   : "math.max expects two integer or float arguments");
  }

  if ((left.type == VALUE_float && !isfinite(left.f)) ||
      (right.type == VALUE_float && !isfinite(right.f))) {
    value_free(&left);
    value_free(&right);
    return lc_math_undefined_return(ctx, nextop);
  }

  if (left.type == VALUE_int && right.type == VALUE_int) {
    int64_t result = select_min
                         ? (left.i < right.i ? left.i : right.i)
                         : (left.i > right.i ? left.i : right.i);
    value_free(&left);
    value_free(&right);
    push_stack(ctx->vm->stack, (VALUE_t){VALUE_int, {.i = result}});
    return nextop;
  }

  double left_float = left.type == VALUE_int ? (double)left.i : left.f;
  double right_float = right.type == VALUE_int ? (double)right.i : right.f;
  double result;
  if (left_float == 0.0 && right_float == 0.0) {
    bool left_negative = signbit(left_float) != 0;
    bool right_negative = signbit(right_float) != 0;
    bool negative_result = select_min ? (left_negative || right_negative)
                                     : (left_negative && right_negative);
    result = negative_result ? -0.0 : 0.0;
  } else {
    result = select_min ? fmin(left_float, right_float)
                        : fmax(left_float, right_float);
  }
  value_free(&left);
  value_free(&right);
  push_stack(ctx->vm->stack, (VALUE_t){VALUE_float, {.f = result}});
  return nextop;
}

uint8_t *lc_math_min(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item) {
  return lc_math_select(ctx, nextop, item, true);
}

uint8_t *lc_math_max(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item) {
  return lc_math_select(ctx, nextop, item, false);
}

uint8_t *lc_math_sqrt(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item) {
  VALUE_t value;
  double input;
  double result;
  (void)item;

  value = pop_stack(ctx->vm->stack);
  if (value.type != VALUE_int && value.type != VALUE_float) {
    value_free(&value);
    return lc_invalid_args_detail_return(
        ctx, nextop, VALUE_NIL, "math.sqrt expects an integer or float");
  }

  input = value.type == VALUE_int ? (double)value.i : value.f;
  value_free(&value);
  if (input < 0.0) {
    return lc_invalid_args_detail_return(
        ctx, nextop, VALUE_NIL, "math.sqrt expects a non-negative number");
  }
  if (!isfinite(input)) {
    return lc_math_undefined_return(ctx, nextop);
  }

  result = sqrt(input);
  if (!isfinite(result)) {
    return lc_math_undefined_return(ctx, nextop);
  }
  push_stack(ctx->vm->stack, (VALUE_t){VALUE_float, {.f = result}});
  return nextop;
}

uint8_t *lc_math_pow(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item) {
  VALUE_t exponent = pop_stack(ctx->vm->stack);
  VALUE_t base = pop_stack(ctx->vm->stack);
  double base_float;
  double exponent_float;
  double result;
  (void)item;

  if ((base.type != VALUE_int && base.type != VALUE_float) ||
      (exponent.type != VALUE_int && exponent.type != VALUE_float)) {
    value_free(&base);
    value_free(&exponent);
    return lc_invalid_args_detail_return(
        ctx, nextop, VALUE_NIL,
        "math.pow expects two integer or float arguments");
  }

  base_float = base.type == VALUE_int ? (double)base.i : base.f;
  exponent_float =
      exponent.type == VALUE_int ? (double)exponent.i : exponent.f;
  value_free(&base);
  value_free(&exponent);
  if (!isfinite(base_float) || !isfinite(exponent_float)) {
    return lc_math_undefined_return(ctx, nextop);
  }

  result = pow(base_float, exponent_float);
  if (!isfinite(result)) {
    return lc_math_undefined_return(ctx, nextop);
  }
  push_stack(ctx->vm->stack, (VALUE_t){VALUE_float, {.f = result}});
  return nextop;
}
