#pragma once

#include <stddef.h>
#include <stdint.h>

/*
 * Sinistra string values are byte strings. Keep runtime strings small enough
 * for interactive MUD use. This is the single source of truth for the maximum
 * string value and source string-literal payload size.
 */
#define SIN_MAX_STRING_BYTES ((size_t)UINT16_MAX)

_Static_assert(SIN_MAX_STRING_BYTES <= (size_t)UINT16_MAX,
               "SIN_MAX_STRING_BYTES must fit the bytecode string operand");
