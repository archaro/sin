#pragma once

#include <stddef.h>

/*
 * Sinistra string values are byte strings. Keep runtime strings small enough
 * for interactive MUD use and aligned with the u16 bytecode string operand.
 */
#define SIN_MAX_STRING_BYTES ((size_t)65535u)
