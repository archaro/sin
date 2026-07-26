// The item is the basic unit of storage.  It may contain a value or code.
// It may also contain nested items.  All items are evaluated.  A value item
// pushes a value onto the stack.  A code item is executed, and the value of
// the executed item is pushed onto the stack.
//
// Each code item is an isolated unit, with its own stack, etc.

// Licensed under the MIT License - see LICENSE file for details.

#pragma once

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

#include "value.h"
#include "compiler/compdiag.h"

// Item names follow the v1 itemstore contract.  The root is depth 0; public
// item paths contain at most eight non-root layers.  MAX_ITEM_NAME includes
// the terminating NUL, while ITEM_MAX_FULL_NAME_LENGTH does not.
#define ITEM_MAX_LAYER_NAME_LENGTH 32u
#define ITEM_MAX_DEPTH 8u
#define ITEM_MAX_FULL_NAME_LENGTH \
  (ITEM_MAX_LAYER_NAME_LENGTH * ITEM_MAX_DEPTH + (ITEM_MAX_DEPTH - 1u))
#define MAX_ITEM_NAME (ITEM_MAX_FULL_NAME_LENGTH + 1u)

// Item children are also stored in an indexable array for iteration
// performance.  This value controls the size of that array.
#define ITEM_ARRAY_INIT_CAPACITY  10

typedef struct Item ITEM_t;
typedef struct Itemstore ITEMSTORE_t;

typedef enum {ITEM_value, ITEM_code} ITEM_e;
typedef enum {
  ITEMSTORE_DURABLE_FULL = 0,
  ITEMSTORE_DURABLE_FAST = 1
} ITEMSTORE_DURABILITY_e;
typedef enum {
  ITEMSTORE_SAVE_SUCCESS,
  ITEMSTORE_SAVE_TARGET_EXISTS,
  ITEMSTORE_SAVE_FAILURE
} ITEMSTORE_SAVE_RESULT_e;
typedef bool (*ITEMSTORE_SYNC_HOOK_t)(FILE *file, const char *path);

/* Explicit itemstore ownership boundary.  The root returned by
 * itemstore_root() is borrowed and remains valid until itemstore_destroy(). */
ITEMSTORE_t *itemstore_create(const char *name);
/* Creates a dedicated store for detached boot bytecode.  The store takes
 * ownership of bytecode on success; callers retain it if construction fails. */
ITEMSTORE_t *itemstore_create_boot(const char *name, uint8_t *bytecode,
                                   uint32_t bytecode_len);
ITEM_t *itemstore_root(ITEMSTORE_t *store);
void itemstore_destroy(ITEMSTORE_t *store);
ITEMSTORE_t *itemstore_load_with_options(const char *filename,
                                         bool strict_validation);
ITEMSTORE_t *itemstore_load(const char *filename);
bool itemstore_save_with_options(const char *filename, ITEMSTORE_t *store,
                                 ITEMSTORE_DURABILITY_e durability);
bool itemstore_save(const char *filename, ITEMSTORE_t *store);
ITEMSTORE_SAVE_RESULT_e itemstore_save_no_replace(
    const char *filename, ITEMSTORE_t *store,
    ITEMSTORE_DURABILITY_e durability);
uint64_t itemstore_generation(const ITEMSTORE_t *store);
uint64_t itemstore_cache_hits(const ITEMSTORE_t *store);
uint64_t itemstore_cache_misses(const ITEMSTORE_t *store);
ITEMSTORE_t *itemstore_owner(const ITEM_t *item);
ITEM_e item_kind(const ITEM_t *item);
const char *item_layer_name(const ITEM_t *item);
ITEM_t *item_parent(const ITEM_t *item);
const VALUE_t *item_value(const ITEM_t *item);
const uint8_t *item_bytecode(const ITEM_t *item);
uint32_t item_bytecode_length(const ITEM_t *item);
size_t item_child_count(const ITEM_t *item);
ITEM_t *item_child_at(const ITEM_t *item, size_t index);
bool item_is_in_use(const ITEM_t *item);
void item_enter_use(ITEM_t *item);
void item_leave_use(ITEM_t *item);

// insert_item creates or replaces a value item. On success, the itemstore takes
// ownership of value, including any VALUE_str payload; callers must not free the
// string after transfer. Existing value payloads or code bytecode are freed
// before replacement. If validation, input-limit checks, or replacement fails
// before the value is stored, the caller retains ownership of value. An
// incompatible alias of an existing item payload is rejected without mutation;
// that pointer is already owned by the item and must not be freed by the caller.
ITEM_t *insert_item(ITEM_t *root, const char *item_name, VALUE_t value);
// insert_code_item creates or replaces a code item. On success, the itemstore
// takes ownership of bytecode and frees any previous code bytecode/value payload.
// If validation or replacement fails before bytecode is stored, the caller
// retains ownership of bytecode. An incompatible alias of an existing value
// payload is rejected without mutation; that pointer is already item-owned.
ITEM_t *insert_code_item(ITEM_t *root, const char *item_name, uint32_t len,
                                                        uint8_t *bytecode);
ITEM_t *find_item(ITEM_t *root, const char *item_name);
// Cached lookup validates item_name before touching the cache. Invalid names
// return NULL, set found to false when supplied, and do not affect cache
// counters. Valid lookups cache both found and not-found results; cached item
// pointers are borrowed and valid only for the current itemstore generation.
ITEM_t *find_item_cached(ITEM_t *root, const char *item_name, bool *found);
ITEM_t *find_item_by_index(ITEM_t *parent, const size_t index);
void delete_item(ITEM_t *root, const char *item_name);
// set_item creates or replaces a value item. The itemstore takes ownership of
// value, including any VALUE_str payload, whether updating an existing item or
// inserting a new one. set_item always consumes value, including when
// validation, input-limit checks, replacement, or path creation fails. The
// exception is an incompatible alias of an existing code payload: it is
// rejected without mutation because the pointer is already item-owned.
void set_item(ITEM_t *root, const char *item_name, VALUE_t value);
void get_itemname(ITEM_t *item, char *itemname);
// Returns a newly allocated source filename string owned by the caller; free it
// with free(). The item is borrowed and not modified.
char *get_itemfilename_in_srcroot(ITEM_t *item, const char *srcroot);
char *get_itemfilename(ITEM_t *item);
// Borrows item and source for the duration of the call. Does not take ownership
// of source or modify/free it.
bool save_itemsource_in_srcroot(ITEM_t *item, char *source, const char *srcroot);
bool save_itemsource(ITEM_t *item, char *source);
// Returns a newly allocated, NUL-terminated copy of the source file, including
// an allocated empty string for an empty file. The caller owns the result and
// must free it. On failure, returns NULL and writes a best-effort diagnostic to
// detail when detail is non-NULL and detail_size is non-zero. The item and
// srcroot are borrowed; neither is modified or freed.
char *read_itemsource_in_srcroot(ITEM_t *item, const char *srcroot,
                                 char *detail, size_t detail_size);
bool itemstore_durability_requires_sync(ITEMSTORE_DURABILITY_e durability);
void itemstore_set_sync_hook_for_tests(ITEMSTORE_SYNC_HOOK_t hook);
void dump_item(ITEM_t *item, char *item_name, bool isroot);

// Other item-related API functions
bool is_valid_layer(const char *str);
void set_error_item(ITEM_t *root, const int errnum,
                    const char *errdetail, ITEM_t *current_item);
void set_compiler_error_item(ITEM_t *root, const CompilerDiagnostic *diag);
void clear_error_item(ITEM_t *root);
