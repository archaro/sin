#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "memory.h"

typedef struct {
  char *buf;
  size_t cap;
  size_t len;
  size_t max_len;
  bool failed;
} SIN_STRBUILDER_t;

static inline bool sin_sb_init(SIN_STRBUILDER_t *sb, size_t initial_cap, size_t max_len) {
  size_t max_cap = 0;
  if (alloc_add_overflow(max_len, 1u, &max_cap)) return false;
  if (initial_cap == 0) initial_cap = 1;
  if (initial_cap > max_cap) initial_cap = max_cap;
  sb->buf = malloc(initial_cap);
  if (!sb->buf) {
    sb->cap = 0;
    sb->len = 0;
    sb->max_len = max_len;
    sb->failed = true;
    return false;
  }
  sb->buf[0] = '\0';
  sb->cap = initial_cap;
  sb->len = 0;
  sb->max_len = max_len;
  sb->failed = false;
  return true;
}

static inline void sin_sb_dispose(SIN_STRBUILDER_t *sb) {
  free(sb->buf);
  sb->buf = NULL;
  sb->cap = 0;
  sb->len = 0;
  sb->max_len = 0;
  sb->failed = false;
}

static inline bool sin_sb_ensure(SIN_STRBUILDER_t *sb, size_t add_len) {
  size_t new_len = 0;
  size_t needed = 0;
  size_t max_cap = 0;
  if (sb->failed ||
      alloc_add_overflow(sb->len, add_len, &new_len) ||
      new_len > sb->max_len ||
      alloc_add_overflow(new_len, 1u, &needed) ||
      alloc_add_overflow(sb->max_len, 1u, &max_cap)) {
    sb->failed = true;
    return false;
  }
  if (needed <= sb->cap) return true;

  size_t new_cap = 0;
  if (!alloc_grow_capacity(sb->cap, needed, &new_cap)) {
    sb->failed = true;
    return false;
  }
  if (new_cap > max_cap) new_cap = max_cap;
  if (new_cap < needed) {
    sb->failed = true;
    return false;
  }
  char *new_buf = realloc(sb->buf, new_cap);
  if (!new_buf) {
    sb->failed = true;
    return false;
  }
  sb->buf = new_buf;
  sb->cap = new_cap;
  return true;
}

static inline bool sin_sb_append_bytes(SIN_STRBUILDER_t *sb, const char *src, size_t len) {
  if (!sin_sb_ensure(sb, len)) return false;
  memcpy(sb->buf + sb->len, src, len);
  sb->len += len;
  sb->buf[sb->len] = '\0';
  return true;
}

static inline bool sin_sb_append_cstr(SIN_STRBUILDER_t *sb, const char *src) {
  return sin_sb_append_bytes(sb, src, strlen(src));
}

static inline char *sin_sb_take(SIN_STRBUILDER_t *sb) {
  if (sb->failed) return NULL;
  char *out = sb->buf;
  sb->buf = NULL;
  sb->cap = 0;
  sb->len = 0;
  sb->max_len = 0;
  return out;
}
