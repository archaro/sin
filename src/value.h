// Values are things.

// Licensed under the MIT License - see LICENSE file for details.

#pragma once

#include <stdint.h>

typedef enum { VALUE_int,
               VALUE_str,
               VALUE_nil,
               VALUE_bool
             } VALUE_e;

typedef struct {
  VALUE_e type; // What sort of value am I?
  union {
    int64_t i;  // This is an integer value
    char *s; // This is a string value
  };
} VALUE_t;

#ifndef VALUE_INTERNAL
extern VALUE_t VALUE_NIL;
extern VALUE_t VALUE_ZERO;
extern VALUE_t VALUE_TRUE;
extern VALUE_t VALUE_FALSE;
#endif

#define FREE_STR(val) \
  if ((val).type == VALUE_str) { \
    STRINGDEBUG_LOG("Freeing: %s\n", val.s); \
    FREE_ARRAY(char, (val).s, strlen((val).s) + 1); \
  }
  
VALUE_t convert_to_bool(VALUE_t from);

// Truthiness contract for VM values:
// - nil is false
// - bool false/true preserve their value
// - int 0 is false; any non-zero int is true
// - string "" (empty) is false; any non-empty string is true
//
// Ownership contract:
// - value_is_truthy() is read-only and never frees memory.
// - value_to_bool_inplace() mutates in place to VALUE_bool and frees string
//   storage if and only if the input value owns VALUE_str memory.
int value_is_truthy(const VALUE_t *value);
void value_to_bool_inplace(VALUE_t *value);
