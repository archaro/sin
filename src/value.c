// Value things

// Licensed under the MIT License - see LICENSE file for details.

#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#define VALUE_INTERNAL
#include "value.h"

const VALUE_t VALUE_NIL = {VALUE_nil, {0}};
const VALUE_t VALUE_TRUE = {VALUE_bool, {1}};
const VALUE_t VALUE_FALSE = {VALUE_bool, {0}};
const VALUE_t VALUE_ZERO = {VALUE_int, {0}};

const char *value_type_name(VALUE_e type) {
  switch (type) {
    case VALUE_int: return "int";
    case VALUE_float: return "float";
    case VALUE_str: return "str";
    case VALUE_nil: return "nil";
    case VALUE_bool: return "bool";
  }
  return "unknown";
}

bool value_is_type(const VALUE_t *value, VALUE_e type) {
  return value != NULL && value->type == type;
}

void value_free(VALUE_t *value) {
  if (!value) return;
  if (value->type == VALUE_str && value->s) {
    free(value->s);
  }
  value->type = VALUE_nil;
  value->i = 0;
}

VALUE_t value_clone(const VALUE_t *value) {
  if (!value) return VALUE_NIL;
  VALUE_t out = *value;
  if (value->type == VALUE_str) {
    out.s = value->s ? strdup(value->s) : strdup("");
  }
  return out;
}

void value_move(VALUE_t *dst, VALUE_t *src) {
  if (!dst || !src || dst == src) return;
  value_free(dst);
  *dst = *src;
  src->type = VALUE_nil;
  src->i = 0;
}

void value_replace(VALUE_t *dst, VALUE_t src) {
  if (!dst) {
    value_free(&src);
    return;
  }
  value_free(dst);
  *dst = src;
}

int value_is_truthy(const VALUE_t *value) {
  if (!value) return 0;
  switch (value->type) {
    case VALUE_bool:
    case VALUE_int:
      return value->i != 0;
    case VALUE_float:
      return value->f_bits != 0;
    case VALUE_str:
      return value->s && value->s[0] != '\0';
    case VALUE_nil:
    default:
      return 0;
  }
}

void value_to_bool_inplace(VALUE_t *value) {
  int truthy = value_is_truthy(value);
  value_free(value);
  value->type = VALUE_bool;
  value->i = truthy;
}

VALUE_t convert_to_bool(VALUE_t from) {
  value_to_bool_inplace(&from);
  return from;
}

bool value_equal(const VALUE_t *left, const VALUE_t *right) {
  if (!left || !right || left->type != right->type) return false;
  switch (left->type) {
    case VALUE_int:
    case VALUE_bool:
      return left->i == right->i;
    case VALUE_float:
      return left->f_bits == right->f_bits;
    case VALUE_str:
      return strcmp(left->s ? left->s : "", right->s ? right->s : "") == 0;
    case VALUE_nil:
      return true;
  }
  return false;
}

bool value_not_equal(const VALUE_t *left, const VALUE_t *right) {
  // Preserve the VM quirk: mismatched types are not equal, so != is true.
  return !value_equal(left, right);
}

static bool value_order_matches(const VALUE_t *left, const VALUE_t *right,
                                bool (*predicate)(int comparison)) {
  int comparison = 0;
  if (!predicate || !value_order(left, right, &comparison)) return false;
  return predicate(comparison);
}

static bool comparison_lt(int comparison) { return comparison < 0; }
static bool comparison_lte(int comparison) { return comparison <= 0; }
static bool comparison_gt(int comparison) { return comparison > 0; }
static bool comparison_gte(int comparison) { return comparison >= 0; }

bool value_less_than(const VALUE_t *left, const VALUE_t *right) {
  return value_order_matches(left, right, comparison_lt);
}

bool value_less_equal(const VALUE_t *left, const VALUE_t *right) {
  return value_order_matches(left, right, comparison_lte);
}

bool value_greater_than(const VALUE_t *left, const VALUE_t *right) {
  return value_order_matches(left, right, comparison_gt);
}

bool value_greater_equal(const VALUE_t *left, const VALUE_t *right) {
  return value_order_matches(left, right, comparison_gte);
}

