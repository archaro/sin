// Values are things.

// Licensed under the MIT License - see LICENSE file for details.

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <float.h>
#include <limits.h>

/*
 * Sin serializes and interprets VALUE_float as IEEE 754 binary64. Keep these
 * requirements at the value boundary so every user of VALUE_t gets an early
 * compile-time failure on unsupported floating-point targets.
 */
_Static_assert(CHAR_BIT == 8, "VALUE_float binary64 payloads require 8-bit bytes");
_Static_assert(sizeof(uint64_t) == 8, "VALUE_float binary64 payloads require 64-bit uint64_t");
_Static_assert(sizeof(double) == 8, "VALUE_float requires 64-bit double");
_Static_assert(FLT_RADIX == 2, "VALUE_float requires binary floating-point radix");
_Static_assert(DBL_MANT_DIG == 53, "VALUE_float requires IEEE 754 binary64 precision");
_Static_assert(DBL_MAX_EXP == 1024, "VALUE_float requires IEEE 754 binary64 exponent range");
_Static_assert(DBL_MIN_EXP == -1021, "VALUE_float requires IEEE 754 binary64 minimum exponent");

typedef enum { VALUE_int,
               VALUE_float,
               VALUE_str,
               VALUE_nil,
               VALUE_bool
             } VALUE_e;

typedef enum { VALUE_NUMERIC_NONE,
               VALUE_NUMERIC_INT,
               VALUE_NUMERIC_FLOAT
             } VALUE_numeric_kind_e;

typedef enum { VALUE_TEXT_OK,
               VALUE_TEXT_NIL,
               VALUE_TEXT_BUFFER_TOO_SMALL,
               VALUE_TEXT_FORMAT_ERROR,
               VALUE_TEXT_UNKNOWN_TYPE
             } VALUE_text_result_e;

typedef enum { VALUE_TEXT_NIL_OMIT,
               VALUE_TEXT_NIL_LITERAL
             } VALUE_text_nil_policy_e;

#define VALUE_PLAIN_TEXT_BUFFER_SIZE 64

typedef struct {
  VALUE_e type; // What sort of value am I?
  union {
    int64_t i;  // This is an integer value
    double f; // This is a floating-point value used for arithmetic
    uint64_t f_bits; // This is an IEEE 754 binary64 payload view
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
uint64_t value_float_to_bits(double value);
double value_float_from_bits(uint64_t bits);

// Truthiness contract for VM values:
// - nil is false
// - bool false/true preserve their value
// - int 0 is false; any non-zero int is true
// - float +0.0 and -0.0 are false; all other values are true.
//   NaN is truthy because it is not equal to zero.
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
/*
 * Return non-owning plain user-visible text for value. Strings borrow their
 * payload (a null payload is treated as empty text); all other text is written
 * to buffer. The input and any string payload are never consumed or mutated.
 * Nil is either omitted or rendered as "nil" according to nil_policy.
 */
VALUE_text_result_e value_plain_text(const VALUE_t *value,
                                     VALUE_text_nil_policy_e nil_policy,
                                     char *buffer, size_t buffer_size,
                                     const char **text, size_t *text_length);
bool value_is_type(const VALUE_t *value, VALUE_e type);
void value_free(VALUE_t *value);
bool value_string_within_limit(const VALUE_t *value);
VALUE_t value_clone(const VALUE_t *value);
void value_move(VALUE_t *dst, VALUE_t *src);
void value_replace(VALUE_t *dst, VALUE_t src);
bool value_equal(const VALUE_t *left, const VALUE_t *right);
bool value_not_equal(const VALUE_t *left, const VALUE_t *right);
bool value_less_than(const VALUE_t *left, const VALUE_t *right);
bool value_less_equal(const VALUE_t *left, const VALUE_t *right);
bool value_greater_than(const VALUE_t *left, const VALUE_t *right);
bool value_greater_equal(const VALUE_t *left, const VALUE_t *right);
bool value_is_numeric(const VALUE_t *value);
VALUE_numeric_kind_e value_numeric_kind(const VALUE_t *value);
// Arithmetic helpers support int and IEEE 754 binary64 float arithmetic.
// OP_ADD intentionally preserves the VM quirk that nil participates as
// integer 0. Signed integer overflow returns false and VALUE_NIL; integer
// divide by zero still succeeds with integer 0. value_div preserves the VM
// invalid-operand result of int 0.
bool value_add(const VALUE_t *left, const VALUE_t *right, VALUE_t *result);
bool value_sub(const VALUE_t *left, const VALUE_t *right, VALUE_t *result);
bool value_mul(const VALUE_t *left, const VALUE_t *right, VALUE_t *result);
bool value_div(const VALUE_t *left, const VALUE_t *right, VALUE_t *result);
bool value_mod(const VALUE_t *left, const VALUE_t *right, VALUE_t *result);
bool value_neg(VALUE_t *value);
bool value_order(const VALUE_t *left, const VALUE_t *right, int *comparison);
