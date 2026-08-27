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

typedef struct Item ITEM_t;
typedef struct Itemstore ITEMSTORE_t;

typedef enum {ITEM_value, ITEM_code} ITEM_e;
typedef enum {
  ITEM_MUTATION_CREATED,
  ITEM_MUTATION_REPLACED,
  ITEM_MUTATION_DELETED,
  ITEM_MUTATION_NOT_FOUND,
  ITEM_MUTATION_INVALID_ARGUMENT,
  ITEM_MUTATION_INVALID_NAME,
  ITEM_MUTATION_INVALID_PAYLOAD,
  ITEM_MUTATION_IN_USE,
  ITEM_MUTATION_ALLOCATION_FAILED
} ITEM_MUTATION_STATUS_e;

typedef struct {
  ITEM_MUTATION_STATUS_e status;
  ITEM_t *item;
} ITEM_MUTATION_RESULT_t;

// item_mutation_succeeded is true only for CREATED, REPLACED, and DELETED.
bool item_mutation_succeeded(ITEM_MUTATION_RESULT_t result);
// Item paths use ASCII case-insensitive layer names. These helpers validate
// a path and copy its canonical lower-case spelling to out_name, which must
// provide MAX_ITEM_NAME bytes. The relative form resolves a leading '.' from
// context_item; ordinary store root labels are not folded, while a relative
// path from a detached executable context uses that context's full name.
bool item_path_canonicalize(const char *item_name, char *out_name);
bool item_path_canonicalize_relative(const ITEM_t *context_item,
                                     const char *item_name, char *out_name);
/* Returns true for the runtime-owned error item and every descendant. The
 * input must already be canonical and root-relative. */
bool item_path_is_error_namespace(const char *item_name);
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
uint64_t itemstore_topology_revision(const ITEMSTORE_t *store);
uint64_t itemstore_topology_revision_epoch(const ITEMSTORE_t *store);
bool itemstore_topology_revision_token_exhausted(const ITEMSTORE_t *store);
uint64_t itemstore_payload_revision(const ITEMSTORE_t *store);
/* Increments only when payload_revision wraps, so the pair is a
 * non-repeating mutation token for practical store lifetimes. */
uint64_t itemstore_payload_revision_epoch(const ITEMSTORE_t *store);
bool itemstore_payload_revision_token_exhausted(const ITEMSTORE_t *store);
uint64_t itemstore_cache_hits(const ITEMSTORE_t *store);
uint64_t itemstore_cache_misses(const ITEMSTORE_t *store);
ITEMSTORE_t *itemstore_owner(const ITEM_t *item);
ITEM_e item_kind(const ITEM_t *item);
const char *item_layer_name(const ITEM_t *item);
ITEM_t *item_parent(const ITEM_t *item);
/* Returns the borrowed value payload for value items; code items and NULL
 * inputs return NULL. */
const VALUE_t *item_value(const ITEM_t *item);
/* Returns the borrowed bytecode payload and length only for code items;
 * value items and NULL inputs return NULL and zero respectively. */
const uint8_t *item_bytecode(const ITEM_t *item);
uint32_t item_bytecode_length(const ITEM_t *item);
size_t item_child_count(const ITEM_t *item);
ITEM_t *item_child_at(const ITEM_t *item, size_t index);
bool item_is_in_use(const ITEM_t *item);
void item_enter_use(ITEM_t *item);
void item_leave_use(ITEM_t *item);

// On CREATED or REPLACED, result.item is the borrowed resulting leaf and the
// itemstore takes ownership of the supplied payload. DELETED always returns a
// NULL result.item. Every failure returns a NULL result.item, leaves the tree
// and revisions unchanged, and leaves the supplied payload caller-owned. The
// exception is a rejected alias already owned by the target: it remains owned
// by that target and must not be freed by the caller.
ITEM_MUTATION_RESULT_t item_set_value(ITEM_t *root, const char *item_name,
                                      VALUE_t value);
ITEM_MUTATION_RESULT_t item_set_code(ITEM_t *root, const char *item_name,
                                     uint32_t len, uint8_t *bytecode);
ITEM_t *find_item(ITEM_t *root, const char *item_name);
// Cached lookup validates item_name before touching the cache. Invalid names
// return NULL, set found to false when supplied, and do not affect cache
// counters. Valid lookups cache both found and not-found results; cached item
// pointers are borrowed and valid only for the current itemstore topology
// revision. Payload replacement preserves pointer and cache validity.
ITEM_t *find_item_cached(ITEM_t *root, const char *item_name, bool *found);
// DELETED returns a NULL item. Valid absent names return NOT_FOUND; malformed
// names, null roots, and pinned subtrees return their corresponding failures.
ITEM_MUTATION_RESULT_t item_delete(ITEM_t *root, const char *item_name);
void get_itemname(ITEM_t *item, char *itemname);
// Returns a newly allocated source filename string owned by the caller; free it
// with free(). The item is borrowed and not modified.
char *get_itemfilename_in_srcroot(ITEM_t *item, const char *srcroot);
char *get_itemfilename(ITEM_t *item);
// Borrows item and source for the duration of the call. Does not take ownership
// of source or modify/free it. Only code items may have source sidecars.
bool save_itemsource_in_srcroot(ITEM_t *item, char *source, const char *srcroot);
bool save_itemsource(ITEM_t *item, char *source);
// Returns a newly allocated, NUL-terminated copy of the source file, including
// an allocated empty string for an empty file. The caller owns the result and
// must free it. On failure, returns NULL and writes a best-effort diagnostic to
// detail when detail is non-NULL and detail_size is non-zero. The item and
// srcroot are borrowed; neither is modified or freed. NULL and non-code items
// are rejected before the sidecar filesystem is accessed.
char *read_itemsource_in_srcroot(ITEM_t *item, const char *srcroot,
                                 char *detail, size_t detail_size);
bool itemstore_durability_requires_sync(ITEMSTORE_DURABILITY_e durability);
// Process-global test hook; install or reset it only while the process is
// quiescent, and run hook-using tests serially.
void itemstore_set_sync_hook_for_tests(ITEMSTORE_SYNC_HOOK_t hook);

// Other item-related API functions
bool is_valid_layer(const char *str);
void set_error_item(ITEM_t *root, const int errnum,
                    const char *errdetail, ITEM_t *current_item);
void set_compiler_error_item(ITEM_t *root, const CompilerDiagnostic *diag);
void clear_error_item(ITEM_t *root);
