// Item lookup and registry context.
// Licensed under the MIT License - see LICENSE file for details.

#include <string.h>
#include <stdlib.h>
#include <limits.h>

#include "log.h"
#include "item_internal.h"

static void assign_store(ITEM_t *item, ITEMSTORE_t *store) {
  if (!item) return;
  item->store = store;
  for (size_t i = 0; i < item_children_count(item->children); i++) {
    assign_store(item_children_at(item->children, i), store);
  }
}

ITEMSTORE_t *itemstore_create(const char *name) {
  ITEMSTORE_t *store = calloc(1, sizeof(*store));
  if (!store) return NULL;
  store->context.topology_revision = 1;
  store->context.payload_revision = 1;
  store->context.sync_hook = itemstore_default_sync_hook;
  store->root = make_root_item(name);
  if (!store->root) { free(store); return NULL; }
  assign_store(store->root, store);
  return store;
}

ITEMSTORE_t *itemstore_create_boot(const char *name, uint8_t *bytecode,
                                   uint32_t bytecode_len) {
  ITEMSTORE_t *store = calloc(1, sizeof(*store));
  if (!store) return NULL;
  if (bytecode_len > (uint32_t)INT_MAX) { free(store); return NULL; }
  if (bytecode_len > 0u && !bytecode) { free(store); return NULL; }
  uint8_t *owned_bytecode = NULL;
  if (bytecode_len > 0u) {
    owned_bytecode = malloc(bytecode_len);
    if (!owned_bytecode) { free(store); return NULL; }
    memcpy(owned_bytecode, bytecode, bytecode_len);
  }
  store->context.topology_revision = 1;
  store->context.payload_revision = 1;
  store->context.sync_hook = itemstore_default_sync_hook;
  store->root = make_item(name, NULL, ITEM_code, VALUE_NIL, owned_bytecode,
                          (int)bytecode_len);
  if (!store->root) {
    free(store);
    return NULL;
  }
  free(bytecode);
  assign_store(store->root, store);
  return store;
}

ITEM_t *itemstore_root(ITEMSTORE_t *store) { return store ? store->root : NULL; }

void itemstore_destroy(ITEMSTORE_t *store) {
  if (!store) return;
  destroy_item(store->root);
  free(store);
}

ITEMSTORE_t *itemstore_load_with_options(const char *filename, bool strict_validation) {
  ITEM_t *root = load_itemstore_with_options(filename, strict_validation);
  if (!root) return NULL;
  ITEMSTORE_t *store = calloc(1, sizeof(*store));
  if (!store) { destroy_item(root); return NULL; }
  store->context.topology_revision = 1;
  store->context.payload_revision = 1;
  store->context.sync_hook = itemstore_default_sync_hook;
  store->root = root;
  assign_store(root, store);
  return store;
}

ITEMSTORE_t *itemstore_load(const char *filename) {
  ITEM_t *root = load_itemstore(filename);
  if (!root) return NULL;
  ITEMSTORE_t *store = calloc(1, sizeof(*store));
  if (!store) { destroy_item(root); return NULL; }
  store->context.topology_revision = 1;
  store->context.payload_revision = 1;
  store->context.sync_hook = itemstore_default_sync_hook;
  store->root = root;
  assign_store(root, store);
  return store;
}

bool itemstore_save_with_options(const char *filename, ITEMSTORE_t *store,
                                 ITEMSTORE_DURABILITY_e durability) {
  return store && save_itemstore_with_options(filename, store->root, durability);
}
bool itemstore_save(const char *filename, ITEMSTORE_t *store) {
  return store && save_itemstore(filename, store->root);
}

ITEMSTORE_SAVE_RESULT_e itemstore_save_no_replace(
    const char *filename, ITEMSTORE_t *store,
    ITEMSTORE_DURABILITY_e durability) {
  return store ? save_itemstore_no_replace(filename, store->root, durability)
               : ITEMSTORE_SAVE_FAILURE;
}

ITEMSTORE_t *itemstore_owner(const ITEM_t *item) { return item ? item->store : NULL; }
ITEM_e item_kind(const ITEM_t *item) { return item ? item->type : ITEM_value; }
const char *item_layer_name(const ITEM_t *item) { return item ? item->name : NULL; }
ITEM_t *item_parent(const ITEM_t *item) { return item ? item->parent : NULL; }
const VALUE_t *item_value(const ITEM_t *item) {
  return item && item->type == ITEM_value ? &item->value : NULL;
}
const uint8_t *item_bytecode(const ITEM_t *item) {
  return item && item->type == ITEM_code ? item->bytecode : NULL;
}
uint32_t item_bytecode_length(const ITEM_t *item) {
  return item && item->type == ITEM_code ? item->bytecode_len : 0;
}
size_t item_child_count(const ITEM_t *item) {
  return item ? item_children_count(item->children) : 0;
}
ITEM_t *item_child_at(const ITEM_t *item, size_t index) {
  return item ? item_children_at(item->children, index) : NULL;
}
bool item_is_in_use(const ITEM_t *item) {
  return item && item->execution_pins != 0;
}
void item_enter_use(ITEM_t *item) {
  if (item && item->execution_pins != UINT32_MAX) item->execution_pins++;
}
void item_leave_use(ITEM_t *item) {
  // UINT32_MAX is a permanent saturated state: an additional enter cannot
  // be represented, so releases must not undercount outstanding pins.
  if (item && item->execution_pins != 0 &&
      item->execution_pins != UINT32_MAX) item->execution_pins--;
}

