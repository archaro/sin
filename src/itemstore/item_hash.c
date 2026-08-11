// The Item.  The nub and the gist of the whole brouhaha in a nutshell.

// Licensed under the MIT License - see LICENSE file for details.

#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include "item_internal.h"
#include "log.h"
#include "memory.h"

#define ITEM_CHILDREN_INITIAL_BUCKETS 16u
#define ITEM_CHILDREN_INITIAL_CAPACITY 10u

typedef struct ItemEntry ItemEntry_t;

struct ItemEntry {
  ITEM_t *child;
  ItemEntry_t *next;
};

struct ItemChildren {
  uint32_t size;
  uint32_t entry_count;
  ItemEntry_t **table;
  size_t ordered_size;
  size_t ordered_capacity;
  ITEM_t **ordered_array;
};

uint32_t murmur3_32(const char *key, size_t len, uint32_t seed) {
  const uint32_t c1 = 0xcc9e2d51u;
  const uint32_t c2 = 0x1b873593u;
  const size_t nblocks = len / 4u;
  const uint8_t *bytes = (const uint8_t *)key;
  uint32_t hash = seed;

  for (size_t i = 0; i < nblocks; i++) {
    const uint8_t *block = bytes + i * 4u;
    uint32_t k = (uint32_t)block[0] | ((uint32_t)block[1] << 8) |
                 ((uint32_t)block[2] << 16) | ((uint32_t)block[3] << 24);
    k *= c1;
    k = (k << 15) | (k >> 17);
    k *= c2;
    hash ^= k;
    hash = ((hash << 13) | (hash >> 19)) * 5u + 0xe6546b64u;
  }

  const uint8_t *tail = bytes + nblocks * 4u;
  uint32_t k1 = 0;
  switch (len & 3u) {
    case 3:
      k1 ^= (uint32_t)tail[2] << 16;
      __attribute__((fallthrough));
    case 2:
      k1 ^= (uint32_t)tail[1] << 8;
      __attribute__((fallthrough));
    case 1:
      k1 ^= tail[0];
      k1 *= c1;
      k1 = (k1 << 15) | (k1 >> 17);
      k1 *= c2;
      hash ^= k1;
      break;
  }

  hash ^= (uint32_t)len;
  hash ^= hash >> 16;
  hash *= 0x85ebca6bu;
  hash ^= hash >> 13;
  hash *= 0xc2b2ae35u;
  hash ^= hash >> 16;
  return hash;
}

static uint32_t hash_key_span(const char *key, size_t len) {
  if (len <= 4u) {
    uint32_t hash = 0;
    memcpy(&hash, key, len);
    return hash;
  }
  return murmur3_32(key, len, 0);
}

static uint32_t hash_key(const char *key) {
  return hash_key_span(key, strlen(key));
}

static ITEM_CHILDREN_t *create_children(uint32_t bucket_count,
                                        size_t ordered_capacity) {
  ITEM_CHILDREN_t *children = alloc_calloc(1, sizeof *children);
  if (!children) return NULL;

  children->size = bucket_count > 0 ? bucket_count : 1u;
  children->ordered_capacity = ordered_capacity;
  children->table = alloc_calloc(children->size, sizeof *children->table);
  if (!children->table) {
    free(children);
    return NULL;
  }

  if (ordered_capacity > 0) {
    children->ordered_array =
        alloc_calloc(ordered_capacity, sizeof *children->ordered_array);
    if (!children->ordered_array) {
      free(children->table);
      free(children);
      return NULL;
    }
  }
  return children;
}

ITEM_CHILDREN_t *item_children_create_runtime(void) {
  return create_children(ITEM_CHILDREN_INITIAL_BUCKETS,
                          ITEM_CHILDREN_INITIAL_CAPACITY);
}

ITEM_CHILDREN_t *item_children_create_loaded(uint32_t expected_children) {
  uint32_t buckets = expected_children == 0
      ? 1u
      : (uint32_t)(((uint64_t)expected_children * 4u + 2u) / 3u);
  return create_children(buckets, expected_children);
}

bool item_children_loaded_allocation_bytes(uint32_t expected_children,
                                           size_t *bytes) {
  uint32_t buckets = expected_children == 0
      ? 1u
      : (uint32_t)(((uint64_t)expected_children * 4u + 2u) / 3u);
  size_t total = sizeof(ITEM_t);
  size_t part = 0;
  if (alloc_add_overflow(total, sizeof(struct ItemChildren), &total) ||
      alloc_mul_overflow((size_t)buckets, sizeof(ItemEntry_t *), &part) ||
      alloc_add_overflow(total, part, &total) ||
      alloc_mul_overflow((size_t)expected_children, sizeof(ITEM_t *), &part) ||
      alloc_add_overflow(total, part, &total) ||
      alloc_mul_overflow((size_t)expected_children, sizeof(ItemEntry_t), &part) ||
      alloc_add_overflow(total, part, &total)) {
    return false;
  }
  if (bytes) *bytes = total;
  return true;
}

