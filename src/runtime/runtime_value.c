// Runtime value/string ownership helpers

// Licensed under the MIT License - see LICENSE file for details.

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "runtime_value.h"
#include "string_limits.h"

typedef struct {
  char *ptr;
  size_t cap;
} strbuf_meta_t;

enum { strbuf_initial_capacity = 16 };

static strbuf_meta_t *strbuf_table = NULL;
static size_t strbuf_table_capacity = 0;
static size_t strbuf_table_count = 0;
static size_t strbuf_table_tombstones = 0;
static strbuf_probe_t strbuf_probe;
static int strbuf_fail_metadata_alloc_for_tests = 0;
static char strbuf_tombstone_marker;

#define STRBUF_TOMBSTONE (&strbuf_tombstone_marker)

static size_t strbuf_hash(char *ptr) {
  uintptr_t bits = (uintptr_t)ptr;
#if UINTPTR_MAX > UINT32_MAX
  bits ^= bits >> 33;
  bits *= UINT64_C(0xff51afd7ed558ccd);
  bits ^= bits >> 33;
  bits *= UINT64_C(0xc4ceb9fe1a85ec53);
  bits ^= bits >> 33;
#else
  bits ^= bits >> 16;
  bits *= UINT32_C(0x85ebca6b);
  bits ^= bits >> 13;
  bits *= UINT32_C(0xc2b2ae35);
  bits ^= bits >> 16;
#endif
  return (size_t)bits;
}

static int strbuf_allocate_table(size_t capacity, strbuf_meta_t **out) {
  if (capacity > SIZE_MAX / sizeof(**out) ||
      strbuf_fail_metadata_alloc_for_tests) return 0;
  *out = calloc(capacity, sizeof(**out));
  return *out != NULL;
}

static strbuf_meta_t *strbuf_find(char *ptr) {
  strbuf_probe.find_calls++;
  if (!strbuf_table) return NULL;
  size_t mask = strbuf_table_capacity - 1u;
  size_t index = strbuf_hash(ptr) & mask;
  for (;;) {
    strbuf_meta_t *entry = &strbuf_table[index];
    strbuf_probe.find_nodes++;
    if (!entry->ptr) return NULL;
    if (entry->ptr == ptr) return entry;
    index = (index + 1u) & mask;
  }
}

static int strbuf_rehash(size_t capacity) {
  strbuf_meta_t *replacement;
  if (!strbuf_allocate_table(capacity, &replacement)) return 0;
  for (size_t i = 0; i < strbuf_table_capacity; i++) {
    strbuf_meta_t entry = strbuf_table[i];
    if (!entry.ptr || entry.ptr == STRBUF_TOMBSTONE) continue;
    size_t index = strbuf_hash(entry.ptr) & (capacity - 1u);
    while (replacement[index].ptr) index = (index + 1u) & (capacity - 1u);
    replacement[index] = entry;
  }
  free(strbuf_table);
  strbuf_table = replacement;
  strbuf_table_capacity = capacity;
  strbuf_table_tombstones = 0;
  return 1;
}

static int strbuf_prepare_insert(void) {
  if (!strbuf_table) return strbuf_rehash(strbuf_initial_capacity);
  size_t used = strbuf_table_count + strbuf_table_tombstones + 1u;
  if (used < strbuf_table_capacity - strbuf_table_capacity / 4u) return 1;
  if (strbuf_table_capacity <= SIZE_MAX / 2u &&
      strbuf_rehash(strbuf_table_capacity * 2u)) return 1;
  /* A same-size rebuild can reclaim tombstones if growing failed. */
  return strbuf_table_tombstones && strbuf_rehash(strbuf_table_capacity);
}

static void strbuf_forget(char *ptr) {
  strbuf_probe.forget_calls++;
  if (!strbuf_table) return;
  size_t mask = strbuf_table_capacity - 1u;
  size_t index = strbuf_hash(ptr) & mask;
  for (;;) {
    strbuf_meta_t *entry = &strbuf_table[index];
    strbuf_probe.forget_nodes++;
    if (!entry->ptr) return;
    if (entry->ptr == ptr) {
      entry->ptr = STRBUF_TOMBSTONE;
      entry->cap = 0;
      strbuf_table_count--;
      strbuf_table_tombstones++;
      if (!strbuf_table_count) {
        free(strbuf_table);
        strbuf_table = NULL;
        strbuf_table_capacity = 0;
        strbuf_table_tombstones = 0;
      } else if (strbuf_table_capacity > strbuf_initial_capacity &&
                 strbuf_table_count * 8u <= strbuf_table_capacity) {
        (void)strbuf_rehash(strbuf_table_capacity / 2u);
      } else if (strbuf_table_tombstones * 4u >= strbuf_table_capacity) {
        (void)strbuf_rehash(strbuf_table_capacity);
      }
      return;
    }
    index = (index + 1u) & mask;
  }
}

static void strbuf_track(char *ptr, size_t cap) {
  strbuf_meta_t *meta = strbuf_find(ptr);
  if (meta) {
    meta->cap = cap;
    return;
  }
  if (!strbuf_prepare_insert()) return;
  size_t mask = strbuf_table_capacity - 1u;
  size_t index = strbuf_hash(ptr) & mask;
  size_t tombstone = SIZE_MAX;
  for (;;) {
    strbuf_meta_t *entry = &strbuf_table[index];
    if (!entry->ptr) {
      if (tombstone != SIZE_MAX) entry = &strbuf_table[tombstone];
      entry->ptr = ptr;
      entry->cap = cap;
      strbuf_table_count++;
      if (tombstone != SIZE_MAX) strbuf_table_tombstones--;
      return;
    }
    if (entry->ptr == STRBUF_TOMBSTONE && tombstone == SIZE_MAX) tombstone = index;
    index = (index + 1u) & mask;
  }
}

size_t strbuf_tracked_count_for_tests(void) { return strbuf_table_count; }

size_t strbuf_registry_capacity_for_tests(void) { return strbuf_table_capacity; }

size_t strbuf_capacity_for_tests(char *ptr) {
  strbuf_meta_t *meta = strbuf_find(ptr);
  return meta ? meta->cap : 0;
}

size_t strbuf_bucket_for_tests(char *ptr, size_t capacity) {
  return capacity ? strbuf_hash(ptr) & (capacity - 1u) : 0;
}

void strbuf_forget_for_tests(char *ptr) { strbuf_forget(ptr); }

void strbuf_track_for_tests(char *ptr, size_t cap) { strbuf_track(ptr, cap); }

void strbuf_fail_metadata_allocations_for_tests(int fail) {
  strbuf_fail_metadata_alloc_for_tests = fail;
}

strbuf_probe_t strbuf_probe_for_tests(void) { return strbuf_probe; }

void strbuf_probe_reset_for_tests(void) { strbuf_probe = (strbuf_probe_t){0}; }

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
    if (left_meta) out_cap = strbuf_growth_capacity(needed);
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
