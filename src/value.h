// Values are things.
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
extern VALUE_t VALUE_ERROR;
#endif

VALUE_t convert_to_bool(VALUE_t from);