bool value_is_numeric(const VALUE_t *value) {
  return value_numeric_kind(value) != VALUE_NUMERIC_NONE;
}

VALUE_numeric_kind_e value_numeric_kind(const VALUE_t *value) {
  if (!value) return VALUE_NUMERIC_NONE;
  switch (value->type) {
    case VALUE_int:
      return VALUE_NUMERIC_INT;
    default:
      return VALUE_NUMERIC_NONE;
  }
}

static bool value_as_add_int_operand(const VALUE_t *value, int64_t *out) {
  if (!value || !out) return false;
  if (value->type == VALUE_nil) {
    *out = 0;
    return true;
  }
  if (value->type == VALUE_int) {
    *out = value->i;
    return true;
  }
  return false;
}

static bool value_as_int_operand(const VALUE_t *value, int64_t *out) {
  if (!value || !out || value->type != VALUE_int) return false;
  *out = value->i;
  return true;
}

bool value_add(const VALUE_t *left, const VALUE_t *right, VALUE_t *result) {
  int64_t lhs;
  int64_t rhs;
  if (!result || !value_as_add_int_operand(left, &lhs) ||
      !value_as_add_int_operand(right, &rhs)) {
    if (result) *result = VALUE_NIL;
    return false;
  }
  result->type = VALUE_int;
  result->i = lhs + rhs;
  return true;
}

bool value_sub(const VALUE_t *left, const VALUE_t *right, VALUE_t *result) {
  int64_t lhs;
  int64_t rhs;
  if (!result || !value_as_int_operand(left, &lhs) ||
      !value_as_int_operand(right, &rhs)) {
    if (result) *result = VALUE_NIL;
    return false;
  }
  result->type = VALUE_int;
  result->i = lhs - rhs;
  return true;
}

bool value_mul(const VALUE_t *left, const VALUE_t *right, VALUE_t *result) {
  int64_t lhs;
  int64_t rhs;
  if (!result || !value_as_int_operand(left, &lhs) ||
      !value_as_int_operand(right, &rhs)) {
    if (result) *result = VALUE_NIL;
    return false;
  }
  result->type = VALUE_int;
  result->i = lhs * rhs;
  return true;
}

bool value_div(const VALUE_t *left, const VALUE_t *right, VALUE_t *result) {
  int64_t lhs;
  int64_t rhs;
  if (!result) return false;
  if (!value_as_int_operand(left, &lhs) || !value_as_int_operand(right, &rhs)) {
    *result = VALUE_ZERO;
    return false;
  }
  result->type = VALUE_int;
  result->i = rhs == 0 ? 0 : lhs / rhs;
  return true;
}

bool value_neg(VALUE_t *value) {
  if (!value || value->type != VALUE_int) return false;
  value->i = -value->i;
  return true;
}

bool value_order(const VALUE_t *left, const VALUE_t *right, int *comparison) {
  if (!left || !right || left->type != right->type) return false;
  switch (left->type) {
    case VALUE_int:
    case VALUE_bool:
      *comparison = (left->i > right->i) - (left->i < right->i);
      return true;
    default:
      return false;
  }
}

const char *value_debug_string(const VALUE_t *value, char *buffer, size_t buffer_size) {
  if (!buffer || buffer_size == 0) return "";
  if (!value) {
    snprintf(buffer, buffer_size, "<null>");
    return buffer;
  }
  switch (value->type) {
    case VALUE_int:
      snprintf(buffer, buffer_size, "%ld", value->i);
      break;
    case VALUE_bool:
      snprintf(buffer, buffer_size, "%s", value->i ? "true" : "false");
      break;
    case VALUE_float:
      snprintf(buffer, buffer_size, "0x%016llx", (unsigned long long)value->f_bits);
      break;
    case VALUE_str:
      snprintf(buffer, buffer_size, "'%s'", value->s ? value->s : "");
      break;
    case VALUE_nil:
      snprintf(buffer, buffer_size, "nil");
      break;
    default:
      snprintf(buffer, buffer_size, "<%s>", value_type_name(value->type));
      break;
  }
  return buffer;
}
