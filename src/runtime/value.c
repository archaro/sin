// Value things

// Licensed under the MIT License - see LICENSE file for details.

#include <math.h>
#include <stddef.h>
#include <inttypes.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#define VALUE_INTERNAL
#include "value.h"
#include "floatconv.h"
#include "runtime_value.h"
#include "string_limits.h"
#include "itemref.h"
#include "list.h"
#include "memory.h"
#include "strbuilder.h"

const VALUE_t VALUE_NIL = {.type = VALUE_nil, .i = 0};
const VALUE_t VALUE_TRUE = {.type = VALUE_bool, .i = 1};
const VALUE_t VALUE_FALSE = {.type = VALUE_bool, .i = 0};
const VALUE_t VALUE_ZERO = {.type = VALUE_int, .i = 0};

static VALUE_text_result_e value_render_append(
    SIN_STRBUILDER_t *sb, const VALUE_t *value,
    VALUE_text_nil_policy_e nil_policy, size_t depth, bool aggregate);
static bool value_render_string(SIN_STRBUILDER_t *sb, const char *s,
                                bool quoted) {
  if (!s) s = "";
  if (quoted && !sin_sb_append_bytes(sb, "\"", 1)) return false;
  for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
    unsigned char c = *p;
    if (!quoted && !sin_sb_append_bytes(sb, (const char *)p, 1)) return false;
    if (!quoted) continue;
    const char *esc = NULL;
    switch (c) {
      case '"': esc = "\\\""; break;
      case '\\': esc = "\\\\"; break;
      case '\n': esc = "\\n"; break;
      case '\t': esc = "\\t"; break;
      case '\r': esc = "\\r"; break;
      case '\b': esc = "\\b"; break;
      case '\f': esc = "\\f"; break;
    }
    if (esc) {
      if (!sin_sb_append_cstr(sb, esc)) return false;
    } else if (c < 0x20u || c == 0x7fu) {
      char hex[5];
      (void)snprintf(hex, sizeof(hex), "\\x%02X", c);
      if (!sin_sb_append_bytes(sb, hex, 4)) return false;
    } else if (!sin_sb_append_bytes(sb, (const char *)p, 1)) return false;
  }
  return !quoted || sin_sb_append_bytes(sb, "\"", 1);
}
static VALUE_text_result_e value_render_append(
    SIN_STRBUILDER_t *sb, const VALUE_t *value,
    VALUE_text_nil_policy_e nil_policy, size_t depth, bool aggregate) {
  if (!value) return VALUE_TEXT_MALFORMED;
  char formatted[128];
  switch (value->type) {
    case VALUE_str:
      return value_render_string(sb, value->s, aggregate)
          ? VALUE_TEXT_OK : VALUE_TEXT_FORMAT_ERROR;
    case VALUE_nil:
      if (!aggregate && nil_policy == VALUE_TEXT_NIL_OMIT)
        return VALUE_TEXT_OK;
      return sin_sb_append_cstr(sb, "nil")
          ? VALUE_TEXT_OK : VALUE_TEXT_FORMAT_ERROR;
    case VALUE_int:
      (void)snprintf(formatted, sizeof(formatted), "%" PRId64, value->i);
      return sin_sb_append_cstr(sb, formatted)
          ? VALUE_TEXT_OK : VALUE_TEXT_FORMAT_ERROR;
    case VALUE_bool:
      return sin_sb_append_cstr(sb, value->i ? "true" : "false")
          ? VALUE_TEXT_OK : VALUE_TEXT_FORMAT_ERROR;
    case VALUE_float:
      if (!sin_format_binary64_buf(value->f, formatted, sizeof(formatted))) {
        return VALUE_TEXT_FORMAT_ERROR;
      }
      return sin_sb_append_cstr(sb, formatted)
          ? VALUE_TEXT_OK : VALUE_TEXT_FORMAT_ERROR;
    case VALUE_itemref: {
      const char *path = value->itemref ? sin_itemref_path(value->itemref) : NULL;
      if (!path) return VALUE_TEXT_MALFORMED;
      return sin_sb_append_cstr(sb, "&") && sin_sb_append_cstr(sb, path)
          ? VALUE_TEXT_OK : VALUE_TEXT_FORMAT_ERROR;
    }
    case VALUE_list: {
      if (!value->list || depth >= SIN_LIST_MAX_DEPTH)
        return VALUE_TEXT_MALFORMED;
      if (!sin_sb_append_cstr(sb, "#[")) return VALUE_TEXT_FORMAT_ERROR;
      size_t count = sin_list_count(value->list);
      for (size_t i = 0; i < count; i++) {
        const VALUE_t *elem = sin_list_get(value->list, i);
        if (!elem) return VALUE_TEXT_MALFORMED;
        if (i && !sin_sb_append_cstr(sb, ", ")) return VALUE_TEXT_FORMAT_ERROR;
        VALUE_text_result_e child = value_render_append(
            sb, elem, VALUE_TEXT_NIL_LITERAL, depth + 1u, true);
        if (child != VALUE_TEXT_OK) return child;
      }
      return sin_sb_append_cstr(sb, "]")
          ? VALUE_TEXT_OK : VALUE_TEXT_FORMAT_ERROR;
    }
    default: return VALUE_TEXT_UNKNOWN_TYPE;
  }
}
VALUE_text_result_e value_render_text(const VALUE_t *value,
                                      VALUE_text_nil_policy_e nil_policy,
                                      char **text, size_t *text_length) {
  if (!text || !text_length) return VALUE_TEXT_FORMAT_ERROR;
  *text = NULL;
  *text_length = 0;
  if (!value ||
      (nil_policy != VALUE_TEXT_NIL_OMIT &&
       nil_policy != VALUE_TEXT_NIL_LITERAL))
    return VALUE_TEXT_FORMAT_ERROR;
  if (value->type == VALUE_nil && nil_policy == VALUE_TEXT_NIL_OMIT)
    return VALUE_TEXT_NIL;
  SIN_STRBUILDER_t sb;
  if (!sin_sb_init(&sb, 64u, SIN_MAX_STRING_BYTES))
    return VALUE_TEXT_ALLOCATION_ERROR;
  VALUE_text_result_e render = value_render_append(
      &sb, value, nil_policy, 0, false);
  if (render != VALUE_TEXT_OK) {
    VALUE_text_result_e result = render;
    if (render == VALUE_TEXT_FORMAT_ERROR) {
      if (sb.limit_exceeded) result = VALUE_TEXT_OUTPUT_LIMIT;
      else if (sb.failed) result = VALUE_TEXT_ALLOCATION_ERROR;
    }
    sin_sb_dispose(&sb);
    return result;
  }
  *text = sin_sb_take(&sb);
  *text_length = strlen(*text);
  return VALUE_TEXT_OK;
}

