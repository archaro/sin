// Runtime value/string ownership helpers

// Licensed under the MIT License - see LICENSE file for details.

#pragma once

#include "value.h"

void strbuf_forget(char *ptr);
void strbuf_track(char *ptr, size_t cap);
void free_runtime_string(char *s);
void value_free_runtime(VALUE_t *value);
VALUE_t concat_two_strings(VALUE_t left, VALUE_t right);
