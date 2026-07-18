// Runtime value/string ownership helpers

// Licensed under the MIT License - see LICENSE file for details.

#pragma once

#include "value.h"

void free_runtime_string(char *s);
VALUE_t concat_two_strings(VALUE_t left, VALUE_t right);

/* Test-only visibility into the bounded lifetime of tracked buffers. */
size_t strbuf_tracked_count_for_tests(void);