uint64_t value_float_to_bits(double value) {
  uint64_t bits;
  memcpy(&bits, &value, sizeof(bits));
  return bits;
}

double value_float_from_bits(uint64_t bits) {
  double value;
  memcpy(&value, &bits, sizeof(value));
  return value;
}

const char *value_type_name(VALUE_e type) {
  switch (type) {
    case VALUE_int: return "int";
    case VALUE_float: return "float";
    case VALUE_str: return "str";
    case VALUE_nil: return "nil";
    case VALUE_bool: return "bool";
    case VALUE_itemref: return "itemref";
    case VALUE_list: return "list";
  }
  return "unknown";
}

static VALUE_text_result_e value_plain_text_copy(const char *source,
                                                 char *buffer,
                                                 size_t buffer_size,
                                                 const char **text,
                                                 size_t *text_length) {
  size_t length = strlen(source);
  if (length == SIZE_MAX || buffer == NULL || buffer_size <= length) {
    return VALUE_TEXT_BUFFER_TOO_SMALL;
  }
  memcpy(buffer, source, length + 1);
  *text = buffer;
  *text_length = length;
  return VALUE_TEXT_OK;
}

VALUE_text_result_e value_plain_text(const VALUE_t *value,
                                     VALUE_text_nil_policy_e nil_policy,
                                     char *buffer, size_t buffer_size,
                                     const char **text, size_t *text_length) {
  char formatted[VALUE_PLAIN_TEXT_BUFFER_SIZE];
  int written;

  if (!text || !text_length) return VALUE_TEXT_FORMAT_ERROR;
  *text = NULL;
  *text_length = 0;
  if (!value) return VALUE_TEXT_FORMAT_ERROR;

  switch (value->type) {
    case VALUE_str:
      *text = value->s ? value->s : "";
      *text_length = strlen(*text);
      return VALUE_TEXT_OK;
    case VALUE_itemref: {
      const char *path = sin_itemref_path(value->itemref);
      size_t length = sin_itemref_path_length(value->itemref);
      if (!path || length > SIZE_MAX - 2u || buffer == NULL || buffer_size <= length + 1u)
        return VALUE_TEXT_BUFFER_TOO_SMALL;
      buffer[0] = '&';
      memcpy(buffer + 1u, path, length + 1u);
      *text = buffer;
      *text_length = length + 1u;
      return VALUE_TEXT_OK;
    }
    case VALUE_nil:
      if (nil_policy == VALUE_TEXT_NIL_OMIT) return VALUE_TEXT_NIL;
      if (nil_policy != VALUE_TEXT_NIL_LITERAL) return VALUE_TEXT_FORMAT_ERROR;
      return value_plain_text_copy("nil", buffer, buffer_size, text,
                                   text_length);
    case VALUE_list:
      return VALUE_TEXT_UNKNOWN_TYPE;
    case VALUE_int:
      written = snprintf(formatted, sizeof(formatted), "%" PRId64, value->i);
      break;
    case VALUE_float:
      if (!sin_format_binary64_buf(value->f, formatted, sizeof(formatted))) {
        return VALUE_TEXT_FORMAT_ERROR;
      }
      written = (int)strlen(formatted);
      break;
    case VALUE_bool:
      written = snprintf(formatted, sizeof(formatted), "%s",
                         value->i ? "true" : "false");
      break;
    default:
      return VALUE_TEXT_UNKNOWN_TYPE;
  }

  if (written < 0) return VALUE_TEXT_FORMAT_ERROR;
  if ((size_t)written >= sizeof(formatted)) return VALUE_TEXT_FORMAT_ERROR;
  return value_plain_text_copy(formatted, buffer, buffer_size, text,
                               text_length);
}

