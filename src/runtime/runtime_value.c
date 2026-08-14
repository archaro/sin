// Runtime value/string ownership helpers

// Licensed under the MIT License - see LICENSE file for details.

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "runtime_value.h"
#include "string_limits.h"

typedef struct strbuf_meta {
  char *ptr;
  size_t cap;
  struct strbuf_meta *next;
} strbuf_meta_t;

static strbuf_meta_t *strbuf_head = NULL;
static strbuf_probe_t strbuf_probe;

static strbuf_meta_t *strbuf_find(char *ptr) {
  strbuf_probe.find_calls++;
  strbuf_meta_t *meta = strbuf_head;
  while (meta) {
    strbuf_probe.find_nodes++;
    if (meta->ptr == ptr) return meta;
    meta = meta->next;
  }
  return NULL;
}

static void strbuf_forget(char *ptr) {
  strbuf_probe.forget_calls++;
  strbuf_meta_t **scan = &strbuf_head;
  while (*scan) {
    strbuf_probe.forget_nodes++;
    if ((*scan)->ptr == ptr) {
      strbuf_meta_t *found = *scan;
      *scan = found->next;
      free(found);
      return;
    }
    scan = &((*scan)->next);
  }
}

static void strbuf_track(char *ptr, size_t cap) {
  strbuf_meta_t *meta = strbuf_find(ptr);
  if (!meta) {
    meta = malloc(sizeof(strbuf_meta_t));
    if (!meta) return;
    meta->next = strbuf_head;
    strbuf_head = meta;
  }
  meta->ptr = ptr;
  meta->cap = cap;
}

size_t strbuf_tracked_count_for_tests(void) {
  size_t count = 0;
  for (strbuf_meta_t *meta = strbuf_head; meta; meta = meta->next) {
    count++;
  }
  return count;
}

size_t strbuf_capacity_for_tests(char *ptr) {
  strbuf_meta_t *meta = strbuf_find(ptr);
  return meta ? meta->cap : 0;
}

void strbuf_forget_for_tests(char *ptr) {
  strbuf_forget(ptr);
}

strbuf_probe_t strbuf_probe_for_tests(void) {
  return strbuf_probe;
}

void strbuf_probe_reset_for_tests(void) {
  strbuf_probe = (strbuf_probe_t){0};
}

void free_runtime_string(char *s) {
  if (!s) return;
  strbuf_forget(s);
  free(s);
}

static size_t strbuf_growth_capacity(size_t needed) {
  size_t cap = 16;
  while (cap < needed) {
    if (cap > SIZE_MAX / 2) return needed;
    cap *= 2;
  }
  return cap;
}

VALUE_t concat_two_strings(VALUE_t left, VALUE_t right) {
  size_t left_len = strlen(left.s);
  size_t right_len = strlen(right.s);
  if (left_len > SIN_MAX_STRING_BYTES ||
      right_len > SIN_MAX_STRING_BYTES - left_len) {
    free_runtime_string(left.s);
    free_runtime_string(right.s);
    return VALUE_NIL;
  }
  size_t needed = left_len + right_len + 1;
  strbuf_meta_t *left_meta = strbuf_find(left.s);
  char *out = NULL;
  size_t out_cap = needed;

  if (left_meta && left_meta->cap >= needed) {
    out = left.s;
    memcpy(out + left_len, right.s, right_len + 1);
    out_cap = left_meta->cap;
    free_runtime_string(right.s);
  } else {
    if (left_meta) {
      out_cap = strbuf_growth_capacity(needed);
    }
    out = malloc(out_cap);
    if (!out) {
      free_runtime_string(left.s);
      free_runtime_string(right.s);
      return VALUE_NIL;
    }
    memcpy(out, left.s, left_len);
    memcpy(out + left_len, right.s, right_len + 1);
    free_runtime_string(left.s);
    free_runtime_string(right.s);
  }

  strbuf_track(out, out_cap);
  left.s = out;
  left.type = VALUE_str;
  return left;
}
