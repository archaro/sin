// Runtime value/string ownership helpers

// Licensed under the MIT License - see LICENSE file for details.

#pragma once

#include "value.h"

// Runtime string tracking and runtime VALUE ownership follow the serialized
// single-thread contract; these helpers are not safe for concurrent calls.
void free_runtime_string(char *s);
VALUE_t concat_two_strings(VALUE_t left, VALUE_t right);

/* Test-only visibility into the bounded lifetime and hash probe work of
 * tracked buffers.  These remain runtime-internal helpers, not part of
 * value.h. */
typedef struct {
  size_t find_calls;
  size_t find_nodes;
  size_t forget_calls;
  size_t forget_nodes;
} strbuf_probe_t;

size_t strbuf_tracked_count_for_tests(void);
size_t strbuf_registry_capacity_for_tests(void);
size_t strbuf_capacity_for_tests(char *ptr);
size_t strbuf_bucket_for_tests(char *ptr, size_t capacity);
void strbuf_forget_for_tests(char *ptr);
void strbuf_track_for_tests(char *ptr, size_t cap);
void strbuf_fail_metadata_allocations_for_tests(int fail);
strbuf_probe_t strbuf_probe_for_tests(void);
void strbuf_probe_reset_for_tests(void);
