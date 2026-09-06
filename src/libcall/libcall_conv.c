// Value conversion libcalls.

// Licensed under the MIT License - see LICENSE file for details.

#include <math.h>
#include <stdint.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include "floatconv.h"
#include "libcall_common.h"
#include "libcall_handlers.h"
#include "stack.h"

static bool conv_int_parse_string(const char *text, int64_t *out) {
  const unsigned char *p = (const unsigned char *)text;
  bool negative = false;
  uint64_t magnitude = 0;
  uint64_t limit;

  if (!text || *text == '\0') return false;
  if (*p == '+' || *p == '-') {
    negative = *p == '-';
    p++;
  }
  if (*p < '0' || *p > '9') return false;

  limit = negative ? (UINT64_C(1) << 63) : (uint64_t)INT64_MAX;
  for (; *p != '\0'; p++) {
    uint64_t digit;
    if (*p < '0' || *p > '9') return false;
    digit = (uint64_t)(*p - '0');
    if (magnitude > (limit - digit) / 10u) return false;
    magnitude = magnitude * 10u + digit;
  }

  if (negative) {
    if (magnitude == (UINT64_C(1) << 63)) {
      *out = INT64_MIN;
    } else {
      *out = -(int64_t)magnitude;
    }
  } else {
    *out = (int64_t)magnitude;
  }
  return true;
}

static bool conv_float_integer_string(const char *text) {
  const unsigned char *p = (const unsigned char *)text;

  if (!text || *text == '\0') return false;
  if (*p == '+' || *p == '-') p++;
  if (*p < '0' || *p > '9') return false;
  for (p++; *p != '\0'; p++) {
    if (*p < '0' || *p > '9') return false;
  }
  return true;
}

static bool conv_float_parse_string(const char *text, double *out) {
  char *normalized = NULL;
  const char *literal = text;
  uint64_t bits = 0;
  bool parsed;

  if (conv_float_integer_string(text)) {
    size_t length = strlen(text);
    if (length > SIZE_MAX - 3u) return false;
    normalized = malloc(length + 3u);
    if (!normalized) return false;
    memcpy(normalized, text, length);
    memcpy(normalized + length, ".0", 3u);
    literal = normalized;
  }

  parsed = sin_parse_binary64_bits(literal, &bits, NULL);
  free(normalized);
  if (!parsed) return false;

  memcpy(out, &bits, sizeof(*out));
  return isfinite(*out);
}

uint8_t *lc_conv_bool(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item) {
  (void)item;
  VALUE_t value = pop_stack(ctx->vm->stack);
  bool truthy = value_is_truthy(&value) != 0;
  value_free(&value);
  push_stack(ctx->vm->stack, truthy ? VALUE_TRUE : VALUE_FALSE);
  return nextop;
}

uint8_t *lc_conv_float(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item) {
  (void)item;
  VALUE_t value = pop_stack(ctx->vm->stack);
  double result;

  switch (value.type) {
    case VALUE_float:
      push_stack(ctx->vm->stack, value);
      return nextop;
    case VALUE_int:
      result = (double)value.i;
      value_free(&value);
      break;
    case VALUE_bool:
      result = value.i ? 1.0 : 0.0;
      value_free(&value);
      break;
    case VALUE_str:
      if (!conv_float_parse_string(value.s, &result)) {
        value_free(&value);
        return lc_invalid_args_nil_return(ctx, nextop,
            "conv.float expects a valid finite float string");
      }
      value_free(&value);
      break;
    case VALUE_nil:
      result = 0.0;
      value_free(&value);
      break;
    default:
      value_free(&value);
      return lc_invalid_args_nil_return(ctx, nextop,
          "conv.float expects an integer, float, boolean, string, or nil");
  }

  push_stack(ctx->vm->stack, (VALUE_t){VALUE_float, {.f = result}});
  return nextop;
}

uint8_t *lc_conv_int(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item) {
  (void)item;
  VALUE_t value = pop_stack(ctx->vm->stack);
  int64_t result;

  switch (value.type) {
    case VALUE_int:
      push_stack(ctx->vm->stack, value);
      return nextop;
    case VALUE_float:
      if (!isfinite(value.f) || value.f < -0x1p63 || value.f >= 0x1p63) {
        value_free(&value);
        return lc_invalid_args_nil_return(ctx, nextop,
            "conv.int expects a finite float in the signed integer range");
      }
      result = (int64_t)trunc(value.f);
      value_free(&value);
      break;
    case VALUE_bool:
      result = value.i ? 1 : 0;
      value_free(&value);
      break;
    case VALUE_str:
      if (!conv_int_parse_string(value.s, &result)) {
        value_free(&value);
        return lc_invalid_args_nil_return(ctx, nextop,
            "conv.int expects a valid integer string");
      }
      value_free(&value);
      break;
    case VALUE_nil:
      value_free(&value);
      result = 0;
      break;
    default:
      value_free(&value);
      return lc_invalid_args_nil_return(ctx, nextop,
          "conv.int expects an integer, float, boolean, string, or nil");
  }

  push_stack(ctx->vm->stack, (VALUE_t){VALUE_int, {.i = result}});
  return nextop;
}
