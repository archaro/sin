// Internal itemstore declarations.
// Licensed under the MIT License - see LICENSE file for details.

#pragma once

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include "item.h"

typedef struct ItemChildren ITEM_CHILDREN_t;

struct Item {
  ITEM_e type;
  uint32_t bytecode_len;
  char name[ITEM_MAX_LAYER_NAME_LENGTH + 1u];
  uint32_t execution_pins;
  ITEM_t *parent;
  ITEM_CHILDREN_t *children;
  uint8_t *bytecode;
  VALUE_t value;
  ITEMSTORE_t *store;
};

typedef struct FetchItemCacheEntry {
  // These pointers are borrowed from the item tree. An entry is usable only
  // while its topology revision matches the context topology revision.
  bool valid;
  bool found;
  char key[MAX_ITEM_NAME];
  uint64_t topology_revision;
  ITEM_t *root;
  ITEM_t *item;
} FETCHITEM_CACHE_ENTRY_t;

#define FETCHITEM_CACHE_SIZE 256u
_Static_assert((FETCHITEM_CACHE_SIZE & (FETCHITEM_CACHE_SIZE - 1u)) == 0,
               "fetch-item cache size must be a power of two");

typedef struct ItemstoreContext {
  uint64_t topology_revision;
  uint64_t topology_revision_epoch;
  bool topology_revision_token_exhausted;
  uint64_t payload_revision;
  uint64_t payload_revision_epoch;
  bool payload_revision_token_exhausted;
  FETCHITEM_CACHE_ENTRY_t fetchitem_cache[FETCHITEM_CACHE_SIZE];
  uint64_t fetchitem_cache_hits;
  uint64_t fetchitem_cache_misses;
  ITEMSTORE_SYNC_HOOK_t sync_hook;
} ITEMSTORE_CONTEXT_t;

struct Itemstore {
  ITEM_t *root;
  ITEMSTORE_CONTEXT_t context;
};

typedef bool (*ITEMSTORE_LOAD_CONSTRUCTOR_FAILURE_HOOK_t)(const char *name);
typedef int (*ITEMSTORE_SOURCE_WRITE_HOOK_t)(const char *source, FILE *file);
typedef int (*ITEMSTORE_SOURCE_CLOSE_HOOK_t)(FILE *file);
typedef bool (*ITEMSTORE_ITEM_CREATION_FAILURE_HOOK_t)(const char *name);
typedef bool (*ITEMSTORE_DIRECTORY_SYNC_HOOK_t)(const char *path);
typedef void (*ITEMSTORE_PRE_PUBLISH_HOOK_t)(const char *path);

void itemstore_bump_topology_revision_for(const ITEM_t *item);
void itemstore_bump_payload_revision_for(const ITEM_t *item);
void itemstore_invalidate_cache_for(const ITEM_t *item);
bool itemstore_default_sync_hook(FILE *file, const char *path);
// These process-global test hooks require process quiescence for installation
// and reset; tests using them must run serially.
void itemstore_set_load_constructor_failure_hook_for_tests(
    ITEMSTORE_LOAD_CONSTRUCTOR_FAILURE_HOOK_t hook);
void itemstore_set_item_creation_failure_hook_for_tests(
    ITEMSTORE_ITEM_CREATION_FAILURE_HOOK_t hook);
void itemstore_set_source_io_hooks_for_tests(
    ITEMSTORE_SOURCE_WRITE_HOOK_t write_hook,
    ITEMSTORE_SOURCE_CLOSE_HOOK_t close_hook);
void itemstore_set_directory_sync_hook_for_tests(
    ITEMSTORE_DIRECTORY_SYNC_HOOK_t hook);
void itemstore_set_pre_publish_hook_for_tests(
    ITEMSTORE_PRE_PUBLISH_HOOK_t hook);

bool validate_item_name(const char *item_name, const char *func_name);
bool validate_item_name_relative(const ITEM_t *base, const char *item_name,
                                 const char *func_name);
bool item_layer_char_is_allowed(unsigned char character);
// Internal lookup for callers that have already validated item_name.
ITEM_t *find_item_unchecked(ITEM_t *root, const char *item_name);
ITEM_t *make_loaded_item(const char *name, ITEM_t *parent, ITEM_e type,
                         VALUE_t value, uint8_t *bytecode, int len,
                         uint32_t expected_children);
void detach_item_and_destroy(ITEM_t *item);
ITEM_t *make_item(const char *name, ITEM_t *parent, ITEM_e type,
                  VALUE_t value, uint8_t *bytecode, int len);
ITEM_t *make_root_item(const char *name);
void destroy_item(ITEM_t *item);

bool save_itemstore_with_options(const char *filename, ITEM_t *root,
                                 ITEMSTORE_DURABILITY_e durability);
bool save_itemstore_with_limits(const char *filename, ITEM_t *root,
                                ITEMSTORE_DURABILITY_e durability,
                                size_t max_records, size_t max_decode_bytes);
ITEMSTORE_SAVE_RESULT_e save_itemstore_no_replace(
    const char *filename, ITEM_t *root, ITEMSTORE_DURABILITY_e durability);
bool save_itemstore(const char *filename, ITEM_t *root);
ITEM_t *load_itemstore_with_options(const char *filename, bool strict_validation);
ITEM_t *load_itemstore(const char *filename);

typedef enum {
  ITEMSTORE_CONVERT_SUCCESS,
  ITEMSTORE_CONVERT_TARGET_EXISTS,
  ITEMSTORE_CONVERT_SAME_FILE,
  ITEMSTORE_CONVERT_FAILURE
} ITEMSTORE_CONVERT_RESULT_e;

ITEMSTORE_CONVERT_RESULT_e itemstore_convert(
    const char *input_filename, const char *output_filename,
    ITEMSTORE_DURABILITY_e durability, bool replace);
ITEMSTORE_CONVERT_RESULT_e itemstore_convert_with_limits(
    const char *input_filename, const char *output_filename,
    ITEMSTORE_DURABILITY_e durability, bool replace,
    size_t max_records, size_t max_decode_bytes,
    size_t conversion_work_limit);

// Child-container internals.
ITEM_CHILDREN_t *item_children_create_runtime(void);
ITEM_CHILDREN_t *item_children_create_loaded(uint32_t expected_children);
void item_children_destroy(ITEM_CHILDREN_t *children);
ITEM_t *item_children_lookup(const ITEM_CHILDREN_t *children, const char *name);
ITEM_t *item_children_lookup_span(const ITEM_CHILDREN_t *children,
                                  const char *name, size_t name_len);
bool item_children_append(ITEM_CHILDREN_t *children, const char *name,
                          ITEM_t *child);
ITEM_t *item_children_detach(ITEM_CHILDREN_t *children, const char *name);
size_t item_children_count(const ITEM_CHILDREN_t *children);
ITEM_t *item_children_at(const ITEM_CHILDREN_t *children, size_t index);
uint32_t item_children_bucket_count(const ITEM_CHILDREN_t *children);
size_t item_children_ordered_capacity(const ITEM_CHILDREN_t *children);
bool item_children_loaded_allocation_bytes(uint32_t expected_children,
                                           size_t *bytes);
uint32_t murmur3_32(const char *key, size_t len, uint32_t seed);

// Persistence internals.
bool write_item(FILE *file, ITEM_t *item, size_t *aggregate_budget);
ITEM_t *read_item(FILE *file, ITEM_t *parent);

// Allocator API.
ITEM_t *allocate_item(void);
void deallocate_item(ITEM_t *item);
