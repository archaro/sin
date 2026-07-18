// Internal itemstore declarations.
// Licensed under the MIT License - see LICENSE file for details.

#pragma once

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include "item.h"

// Define the hash table entry.
struct Entry {
  char *key;
  ITEM_t *child;
  ENTRY_t *next;
};

// This hashtable contains pointers to all the children of this Item.
struct HashTable {
  uint32_t size;
  uint32_t entry_count;
  ENTRY_t **table; // An array of pointers to ENTRY_t
};

typedef struct FetchItemCacheEntry {
  // These pointers are borrowed from the item tree.  An entry is usable only
  // while its generation matches the context generation.
  bool valid;
  bool found;
  char key[MAX_ITEM_NAME];
  uint64_t generation;
  ITEM_t *root;
  ITEM_t *item;
} FETCHITEM_CACHE_ENTRY_t;

#define FETCHITEM_CACHE_SIZE 256u
_Static_assert((FETCHITEM_CACHE_SIZE & (FETCHITEM_CACHE_SIZE - 1u)) == 0,
               "fetch-item cache size must be a power of two");

typedef struct ItemstoreContext {
  uint64_t generation;
  FETCHITEM_CACHE_ENTRY_t fetchitem_cache[FETCHITEM_CACHE_SIZE];
  uint64_t fetchitem_cache_hits;
  uint64_t fetchitem_cache_misses;
  ITEMSTORE_SYNC_HOOK_t sync_hook;
} ITEMSTORE_CONTEXT_t;

typedef bool (*ITEMSTORE_LOAD_CONSTRUCTOR_FAILURE_HOOK_t)(const char *name);
typedef int (*ITEMSTORE_SOURCE_WRITE_HOOK_t)(const char *source, FILE *file);
typedef int (*ITEMSTORE_SOURCE_CLOSE_HOOK_t)(FILE *file);
typedef bool (*ITEMSTORE_ITEM_CREATION_FAILURE_HOOK_t)(const char *name);
typedef bool (*ITEMSTORE_DIRECTORY_SYNC_HOOK_t)(const char *path);

ITEMSTORE_CONTEXT_t *itemstore_default_context(void);
void itemstore_bump_generation(void);
void itemstore_invalidate_cache(void);
bool itemstore_default_sync_hook(FILE *file, const char *path);
void itemstore_set_load_constructor_failure_hook_for_tests(
    ITEMSTORE_LOAD_CONSTRUCTOR_FAILURE_HOOK_t hook);
void itemstore_set_item_creation_failure_hook_for_tests(
    ITEMSTORE_ITEM_CREATION_FAILURE_HOOK_t hook);
void itemstore_set_source_io_hooks_for_tests(
    ITEMSTORE_SOURCE_WRITE_HOOK_t write_hook,
    ITEMSTORE_SOURCE_CLOSE_HOOK_t close_hook);
void itemstore_set_directory_sync_hook_for_tests(
    ITEMSTORE_DIRECTORY_SYNC_HOOK_t hook);

bool validate_item_name(const char *item_name, const char *func_name);
bool validate_item_name_relative(const ITEM_t *base, const char *item_name,
                                 const char *func_name);
bool item_layer_char_is_allowed(unsigned char character);
// Internal lookup for callers that have already validated item_name.
ITEM_t *find_item_unchecked(ITEM_t *root, const char *item_name);
bool create_ordered_array(ITEM_t *item);
bool resize_ordered_array(ITEM_t *item);
ITEM_t *make_loaded_item(const char *name, ITEM_t *parent, ITEM_e type,
                         VALUE_t value, uint8_t *bytecode, int len,
                         uint32_t expected_children);
void detach_item_and_destroy(ITEM_t *item);

// Hash table internals.
HASHTABLE_t *create_hashtable(int size);
uint32_t simple_hash(const char *key, size_t len);
HASHTABLE_t *resize_hashtable(HASHTABLE_t *oldhashtable, int newsize);
float calculate_load_factor(HASHTABLE_t *hashTable);
HASHTABLE_t *maybe_resize_hashtable(HASHTABLE_t *hashtable);
bool insert_hashtable(HASHTABLE_t *hashtable, const char *key, ITEM_t *child);
ITEM_t *search_hashtable(HASHTABLE_t *hashtable, const char *key);
void delete_hashtable(HASHTABLE_t *hashtable, const char *key);
void free_hashtable(HASHTABLE_t *hashtable);
uint32_t murmur3_32(const char *key, size_t len, uint32_t seed);
char *substr(const char *str, size_t begin, size_t len);

// Persistence internals.
bool write_item(FILE *file, ITEM_t *item);
ITEM_t *read_item(FILE *file, ITEM_t *parent);

// Allocator API.
ENTRY_t *allocate_entry(void);
HASHTABLE_t *allocate_hashtable(void);
ITEM_t *allocate_item(void);
void deallocate_entry(ENTRY_t *entry);
void deallocate_hashtable(HASHTABLE_t *hashtable);
void deallocate_item(ITEM_t *item);
