// Values are things.

// Licensed under the MIT License - see LICENSE file for details.

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

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

#define FREE_STR(val) value_free(&(val))
  
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


const char *value_type_name(VALUE_e type);
const char *value_debug_string(const VALUE_t *value, char *buffer, size_t buffer_size);
bool value_is_type(const VALUE_t *value, VALUE_e type);
void value_free(VALUE_t *value);
VALUE_t value_clone(const VALUE_t *value);
void value_move(VALUE_t *dst, VALUE_t *src);
void value_replace(VALUE_t *dst, VALUE_t src);
bool value_equal(const VALUE_t *left, const VALUE_t *right);
bool value_order(const VALUE_t *left, const VALUE_t *right, int *comparison);
