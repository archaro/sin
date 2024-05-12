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

VALUE_t convert_to_bool(VALUE_t from) {
  // This function takes a VALUE of any type and returns a VALUE_bool
  // which is sensibly true or false.
  // NOTE: If from.type == VALUE_str, this function also frees from.s

  switch (from.type) {
    case VALUE_bool:
      // This is already a bool.  Return it.
      return from;
    case VALUE_int:
      // Non-zero ints are true.
      if (from.i == 0) return VALUE_FALSE; else return VALUE_TRUE;
    case VALUE_str:
      // All strings are true.
      free(from.s);
      return VALUE_TRUE;
    default:
      // If in doubt, it ain't true.
      // Also applies to VALUE_nil.
      return VALUE_FALSE;
  }
}

