// Value things

#include <stddef.h>
#include <malloc.h>

#define VALUE_INTERNAL
#include "value.h"

const VALUE_t VALUE_NIL = {VALUE_nil, {0}};

VALUE_t convert_to_bool(VALUE_t from) {
  // This function takes a VALUE of any type and returns a VALUE_bool
  // which is sensibly true or false.
  // NOTE: If from.type == VALUE_str, this function also frees from.s

  const VALUE_t t = {VALUE_bool, {1}};
  const VALUE_t f = {VALUE_bool, {0}};

  switch (from.type) {
    case VALUE_bool:
      // This is already a bool.  Return it.
      return from;
    case VALUE_int:
      // Non-zero ints are true.
      if (from.i == 0) return f; else return t;
    case VALUE_str:
      // All strings are true.
      free(from.s);
      return t;
    default:
      // If in doubt, it ain't true.
      // Also applies to VALUE_nil.
      return f;
  }
}

