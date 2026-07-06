// Item lookup and registry context.
// Licensed under the MIT License - see LICENSE file for details.

#include <string.h>

#include "log.h"
#include "item_internal.h"

static ITEMSTORE_CONTEXT_t default_itemstore_context = {
  .generation = 1,
  .sync_hook = itemstore_default_sync_hook,
};

ITEMSTORE_CONTEXT_t *itemstore_default_context(void) {
  return &default_itemstore_context;
}

void itemstore_bump_generation(void) {
  itemstore_default_context()->generation++;
}

uint64_t get_itemstore_generation(void) {
  return itemstore_default_context()->generation;
}

static uint32_t fetchitem_cache_hash(const char *key) {
  return murmur3_32(key, strlen(key), 0x5EED1234u);
}

ITEM_t *find_item_cached(ITEM_t *root, const char *item_name, bool *found) {
  ITEMSTORE_CONTEXT_t *ctx = itemstore_default_context();
  uint32_t index =
      fetchitem_cache_hash(item_name) & (FETCHITEM_CACHE_SIZE - 1u);
  FETCHITEM_CACHE_ENTRY_t *entry = &ctx->fetchitem_cache[index];

  if (entry->valid && entry->generation == ctx->generation
      && entry->root == root
      && strcmp(entry->key, item_name) == 0) {
    ctx->fetchitem_cache_hits++;
    if (found) *found = entry->found;
    DISASS_LOG("itemcache hit: %s (hits=%llu misses=%llu)\n", item_name,
               (unsigned long long)ctx->fetchitem_cache_hits,
               (unsigned long long)ctx->fetchitem_cache_misses);
    return entry->item;
  }

  ctx->fetchitem_cache_misses++;
  ITEM_t *item = find_item(root, item_name);
  entry->valid = true;
  entry->generation = ctx->generation;
  entry->root = root;
  entry->item = item;
  entry->found = (item != NULL);
  strncpy(entry->key, item_name, MAX_ITEM_NAME - 1);
  entry->key[MAX_ITEM_NAME - 1] = '\0';
  if (found) *found = entry->found;
  DISASS_LOG("itemcache miss: %s (hits=%llu misses=%llu)\n", item_name,
             (unsigned long long)ctx->fetchitem_cache_hits,
             (unsigned long long)ctx->fetchitem_cache_misses);
  return item;
}

ITEM_t *find_item(ITEM_t *root, const char *item_name) {
  // Function to dereference an item by a multi-layer item.
  if (!validate_item_name(item_name, "find_item")) {
    return NULL;
  }
  ITEM_t *current_item = root;
  const char *current_pos = item_name;
  char layer[33]; // 32 characters + 1 for null-terminator

  while (current_item != NULL && *current_pos != '\0') {
    // Find the length of the next layer of the item
    const char *next_dot = strchr(current_pos, '.');
    size_t layer_len = (next_dot != NULL) ? (size_t)(next_dot - current_pos) : strlen(current_pos);
    // Since the constraints guarantee that layer_len will be <= 32,
    // we don't need to check for overflow
    memcpy(layer, current_pos, layer_len);
    layer[layer_len] = '\0'; // Null-terminate the layer string
    // Move to the next layer of the item
    current_item = search_hashtable(current_item->children, layer);
    // If there's no next dot, we've reached the last layer
    if (next_dot == NULL) {
      break;
    }
    // Otherwise, move past the dot to the beginning of the next layer
    current_pos = next_dot + 1;
  }
  return current_item;
}