bool value_is_type(const VALUE_t *value, VALUE_e type) {
  return value != NULL && value->type == type;
}

void value_free(VALUE_t *value) {
  if (!value) return;
  if (value->type == VALUE_str && value->s) {
    free_runtime_string(value->s);
  } else if (value->type == VALUE_itemref) {
    sin_itemref_release(value->itemref);
  } else if (value->type == VALUE_list) {
    sin_list_release(value->list);
  }
  value->type = VALUE_nil;
  value->i = 0;
}

bool value_string_within_limit(const VALUE_t *value) {
  return value == NULL || value->type != VALUE_str ||
      strlen(value->s ? value->s : "") <= SIN_MAX_STRING_BYTES;
}

bool value_clone_fallible(const VALUE_t *value, VALUE_t *out) {
  if (!out) return false;
  *out = VALUE_NIL;
  if (!value) return false;
  *out = *value;
  if (value->type == VALUE_str) {
    const char *source = value->s ? value->s : "";
    size_t length = strlen(source);
    if (!value_string_within_limit(value) || length == SIZE_MAX) {
      *out = VALUE_NIL;
      return false;
    }
    out->s = alloc_malloc(length + 1u);
    if (!out->s) {
      *out = VALUE_NIL;
      return false;
    }
    memcpy(out->s, source, length + 1u);
  } else if (value->type == VALUE_itemref) {
    out->itemref = sin_itemref_retain(value->itemref);
    if (value->itemref && !out->itemref) {
      *out = VALUE_NIL;
      return false;
    }
  } else if (value->type == VALUE_list) {
    out->list = sin_list_retain(value->list);
    if (value->list && !out->list) {
      *out = VALUE_NIL;
      return false;
    }
  }
  return true;
}

VALUE_t value_clone(const VALUE_t *value) {
  VALUE_t out = VALUE_NIL;
  (void)value_clone_fallible(value, &out);
  return out;
}

