// Value things

// Licensed under the MIT License - see LICENSE file for details.

#include <stddef.h>
#include <malloc.h>

#define VALUE_INTERNAL
#include "value.h"

const VALUE_t VALUE_NIL = {VALUE_nil, {0}};
const VALUE_t VALUE_TRUE = {VALUE_bool, {1}};
const VALUE_t VALUE_FALSE = {VALUE_bool, {0}};
const VALUE_t VALUE_ZERO = {VALUE_int, {0}};

int value_is_truthy(const VALUE_t *value) {
  switch (value->type) {
    case VALUE_bool:
    case VALUE_int:
      return value->i != 0;
    case VALUE_str:
      return value->s[0] != '\0';
    case VALUE_nil:
    default:
      return 0;
  }
}

void value_to_bool_inplace(VALUE_t *value) {
  int truthy = value_is_truthy(value);

  if (value->type == VALUE_str) {
    free(value->s);
  }

  value->type = VALUE_bool;
  value->i = truthy;
}

VALUE_t convert_to_bool(VALUE_t from) {
  // Backward-compatible wrapper around in-place conversion.
  value_to_bool_inplace(&from);
  return from;
}