static ITEMSTORE_CONTEXT_t *context_for_root(const ITEM_t *root) {
  ITEMSTORE_t *store = root ? root->store : NULL;
  return store ? &store->context : NULL;
}

void itemstore_bump_topology_revision_for(const ITEM_t *item) {
  ITEMSTORE_CONTEXT_t *ctx = context_for_root(item);
  if (!ctx) return;
  ctx->topology_revision++;
  memset(ctx->fetchitem_cache, 0, sizeof(ctx->fetchitem_cache));
}

void itemstore_bump_payload_revision_for(const ITEM_t *item) {
  ITEMSTORE_CONTEXT_t *ctx = context_for_root(item);
  if (!ctx) return;
  ctx->payload_revision++;
}

void itemstore_invalidate_cache_for(const ITEM_t *item) {
  itemstore_bump_topology_revision_for(item);
}

uint64_t itemstore_topology_revision(const ITEMSTORE_t *store) {
  return store ? store->context.topology_revision : 0;
}
uint64_t itemstore_payload_revision(const ITEMSTORE_t *store) {
  return store ? store->context.payload_revision : 0;
}
uint64_t itemstore_cache_hits(const ITEMSTORE_t *store) {
  return store ? store->context.fetchitem_cache_hits : 0;
}
uint64_t itemstore_cache_misses(const ITEMSTORE_t *store) {
  return store ? store->context.fetchitem_cache_misses : 0;
}

static uint32_t fetchitem_cache_hash(const char *key) {
  return murmur3_32(key, strlen(key), 0x5EED1234u);
}

ITEM_t *find_item_cached(ITEM_t *root, const char *item_name, bool *found) {
  ITEMSTORE_CONTEXT_t *ctx = context_for_root(root);
  if (found) *found = false;
  if (!ctx) return find_item(root, item_name);
  if (!validate_item_name_relative(root, item_name, "find_item_cached")) {
    return NULL;
  }

  size_t item_name_len = strlen(item_name);

  uint32_t index =
      fetchitem_cache_hash(item_name) & (FETCHITEM_CACHE_SIZE - 1u);
  FETCHITEM_CACHE_ENTRY_t *entry = &ctx->fetchitem_cache[index];

  if (entry->valid && entry->topology_revision == ctx->topology_revision
      && entry->root == root
      && strcmp(entry->key, item_name) == 0) {
    ctx->fetchitem_cache_hits++;
    if (found) *found = entry->found;
    logverbose("itemcache hit: %s (hits=%llu misses=%llu)\n", item_name,
               (unsigned long long)ctx->fetchitem_cache_hits,
               (unsigned long long)ctx->fetchitem_cache_misses);
    return entry->item;
  }

  ctx->fetchitem_cache_misses++;
  ITEM_t *item = find_item_unchecked(root, item_name);
  entry->valid = true;
  entry->topology_revision = ctx->topology_revision;
  entry->root = root;
  entry->item = item;
  entry->found = (item != NULL);
  memcpy(entry->key, item_name, item_name_len + 1);
  if (found) *found = entry->found;
  logverbose("itemcache miss: %s (hits=%llu misses=%llu)\n", item_name,
             (unsigned long long)ctx->fetchitem_cache_hits,
             (unsigned long long)ctx->fetchitem_cache_misses);
  return item;
}

ITEM_t *find_item_unchecked(ITEM_t *root, const char *item_name) {
  ITEM_t *current_item = root;
  const char *current_pos = item_name;

  while (current_item != NULL && *current_pos != '\0') {
    // Find the length of the next layer of the item
    const char *next_dot = strchr(current_pos, '.');
    size_t layer_len = (next_dot != NULL)
        ? (size_t)(next_dot - current_pos)
        : strlen(current_pos);
    // Move to the next layer of the item
    current_item = item_children_lookup_span(current_item->children,
                                              current_pos, layer_len);
    // If there's no next dot, we've reached the last layer
    if (next_dot == NULL) {
      break;
    }
    // Otherwise, move past the dot to the beginning of the next layer
    current_pos = next_dot + 1;
  }
  return current_item;
}

ITEM_t *find_item(ITEM_t *root, const char *item_name) {
  // Function to dereference an item by a multi-layer item.
  if (!validate_item_name_relative(root, item_name, "find_item")) {
    return NULL;
  }
  return find_item_unchecked(root, item_name);
}