void value_move(VALUE_t *dst, VALUE_t *src) {
  if (!dst || !src || dst == src) return;
  if (!value_string_within_limit(src)) {
    value_free(dst);
    value_free(src);
    return;
  }
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
  if (!value_string_within_limit(&src)) {
    value_free(dst);
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
      return value->f != 0.0;
    case VALUE_str:
      return value->s && value->s[0] != '\0';
    case VALUE_itemref:
      return 1;
    case VALUE_list:
      return value->list && sin_list_count(value->list) != 0;
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

static bool value_as_comparison_float(const VALUE_t *value, double *out) {
  if (!value || !out) return false;
  if (value->type == VALUE_int) {
    *out = (double)value->i;
    return true;
  }
  if (value->type == VALUE_float) {
    *out = value->f;
    return true;
  }
  return false;
}

bool value_equal(const VALUE_t *left, const VALUE_t *right) {
  if (!left || !right) return false;
  if (left->type == VALUE_float || right->type == VALUE_float) {
    double lhs;
    double rhs;
    return value_as_comparison_float(left, &lhs) &&
           value_as_comparison_float(right, &rhs) && lhs == rhs;
  }
  if (left->type != right->type) return false;
  switch (left->type) {
    case VALUE_int:
    case VALUE_bool:
      return left->i == right->i;
    case VALUE_str:
      return strcmp(left->s ? left->s : "", right->s ? right->s : "") == 0;
    case VALUE_itemref:
      if (!left->itemref || !right->itemref) return false;
      return strcmp(sin_itemref_path(left->itemref),
                    sin_itemref_path(right->itemref)) == 0;
    case VALUE_list:
      return sin_list_equal(left->list, right->list);
    case VALUE_nil:
      return true;
    case VALUE_float:
      return false;
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
    case VALUE_float:
      return VALUE_NUMERIC_FLOAT;
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

static bool value_as_numeric_operand(const VALUE_t *value, double *out) {
  if (!value || !out) return false;
  if (value->type == VALUE_int) {
    *out = (double)value->i;
    return true;
  }
  if (value->type == VALUE_float) {
    *out = value->f;
    return true;
  }
  return false;
}

static bool value_as_int_operand(const VALUE_t *value, int64_t *out) {
  if (!value || !out || value->type != VALUE_int) return false;
  *out = value->i;
  return true;
}

static bool value_arith_float(const VALUE_t *left, const VALUE_t *right,
                              VALUE_t *result, double (*op)(double, double)) {
  double lhs;
  double rhs;
  if (!result || !op || !value_as_numeric_operand(left, &lhs) ||
      !value_as_numeric_operand(right, &rhs)) {
    if (result) *result = VALUE_NIL;
    return false;
  }
  result->type = VALUE_float;
  result->f = op(lhs, rhs);
  return true;
}

static double float_add(double lhs, double rhs) { return lhs + rhs; }
static double float_sub(double lhs, double rhs) { return lhs - rhs; }
static double float_mul(double lhs, double rhs) { return lhs * rhs; }
static double float_div(double lhs, double rhs) { return lhs / rhs; }
static double float_mod(double lhs, double rhs) { return fmod(lhs, rhs); }

static bool value_int_add_overflows(int64_t lhs, int64_t rhs) {
  return (rhs > 0 && lhs > INT64_MAX - rhs) ||
         (rhs < 0 && lhs < INT64_MIN - rhs);
}

static bool value_int_sub_overflows(int64_t lhs, int64_t rhs) {
  return (rhs > 0 && lhs < INT64_MIN + rhs) ||
         (rhs < 0 && lhs > INT64_MAX + rhs);
}

static bool value_int_mul_overflows(int64_t lhs, int64_t rhs) {
  if (lhs == 0 || rhs == 0) return false;
  if (lhs == -1) return rhs == INT64_MIN;
  if (rhs == -1) return lhs == INT64_MIN;
  if (lhs > 0) {
    if (rhs > 0) return lhs > INT64_MAX / rhs;
    return rhs < INT64_MIN / lhs;
  }
  if (rhs > 0) return lhs < INT64_MIN / rhs;
  return lhs < INT64_MAX / rhs;
}

bool value_add(const VALUE_t *left, const VALUE_t *right, VALUE_t *result) {
  int64_t lhs;
  int64_t rhs;
  if (!result) return false;
  if (value_as_add_int_operand(left, &lhs) && value_as_add_int_operand(right, &rhs)) {
    if (value_int_add_overflows(lhs, rhs)) {
      *result = VALUE_NIL;
      return false;
    }
    result->type = VALUE_int;
    result->i = lhs + rhs;
    return true;
  }
  return value_arith_float(left, right, result, float_add);
}

bool value_sub(const VALUE_t *left, const VALUE_t *right, VALUE_t *result) {
  int64_t lhs;
  int64_t rhs;
  if (!result) return false;
  if (value_as_int_operand(left, &lhs) && value_as_int_operand(right, &rhs)) {
    if (value_int_sub_overflows(lhs, rhs)) {
      *result = VALUE_NIL;
      return false;
    }
    result->type = VALUE_int;
    result->i = lhs - rhs;
    return true;
  }
  return value_arith_float(left, right, result, float_sub);
}

bool value_mul(const VALUE_t *left, const VALUE_t *right, VALUE_t *result) {
  int64_t lhs;
  int64_t rhs;
  if (!result) return false;
  if (value_as_int_operand(left, &lhs) && value_as_int_operand(right, &rhs)) {
    if (value_int_mul_overflows(lhs, rhs)) {
      *result = VALUE_NIL;
      return false;
    }
    result->type = VALUE_int;
    result->i = lhs * rhs;
    return true;
  }
  return value_arith_float(left, right, result, float_mul);
}

bool value_div(const VALUE_t *left, const VALUE_t *right, VALUE_t *result) {
  int64_t lhs;
  int64_t rhs;
  if (!result) return false;
  if (value_as_int_operand(left, &lhs) && value_as_int_operand(right, &rhs)) {
    if (lhs == INT64_MIN && rhs == -1) {
      *result = VALUE_NIL;
      return false;
    }
    result->type = VALUE_int;
    result->i = rhs == 0 ? 0 : lhs / rhs;
    return true;
  }
  if ((left && left->type == VALUE_float) || (right && right->type == VALUE_float)) {
    return value_arith_float(left, right, result, float_div);
  }
  *result = VALUE_ZERO;
  return false;
}

bool value_mod(const VALUE_t *left, const VALUE_t *right, VALUE_t *result) {
  int64_t lhs;
  int64_t rhs;
  if (!result) return false;
  if (value_as_int_operand(left, &lhs) && value_as_int_operand(right, &rhs)) {
    if (rhs == 0) { *result = VALUE_NIL; return false; }
    result->type = VALUE_int;
    if (lhs == INT64_MIN && rhs == -1) { result->i = 0; return true; }
    result->i = lhs % rhs;
    return true;
  }
  if ((left && left->type == VALUE_float) || (right && right->type == VALUE_float)) {
    return value_arith_float(left, right, result, float_mod);
  }
  *result = VALUE_NIL;
  return false;
}

bool value_neg(VALUE_t *value) {
  if (!value) return false;
  if (value->type == VALUE_int) {
    if (value->i == INT64_MIN) {
      *value = VALUE_NIL;
      return false;
    }
    value->i = -value->i;
    return true;
  }
  if (value->type == VALUE_float) {
    value->f = -value->f;
    return true;
  }
  return false;
}

bool value_order(const VALUE_t *left, const VALUE_t *right, int *comparison) {
  if (!left || !right || !comparison) return false;
  if (left->type == VALUE_float || right->type == VALUE_float) {
    double lhs;
    double rhs;
    if (!value_as_comparison_float(left, &lhs) ||
        !value_as_comparison_float(right, &rhs) || isnan(lhs) || isnan(rhs)) {
      return false;
    }
    *comparison = (lhs > rhs) - (lhs < rhs);
    return true;
  }
  if (left->type != right->type) return false;
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
  if (value->type == VALUE_list || value->type == VALUE_itemref) {
    char *rendered = NULL;
    size_t length = 0;
    VALUE_text_result_e result = value_render_text(value, VALUE_TEXT_NIL_LITERAL,
                                                   &rendered, &length);
    const char *marker = "<value-render-error>";
    if (result == VALUE_TEXT_OK && length < buffer_size) {
      memcpy(buffer, rendered, length + 1u);
      free(rendered);
      return buffer;
    }
    if (result == VALUE_TEXT_OK) marker = "<trunc>";
    (void)snprintf(buffer, buffer_size, "%s", marker);
    free(rendered);
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
      if (!sin_format_binary64_buf(value->f, buffer, buffer_size)) {
        snprintf(buffer, buffer_size, "<float-format-error>");
      }
      break;
    case VALUE_str:
      snprintf(buffer, buffer_size, "'%s'", value->s ? value->s : "");
      break;
    case VALUE_itemref:
      snprintf(buffer, buffer_size, "&%s", sin_itemref_path(value->itemref) ?
               sin_itemref_path(value->itemref) : "");
      break;
    case VALUE_list:
      snprintf(buffer, buffer_size, "<list:%zu>", sin_list_count(value->list));
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