static bool resize_children(ITEM_CHILDREN_t *children, uint32_t new_size) {
  ItemEntry_t **table = calloc(new_size, sizeof *table);
  if (!table) return false;

  for (uint32_t i = 0; i < children->size; i++) {
    ItemEntry_t *entry = children->table[i];
    while (entry) {
      ItemEntry_t *next = entry->next;
      uint32_t index = hash_key(entry->child->name) % new_size;
      entry->next = table[index];
      table[index] = entry;
      entry = next;
    }
  }
  free(children->table);
  children->table = table;
  children->size = new_size;
  return true;
}

static bool grow_ordered_array(ITEM_CHILDREN_t *children) {
  if (children->ordered_size < children->ordered_capacity) return true;

  size_t required = children->ordered_size + 1u;
  size_t capacity = children->ordered_capacity;
  if (required < ITEM_CHILDREN_INITIAL_CAPACITY) {
    required = ITEM_CHILDREN_INITIAL_CAPACITY;
  }
  if (!alloc_grow_array_capacity((void **)&children->ordered_array, &capacity,
                                 required,
                                 sizeof *children->ordered_array)) {
    logerr("Cannot grow ordered item array beyond %zu entries.\n",
           children->ordered_capacity);
    return false;
  }
  children->ordered_capacity = capacity;
  return true;
}

ITEM_t *item_children_lookup(const ITEM_CHILDREN_t *children,
                             const char *name) {
  if (!children || !name) return NULL;
  return item_children_lookup_span(children, name, strlen(name));
}

ITEM_t *item_children_lookup_span(const ITEM_CHILDREN_t *children,
                                  const char *name, size_t name_len) {
  if (!children || !name || name_len > ITEM_MAX_LAYER_NAME_LENGTH) {
    return NULL;
  }
  uint32_t index = hash_key_span(name, name_len) % children->size;
  for (ItemEntry_t *entry = children->table[index]; entry;
       entry = entry->next) {
    if (memcmp(entry->child->name, name, name_len) == 0 &&
        entry->child->name[name_len] == '\0') {
      return entry->child;
    }
  }
  return NULL;
}

bool item_children_append(ITEM_CHILDREN_t *children, const char *name,
                          ITEM_t *child) {
  if (!children || !name || !child || strcmp(name, child->name) != 0 ||
      item_children_lookup(children, name)) {
    return false;
  }

  ItemEntry_t *entry = alloc_malloc(sizeof *entry);
  if (!entry) return false;
  if (!grow_ordered_array(children)) {
    free(entry);
    return false;
  }

  uint32_t index = hash_key(child->name) % children->size;
  entry->child = child;
  entry->next = children->table[index];
  children->table[index] = entry;
  children->entry_count++;
  children->ordered_array[children->ordered_size++] = child;

  if ((float)children->entry_count / (float)children->size > 0.75f) {
    uint32_t grown_size = children->size * 2u + 1u;
    int new_size = grown_size > (uint32_t)INT_MAX ? INT_MAX
                                                  : (int)grown_size;
    (void)resize_children(children, (uint32_t)new_size);
  }
  return true;
}

ITEM_t *item_children_detach(ITEM_CHILDREN_t *children, const char *name) {
  if (!children || !name) return NULL;

  uint32_t bucket = hash_key(name) % children->size;
  ItemEntry_t *entry = children->table[bucket];
  ItemEntry_t *previous = NULL;
  while (entry && strcmp(entry->child->name, name) != 0) {
    previous = entry;
    entry = entry->next;
  }
  if (!entry) return NULL;

  size_t ordered_index = children->ordered_size;
  for (size_t i = 0; i < children->ordered_size; i++) {
    if (children->ordered_array[i] == entry->child) {
      ordered_index = i;
      break;
    }
  }
  if (ordered_index == children->ordered_size) return NULL;

  if (previous) previous->next = entry->next;
  else children->table[bucket] = entry->next;
  ITEM_t *child = entry->child;
  free(entry);
  for (size_t i = ordered_index; i + 1u < children->ordered_size; i++) {
    children->ordered_array[i] = children->ordered_array[i + 1u];
  }
  children->ordered_size--;
  children->entry_count--;
  return child;
}

void item_children_destroy(ITEM_CHILDREN_t *children) {
  if (!children) return;
  for (size_t i = 0; i < children->ordered_size; i++) {
    destroy_item(children->ordered_array[i]);
  }
  for (uint32_t i = 0; i < children->size; i++) {
    ItemEntry_t *entry = children->table[i];
    while (entry) {
      ItemEntry_t *next = entry->next;
      free(entry);
      entry = next;
    }
  }
  free(children->table);
  free(children->ordered_array);
  free(children);
}

size_t item_children_count(const ITEM_CHILDREN_t *children) {
  return children ? children->ordered_size : 0;
}

ITEM_t *item_children_at(const ITEM_CHILDREN_t *children, size_t index) {
  return children && index < children->ordered_size
      ? children->ordered_array[index]
      : NULL;
}

uint32_t item_children_bucket_count(const ITEM_CHILDREN_t *children) {
  return children ? children->size : 0;
}

size_t item_children_ordered_capacity(const ITEM_CHILDREN_t *children) {
  return children ? children->ordered_capacity : 0;
}

ITEM_t *allocate_item(void) {
  return alloc_malloc(sizeof(ITEM_t));
}

void deallocate_item(ITEM_t *item) {
  free(item);
}
