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
typedef struct Entry ENTRY_t;
typedef struct HashTable HASHTABLE_t;

typedef enum {ITEM_value, ITEM_code} ITEM_e;
typedef enum {
  ITEMSTORE_DURABLE_FULL = 0,
  ITEMSTORE_DURABLE_FAST = 1
} ITEMSTORE_DURABILITY_e;
typedef bool (*ITEMSTORE_SYNC_HOOK_t)(FILE *file, const char *path);

struct Item {
  ITEM_e type;           // 4 bytes
  uint32_t bytecode_len; // 4 bytes
  char name[ITEM_MAX_LAYER_NAME_LENGTH + 1u];
  bool inuse;            // Set when an item is being executed.
  uint8_t pad[7];        // 6 bytes of padding for 8-byte alignment
  ITEM_t *parent;        // 8 bytes - Pointer to the parent item
  HASHTABLE_t *children; // Owned hash table for immediate children.
  uint8_t *bytecode;     // Owned bytecode buffer for code items; NULL for value items.
  VALUE_t value;         // Owned VALUE_t payload for value items; destroyed with the item.
  size_t ordered_size;  // Number of children in the ordered array
  size_t ordered_capacity; // Max size of ordered array
  ITEM_t **ordered_array; // Owned array of borrowed child pointers; children own themselves.
};

// Itemstore API
ITEM_t *make_root_item(const char *name);
// make_item creates an item owned by its parent/itemstore. For ITEM_value, the
// item takes ownership of value, including any VALUE_str payload. For ITEM_code,
// the item takes ownership of bytecode and frees it from destroy_item(); value is
// ignored. On failure before an item is constructed, the caller still owns the
// supplied payloads.
ITEM_t *make_item(const char *name, ITEM_t *parent, ITEM_e type,
                                VALUE_t value, uint8_t *bytecode, int len);
// Recursively destroys item and all children. Frees owned bytecode for code
// items, owned VALUE_t string payloads for value items, the child table, ordered
// child-pointer array, and the ITEM_t itself.
void destroy_item(ITEM_t *item);    
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
bool itemstore_durability_requires_sync(ITEMSTORE_DURABILITY_e durability);
void itemstore_set_sync_hook_for_tests(ITEMSTORE_SYNC_HOOK_t hook);
bool save_itemstore_with_options(const char *filename, ITEM_t *root, ITEMSTORE_DURABILITY_e durability);
bool save_itemstore(const char *filename, ITEM_t *root);
// Loads and returns a newly allocated item tree. The caller owns the returned
// root and must destroy it with destroy_item(); NULL indicates failure and
// transfers no ownership.
ITEM_t *load_itemstore_with_options(const char *filename, bool strict_validation);
ITEM_t *load_itemstore(const char *filename); 
void dump_item(ITEM_t *item, char *item_name, bool isroot);
uint64_t get_itemstore_generation(void);

// Other item-related API functions
bool is_valid_layer(const char *str);
void set_error_item(ITEM_t *root, const int errnum,
                    const char *errdetail, ITEM_t *current_item);
void set_compiler_error_item(ITEM_t *root, const CompilerDiagnostic *diag);
void clear_error_item(ITEM_t *root);
