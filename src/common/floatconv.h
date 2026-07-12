#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

bool sin_parse_binary64(const char *text, double *out, char **errdetail);
bool sin_parse_binary64_bits(const char *text, uint64_t *out_bits, char **errdetail);

/*
 * Locale-stable binary64 formatting for logs and runtime string conversion.
 * Finite results always include a decimal marker (for example, 1.0).
 * The implementation prefers short libc-generated spellings that round-trip and
 * falls back to DBL_DECIMAL_DIG precision when needed.  C99 requires decimal
 * input conversion to recover the same binary64 value from DBL_DECIMAL_DIG
 * significant digits, so this is stable even where Ryū/Grisu is unavailable.
 */
char *sin_format_binary64(double value);
bool sin_format_binary64_buf(double value, char *buf, size_t cap);
