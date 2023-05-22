// Values are things.
#pragma once

#include <stdint.h>

typedef enum {VALUE_int, VALUE_str, VALUE_nil} VALUE_e;

typedef struct {
  VALUE_e type; // What sort of value am I?
  union {
    int64_t i;  // This is an integer value
    char *s; // This is a char* value
  };
} VALUE_t;

#ifndef VALUE_INTERNAL
extern VALUE_t VALUE_NIL;
#endif
