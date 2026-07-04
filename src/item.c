// The Item.  The nub and the gist of the whole brouhaha in a nutshell.

// Licensed under the MIT License - see LICENSE file for details.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libgen.h>
#include <errno.h>
#include <limits.h>
#ifdef _WIN32
#include <process.h>
#else
#include <unistd.h>
#endif

#include "config.h"
#include "error.h"
#include "util.h"
#include "memory.h"
#include "log.h"
#include "item.h"
#include "bytecode_verify.h"

static uint64_t itemstore_generation = 1;

static long current_process_id(void) {
#ifdef _WIN32
  return (long)_getpid();
#else
  return (long)getpid();
#endif
}

static bool sync_itemstore_file(FILE *file, const char *path) {
#ifdef _WIN32
  (void)file;
  (void)path;
  return true;
#else
  if (fsync(fileno(file)) != 0) {
    logerr("Failed to sync temporary itemstore %s: %s\n", path,
           strerror(errno));
    return false;
  }
  return true;
#endif
}

static ITEMSTORE_SYNC_HOOK_t itemstore_sync_hook = sync_itemstore_file;

void itemstore_set_sync_hook_for_tests(ITEMSTORE_SYNC_HOOK_t hook) {
  itemstore_sync_hook = hook != NULL ? hook : sync_itemstore_file;
}

bool itemstore_durability_requires_sync(ITEMSTORE_DURABILITY_e durability) {
  return durability != ITEMSTORE_DURABLE_FAST;
}

#define FETCHITEM_CACHE_SIZE 256u
_Static_assert((FETCHITEM_CACHE_SIZE & (FETCHITEM_CACHE_SIZE - 1u)) == 0,
               "fetch-item cache size must be a power of two");
typedef struct {
  bool valid;
  bool found;
  char key[MAX_ITEM_NAME];
  uint64_t generation;
  ITEM_t *root;
  ITEM_t *item;
} FETCHITEM_CACHE_ENTRY_t;

static FETCHITEM_CACHE_ENTRY_t fetchitem_cache[FETCHITEM_CACHE_SIZE];
static uint64_t fetchitem_cache_hits = 0;
static uint64_t fetchitem_cache_misses = 0;

static inline void bump_itemstore_generation(void) {
  itemstore_generation++;
}

uint64_t get_itemstore_generation(void) {
  return itemstore_generation;
}

static uint32_t fetchitem_cache_hash(const char *key) {
  return murmur3_32(key, strlen(key), 0x5EED1234u);
}

ITEM_t *find_item_cached(ITEM_t *root, const char *item_name, bool *found) {
  uint32_t index =
      fetchitem_cache_hash(item_name) & (FETCHITEM_CACHE_SIZE - 1u);
  FETCHITEM_CACHE_ENTRY_t *entry = &fetchitem_cache[index];

  if (entry->valid && entry->generation == itemstore_generation
      && entry->root == root
      && strcmp(entry->key, item_name) == 0) {
    fetchitem_cache_hits++;
    if (found) *found = entry->found;
    DISASS_LOG("itemcache hit: %s (hits=%llu misses=%llu)\n", item_name,
               (unsigned long long)fetchitem_cache_hits,
               (unsigned long long)fetchitem_cache_misses);
    return entry->item;
  }

  fetchitem_cache_misses++;
  ITEM_t *item = find_item(root, item_name);
  entry->valid = true;
  entry->generation = itemstore_generation;
  entry->root = root;
  entry->item = item;
  entry->found = (item != NULL);
  strncpy(entry->key, item_name, MAX_ITEM_NAME - 1);
  entry->key[MAX_ITEM_NAME - 1] = '\0';
  if (found) *found = entry->found;
  DISASS_LOG("itemcache miss: %s (hits=%llu misses=%llu)\n", item_name,
             (unsigned long long)fetchitem_cache_hits,
             (unsigned long long)fetchitem_cache_misses);
  return item;
}


// The configuration object, defined in sin.c
extern CONFIG_t config;

static bool validate_item_name(const char *item_name,
                                               const char *func_name) {
  /*
   * Item names are already-assembled strings, not numeric values. Integer
   * layers may be assembled by the compiler/runtime using base-10 integer
   * text, but float values are rejected before this API is called. Therefore
   * this validator treats dots only as layer separators: "1.0" is the two
   * layers "1" and "0", not the float spellings "1.0" or "1.00"; +0.0/-0.0
   * and NaN payloads have no item-name representation.
   */
  if (!item_name || *item_name == '\0') {
    logerr("%s called with empty item name.\n", func_name);
    return false;
  }

  const char *segment_start = item_name;
  while (true) {
    const char *dot = strchr(segment_start, '.');
    size_t layer_len = (dot != NULL) ? (size_t)(dot - segment_start)
                                     : strlen(segment_start);

    if (layer_len == 0) {
      logerr("%s called with malformed item name '%s': empty layer.\n",
                                                     func_name, item_name);
      return false;
    }
    if (layer_len > 32) {
      logerr("%s called with malformed item name '%s': layer too long.\n",
                                                     func_name, item_name);
      return false;
    }

    if (dot == NULL) return true;
    segment_start = dot + 1;
  }
}

HASHTABLE_t *create_hashtable(int size) {
  // Create a hashtable with the given number of buckets
  HASHTABLE_t *hashtable = allocate_hashtable();
  hashtable->size = size;
  hashtable->entry_count = 0;
  hashtable->table = calloc((size_t)size, sizeof *hashtable->table);
  return hashtable;
}

static void create_ordered_array_with_capacity(ITEM_t *item,
                                               size_t capacity) {
  item->ordered_size = 0;
  item->ordered_capacity = capacity;
  item->ordered_array = capacity > 0
      ? (ITEM_t **)malloc(sizeof(ITEM_t *) * capacity)
      : NULL;
}

void create_ordered_array(ITEM_t *item) {
  // This must be called after a new hashtable has been created,
  // but must not be called when a hashtable is resized, or memory
  // will leak like a very leaky thing.
  create_ordered_array_with_capacity(item, ITEM_ARRAY_INIT_CAPACITY);
}

uint32_t simple_hash(const char *key, size_t len) {
  // It is pointless to create a 4-byte hash for a key of 4 bytes or less
  uint32_t hash = 0;
  memcpy(&hash, key, len);
  return hash;
}

HASHTABLE_t *resize_hashtable(HASHTABLE_t *oldhashtable, int newsize) {
  // Create a new hash table with the new size
  HASHTABLE_t *newhashtable = create_hashtable(newsize);
  newhashtable->entry_count = oldhashtable->entry_count;
  // Rehash all the existing entries
  for (uint32_t i = 0; i < (oldhashtable)->size; i++) {
    ENTRY_t *current_entry = (oldhashtable)->table[i];
    while (current_entry != NULL) {
      // Save the next entry before we move this one
      ENTRY_t *nextEntry = current_entry->next;
      // Recalculate the hash index for the current entry's key
      size_t keylen = strlen(current_entry->key);
      uint32_t newhashindex;
      if (keylen <= 4) {
        newhashindex = simple_hash(current_entry->key, keylen);
      } else {
        newhashindex = murmur3_32(current_entry->key, keylen, 0);
      }
      newhashindex %= newhashtable->size;
      // Move the existing entry to the head of the new collision chain.
      current_entry->next = newhashtable->table[newhashindex];
      newhashtable->table[newhashindex] = current_entry;
      // Move to the next entry
      current_entry = nextEntry;
    }
  }
  // Free the old table's array of pointers
  // - but not the entries themselves as we reused them
  free((oldhashtable)->table);
  // Free the old hash table struct
  deallocate_hashtable(oldhashtable);
  return newhashtable;
}

void resize_ordered_array(ITEM_t *item) {
  if (item->ordered_size < item->ordered_capacity) return;

  size_t required = item->ordered_size + 1;
  size_t new_capacity = item->ordered_capacity > 0
      ? item->ordered_capacity
      : ITEM_ARRAY_INIT_CAPACITY;
  while (new_capacity < required) {
    if (new_capacity > SIZE_MAX / 2) {
      logerr("Cannot grow ordered item array beyond %zu entries.\n",
             new_capacity);
      abort();
    }
    new_capacity *= 2;
  }
  item->ordered_array = (ITEM_t **)realloc(
      item->ordered_array, new_capacity * sizeof(ITEM_t *));
  item->ordered_capacity = new_capacity;
}

float calculate_load_factor(HASHTABLE_t *hashtable) {
  return (float)hashtable->entry_count / (float)hashtable->size;
}

HASHTABLE_t *maybe_resize_hashtable(HASHTABLE_t *hashtable) {
  float loadfactor = calculate_load_factor(hashtable);
  const float maxloadfactor = 0.75; // Tweak for performance as needed
  if (loadfactor > maxloadfactor) {
    // Double the size - maybe tweak for performance
    int newsize = (hashtable->size * 2) + 1;
    return resize_hashtable(hashtable, newsize);
  }
  // hashtable has not changed.
  return hashtable;
}

void insert_hashtable(HASHTABLE_t *hashtable, const char *key, ITEM_t *child) {
  size_t keylen = strlen(key);
  uint32_t hashindex;
  // Compute the hash - use the key itself if 4 bytes or less
  if (keylen <= 4) {
    // Use the key itself for the hash value
    hashindex = simple_hash(key, keylen);
  } else {
    // Use the MurmurHash function for longer keys.
    hashindex = murmur3_32(key, keylen, 0);
  }
  hashindex %= hashtable->size;
  // Create a new entry
  ENTRY_t *newEntry = allocate_entry();
  newEntry->key = strdup(key);
  newEntry->child = child;
  newEntry->next = hashtable->table[hashindex];
  hashtable->table[hashindex] = newEntry;
  hashtable->entry_count++;
}

ITEM_t *search_hashtable(HASHTABLE_t *hashtable, const char *key) {
  size_t keylen = strlen(key);
  uint32_t hashindex;
  // Check if the key is less than or equal to 4 characters.
  if (keylen <= 4) {
    // Use the key itself for the hash value.
    hashindex = simple_hash(key, keylen);
  } else {
    // Use the MurmurHash function for longer keys.
    hashindex = murmur3_32(key, keylen, 0);
  }
  hashindex %= hashtable->size;
  ENTRY_t *current = hashtable->table[hashindex];

  while (current) {
    if (strcmp(current->key, key) == 0) {
      return current->child;
    }
    current = current->next;
  }
  return NULL;
}

void delete_hashtable(HASHTABLE_t *hashtable, const char *key) {
    size_t keylen = strlen(key);
  uint32_t hashindex;

  // Check if the key is less than or equal to 4 characters.
  if (keylen <= 4) {
    // Use the key itself for the hash value.
    hashindex = simple_hash(key, keylen);
  } else {
    // Use the MurmurHash function for longer keys.
    hashindex = murmur3_32(key, keylen, 0);
  }
  hashindex %= hashtable->size;
  ENTRY_t *current = hashtable->table[hashindex];
  ENTRY_t *previous = NULL;
  while (current) {
    if (strcmp(current->key, key) == 0) {
      if (previous == NULL) {
        // Remove the first entry in the chain
        hashtable->table[hashindex] = current->next;
      } else {
        // Remove the entry from the chain
        previous->next = current->next;
      }
      free(current->key);
      deallocate_entry(current);
      hashtable->entry_count--;
      return;
    }
    previous = current;
    current = current->next;
  }
}

void free_hashtable(HASHTABLE_t* hashtable) {
  for (uint32_t i = 0; i < hashtable->size; i++) {
    ENTRY_t *current = hashtable->table[i];
    while (current) {
      ENTRY_t *temp = current;
      current = current->next;
      destroy_item(temp->child);
      free(temp->key);
      deallocate_entry(temp);
    }
  }
  free(hashtable->table);
  deallocate_hashtable(hashtable);
}

uint32_t murmur3_32(const char *key, size_t len, uint32_t seed) {
  // This is an implementation of MurmurHash3
  uint32_t c1 = 0xcc9e2d51;
  uint32_t c2 = 0x1b873593;
  uint32_t r1 = 15;
  uint32_t r2 = 13;
  uint32_t m = 5;
  uint32_t n = 0xe6546b64;
  uint32_t hash = seed;
  const int nblocks = len / 4;
  const uint32_t* blocks = (const uint32_t*)key;
  int i;
  for (i = 0; i < nblocks; i++) {
    uint32_t k = blocks[i];
    k *= c1;
    k = (k << r1) | (k >> (32 - r1));
    k *= c2;
    hash ^= k;
    hash = ((hash << r2) | (hash >> (32 - r2))) * m + n;
  }
  const uint8_t *tail = (const uint8_t*)(key + nblocks * 4);
  uint32_t k1 = 0;
  switch (len & 3) {
    case 3:
      k1 ^= tail[2] << 16;
      __attribute__((fallthrough));
    case 2:
      k1 ^= tail[1] << 8;
      __attribute__((fallthrough));
    case 1:
      k1 ^= tail[0];
      k1 *= c1;
      k1 = (k1 << r1) | (k1 >> (32 - r1));
      k1 *= c2;
      hash ^= k1;
  }
  hash ^= len;
  hash ^= (hash >> 16);
  hash *= 0x85ebca6b;
  hash ^= (hash >> 13);
  hash *= 0xc2b2ae35;
  hash ^= (hash >> 16);
  return hash;
}

char *substr(const char *str, size_t begin, size_t len) {
  // Helper function to create a substring
  if (str == NULL || strlen(str) == 0 || strlen(str) < (begin + len))  {
    return NULL;
  } else {
    return strndup(str + begin, len);
  }
}

ENTRY_t *allocate_entry() {
  // Allocator API: Gimme a new ENTRY_t
  return malloc(sizeof(ENTRY_t));
}

HASHTABLE_t *allocate_hashtable() {
  // Allocator API: Gimme a new HASHTABLE_t
  return malloc(sizeof(HASHTABLE_t));
}

ITEM_t *allocate_item() {
  // Allocator API: Gimme a new Item
  return malloc(sizeof(ITEM_t));
}

void deallocate_entry(ENTRY_t *entry) {
  // Allocator API: Take this ENTRY_t back.
  free(entry);
}

void deallocate_hashtable(HASHTABLE_t *hashtable) {
  // Allocator API: Take this HashTable back.
  free(hashtable);
}

void deallocate_item(ITEM_t *item) {
  // Allocator API: Take this Item back.
  free(item);
}

static uint32_t hashtable_buckets_for_entries(uint32_t entry_count) {
  if (entry_count == 0) return 1;
  return (uint32_t)(((uint64_t)entry_count * 4u + 2u) / 3u);
}

static ITEM_t *construct_item(const char *name, ITEM_t *parent, ITEM_e type,
                              VALUE_t value, uint8_t *bytecode, int len,
                              uint32_t expected_children,
                              bool presize_children) {
  ITEM_t *item = allocate_item();
  item->parent = parent;
  item->inuse = false;
  item->type = type;
  item->bytecode = NULL;
  item->bytecode_len = 0;
  // There are two types of items.  Those which don't contain a value
  // MUST contain bytecode.
  if (type == ITEM_value) {
    item->value = value;
  } else {
    // The bytecode is allocated elsewhere, before calling this function.
    item->bytecode = bytecode;
    item->bytecode_len = len;
  }
  strncpy(item->name, name, strlen(name)+1);
  uint32_t bucket_count = presize_children
      ? hashtable_buckets_for_entries(expected_children)
      : 16u;
  size_t ordered_capacity = presize_children
      ? expected_children
      : ITEM_ARRAY_INIT_CAPACITY;
  item->children = create_hashtable((int)bucket_count);
  create_ordered_array_with_capacity(item, ordered_capacity);

  if (parent != NULL) {
    insert_hashtable(parent->children, name, item);
    parent->children = maybe_resize_hashtable(parent->children);
    resize_ordered_array(parent);
    parent->ordered_array[parent->ordered_size++] = item;
  }
  return item;
}

ITEM_t *make_item(const char *name, ITEM_t *parent, ITEM_e type,
                                VALUE_t value, uint8_t *bytecode, int len) {
  // Note that for performance reasons this function does not check
  // to see if the item already exists at this layer.  You MUST
  // check that before you call this function!
  return construct_item(name, parent, type, value, bytecode, len, 0, false);
}

ITEM_t *make_root_item(const char* name) {
  VALUE_t value = {.type = VALUE_int, .i = 0};
  return construct_item(name, NULL, ITEM_value, value, NULL, 0, 0, false);
}

static ITEM_t *make_loaded_item(const char *name, ITEM_t *parent, ITEM_e type,
                                VALUE_t value, uint8_t *bytecode, int len,
                                uint32_t expected_children) {
  return construct_item(name, parent, type, value, bytecode, len,
                        expected_children, true);
}

void destroy_item(ITEM_t *item) {
  if (item->type == ITEM_code) {
    free(item->bytecode);
  } else if (item->type == ITEM_value) {
    value_free(&item->value);
  }
  // Free the item's innards
  free_hashtable(item->children);
  free(item->ordered_array);
  // Then free the item
  deallocate_item(item);
}

ITEM_t *insert_item(ITEM_t *root, const char *item_name, VALUE_t value) {
  // Function to insert a new item into the tree at the specified node.
  if (!validate_item_name(item_name, "insert_item")) {
    return NULL;
  }
  // If layers of the item don't exist, they are created with a default
  // value of 0.
  ITEM_t *current_item = root;
  const char *current_pos = item_name;
  // Buffer to hold each layer of the item, with space for null terminator
  char layer[33];
  ITEMDEBUG_LOG("Creating new item %s\n", item_name);
  while (current_item != NULL && *current_pos != '\0') {
    const char *next_dot = strchr(current_pos, '.');
    size_t layer_len = (next_dot != NULL) ?
                     (size_t)(next_dot - current_pos) : strlen(current_pos);
    // Copy the current layer into the buffer and null-terminate it
    memcpy(layer, current_pos, layer_len);
    layer[layer_len] = '\0';
    // Check if the current layer exists as a child of the current item
    ITEM_t *child_item = search_hashtable(current_item->children, layer);
    if (child_item == NULL) {
      // If the child does not exist, create it with a default value of 0
      VALUE_t nil = {VALUE_nil, {0}};
      child_item = make_item(layer, current_item, ITEM_value, nil, NULL, 0);
    }
    // Move to the child item
    current_item = child_item;
    if (next_dot == NULL) {
      // If there's no next dot, we've reached the last layer
      // Possibly free currently in-use memory
      // (it might have been newly-created, or might already exist)
      if (current_item->type == ITEM_value) {
          value_free(&current_item->value);
      } else if (current_item->type == ITEM_code) {
        if (current_item->inuse) {
          char name[MAX_ITEM_NAME];
          get_itemname(current_item, name);
          logerr("Cannot delete item %s: currently in use.\n", name);
          return NULL;
        }
        if (current_item->bytecode_len > 0) {
          free(current_item->bytecode);
        }
      }
      current_item->value = value;
      break;
    }
    // Otherwise, move past the dot to the beginning of the next layer
    current_pos = next_dot + 1;
  }
  // Return a pointer to the last-created item
  bump_itemstore_generation();
  return current_item;
}

ITEM_t *insert_code_item(ITEM_t *root, const char *item_name, uint32_t len,
                                                      uint8_t *bytecode) {
  if (!validate_item_name(item_name, "insert_code_item")) {
    return NULL;
  }
  // This function is basically the same as insert_item() but creates a
  // code item instead of a value item.
  ITEM_t *current_item = root;
  const char *current_pos = item_name;
  // Buffer to hold each layer of the item, with space for null terminator
  char layer[33];
  ITEMDEBUG_LOG("Creating new item %s\n", item_name);
  while (current_item != NULL && *current_pos != '\0') {
    const char *next_dot = strchr(current_pos, '.');
    size_t layer_len = (next_dot != NULL) ?
                     (size_t)(next_dot - current_pos) : strlen(current_pos);
    // Copy the current layer into the buffer and null-terminate it
    memcpy(layer, current_pos, layer_len);
    layer[layer_len] = '\0';
    // Check if the current layer exists as a child of the current item
    ITEM_t *child_item = search_hashtable(current_item->children, layer);
    if (child_item == NULL) {
      // If the child does not exist, create it with a default value of 0
      VALUE_t nil = {VALUE_nil, {0}};
      child_item = make_item(layer, current_item, ITEM_value, nil, NULL, 0);
    }
    // Move to the child item
    current_item = child_item;
    if (next_dot == NULL) {
      // If there's no next dot, we've reached the last layer
      // It's code item, remember!
      if (current_item->type == ITEM_value) {
        value_free(&current_item->value);
      }
      current_item->type = ITEM_code;
      current_item->value.type = VALUE_nil; // Just to be safe
      if (current_item->bytecode_len > 0) {
        free(current_item->bytecode);
      }
      current_item->bytecode_len = len;
      current_item->bytecode = bytecode;
      break;
    }
    // Otherwise, move past the dot to the beginning of the next layer
    current_pos = next_dot + 1;
  }
  // Return a pointer to the last-created item
  bump_itemstore_generation();
  return current_item;
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

ITEM_t *find_item_by_index(ITEM_t *parent, const size_t index) {
  // Given the parent item, return the indexed child.
  if (index >= parent->ordered_size) {
    // No item at that index.
    return NULL;
  }
  return parent->ordered_array[index];
}

void delete_item(ITEM_t *root, const char *item_name) {
  // Find an item and then delete it and all of its children.
  if (!validate_item_name(item_name, "delete_item")) {
    return;
  }
  ITEM_t *item = find_item(root, item_name);
  if (item) {
    if (item->inuse) {
      char name[MAX_ITEM_NAME];
      get_itemname(item, name);
      logerr("Cannot delete item %s: currently in use.\n", name);
      return;
    }
    // We don't care about items that don't exist, just silently ignore the
    // delete request.  It's not there anyway, so why the complaining?
    // First, remove the item from its parent's hashtable:
    delete_hashtable(item->parent->children, item->name);
    // Remove from order array
    for (size_t i = 0; i < item->parent->ordered_size; i++) {
      if (item->parent->ordered_array[i] == item) {
        // Shift elements left
        for (size_t j = i; j < item->parent->ordered_size - 1; j++) {
          item->parent->ordered_array[j] = item->parent->ordered_array[j + 1];
        }
        item->parent->ordered_size--;
        break;
      }
    }
    // Now we have isolated this item, delete it and all its children.
    destroy_item(item);
    bump_itemstore_generation();
    ITEMDEBUG_LOG("Item %s has been deleted, along with all of its children.\n",
                                                                 item_name);
  }
}

void set_item(ITEM_t *root, const char *item_name, VALUE_t value) {
  // Find an item, and set its value.
  if (!validate_item_name(item_name, "set_item")) {
    return;
  }
  // If the item does not exist, it will be created, and then set.
  ITEMDEBUG_LOG("Trying to set item '%s'\n", item_name);
  ITEM_t *item = find_item(root, item_name);
  if (item) {
    // Item exists, so just update its value.
    value_replace(&item->value, value);
  } else {
    // Item doesn't exist, so create it.
    insert_item(root, item_name, value);
  }
}

void get_itemname(ITEM_t *item, char *itemname) {
  // Returns the full name of an item.  The itemname buffer must be
  // at least MAX_ITEM_NAME in length.
  if (item->parent->parent) {
    // We stop at the item before the root item.
    get_itemname(item->parent, itemname);
    strcat(itemname, ".");
    strcat(itemname, item->name);
  } else {
    strcpy(itemname, item->name);
  }
}

char *get_itemfilename(ITEM_t *item) {
  // Returns the filename of the item (only relevant if it is a source
  // item).  The return value will need to be freed by the caller.
  char *filename, *p;
  char itemname[MAX_ITEM_NAME];
  int l;

  itemname[0] = '\0';
  get_itemname(item, itemname);
  l = strlen(itemname) + strlen(config.srcroot) + 13;
  filename = malloc((size_t)l);
  p = itemname;
  while (*p) {
    if(*p == '.') *p = '/';
    p++;
  }
  snprintf(filename, l, "%s/%s/source.sin", config.srcroot, itemname);
  return filename;
}

bool save_itemsource(ITEM_t *item, char *source) {
  // Saves the item source into srcroot.
  // If the source cannot be saved for whatever reason, this is
  // reported in the error log.  The function returns true if the
  // source was saved, otherwise false.

  char *filename = get_itemfilename(item);
  // There is a much better way to do this, but I don't care right now.
  char *dircopy = strdup(filename);
  char *dir = dirname(dircopy);
  bool res = make_path(dir);
  free(dircopy);
  if (!res) {
    free(filename);
    return false;
  }
  // When we arrive here, we know that the path exists.
  FILE *out = fopen(filename, "w");
  if (!out) {
    logerr("Failed to open file %s: %s\n", filename, strerror(errno));
    free(filename);
    return false;
  }
  if (fputs(source, out) == EOF) {
    logerr("Failed to write text to file %s\n", filename);
  }
  if (fclose(out) != 0) {
    logerr("Failed to close file %s\n", filename);
  }
  free(filename);
  return true;
}

/* The on-disk contract is documented in docs/itemstore-format.md. */
#define ITEMSTORE_V1_MAGIC "SINITEM"
#define ITEMSTORE_V1_MAGIC_SIZE ((uint32_t)sizeof(ITEMSTORE_V1_MAGIC))
#define ITEMSTORE_V1_FORMAT_VERSION ((uint16_t)1u)

typedef uint8_t ITEMSTORE_ITEM_TAG_t;
enum {
  ITEMSTORE_ITEM_TAG_VALUE = 1,
  ITEMSTORE_ITEM_TAG_CODE = 2
};

typedef uint8_t ITEMSTORE_VALUE_TAG_t;
enum {
  ITEMSTORE_VALUE_TAG_INT = 0,
  ITEMSTORE_VALUE_TAG_FLOAT = 1,
  ITEMSTORE_VALUE_TAG_STRING = 2,
  ITEMSTORE_VALUE_TAG_NIL = 3,
  ITEMSTORE_VALUE_TAG_BOOL = 4
};

#define ITEMSTORE_MAX_DEPTH 8u
#define ITEMSTORE_MAX_CHILDREN_PER_ITEM 250u
#define ITEMSTORE_MAX_STRING_LEN (16u * 1024u * 1024u)
#define ITEMSTORE_MAX_BYTECODE_LEN (64u * 1024u * 1024u)

typedef struct {
  size_t depth;
  size_t max_depth;
  uint32_t max_children_per_item;
  uint32_t max_string_len;
  uint32_t max_bytecode_len;
  const char *filename;
} ITEMSTORE_READ_CTX_t;

static bool write_bytes(FILE *file, const void *data, size_t length,
                        const char *context) {
  if (length == 0) return true;

  size_t written = fwrite(data, 1, length, file);
  if (written == length) return true;

  if (ferror(file)) {
    logerr("Failed to write itemstore %s: wrote %zu of %zu bytes: %s\n",
           context, written, length, strerror(errno));
  } else {
    logerr("Failed to write itemstore %s: wrote %zu of %zu bytes.\n",
           context, written, length);
  }
  return false;
}

static bool read_bytes(FILE *file, void *data, size_t length,
                       const char *context) {
  if (length == 0) return true;

  size_t bytes_read = fread(data, 1, length, file);
  if (bytes_read == length) return true;

  if (ferror(file)) {
    logerr("Failed to read itemstore %s: read %zu of %zu bytes: %s\n",
           context, bytes_read, length, strerror(errno));
  } else if (feof(file)) {
    logerr("Failed to read itemstore %s: unexpected end of file after %zu "
           "of %zu bytes.\n", context, bytes_read, length);
  } else {
    logerr("Failed to read itemstore %s: read %zu of %zu bytes.\n",
           context, bytes_read, length);
  }
  return false;
}

static bool write_u8(FILE *file, uint8_t value, const char *context) {
  return write_bytes(file, &value, sizeof(value), context);
}

static bool write_u16_le(FILE *file, uint16_t value, const char *context) {
  uint8_t bytes[2] = {
    (uint8_t)(value & UINT16_C(0xff)),
    (uint8_t)((value >> 8) & UINT16_C(0xff))
  };
  return write_bytes(file, bytes, sizeof(bytes), context);
}

static bool write_u32_le(FILE *file, uint32_t value, const char *context) {
  uint8_t bytes[4];
  for (size_t i = 0; i < sizeof(bytes); i++) {
    bytes[i] = (uint8_t)((value >> (i * 8)) & UINT32_C(0xff));
  }
  return write_bytes(file, bytes, sizeof(bytes), context);
}

static bool write_u64_le(FILE *file, uint64_t value, const char *context) {
  uint8_t bytes[8];
  for (size_t i = 0; i < sizeof(bytes); i++) {
    bytes[i] = (uint8_t)((value >> (i * 8)) & UINT64_C(0xff));
  }
  return write_bytes(file, bytes, sizeof(bytes), context);
}

static bool read_u8(FILE *file, uint8_t *value, const char *context) {
  return read_bytes(file, value, sizeof(*value), context);
}

static bool read_u16_le(FILE *file, uint16_t *value, const char *context) {
  uint8_t bytes[2];
  if (!read_bytes(file, bytes, sizeof(bytes), context)) return false;
  *value = (uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8);
  return true;
}

static bool read_u32_le(FILE *file, uint32_t *value, const char *context) {
  uint8_t bytes[4];
  if (!read_bytes(file, bytes, sizeof(bytes), context)) return false;
  *value = 0;
  for (size_t i = 0; i < sizeof(bytes); i++) {
    *value |= (uint32_t)bytes[i] << (i * 8);
  }
  return true;
}

static bool read_u64_le(FILE *file, uint64_t *value, const char *context) {
  uint8_t bytes[8];
  if (!read_bytes(file, bytes, sizeof(bytes), context)) return false;
  *value = 0;
  for (size_t i = 0; i < sizeof(bytes); i++) {
    *value |= (uint64_t)bytes[i] << (i * 8);
  }
  return true;
}

bool write_item(FILE *file, ITEM_t *item) {
  size_t depth = 0;
  for (ITEM_t *ancestor = item->parent; ancestor != NULL;
       ancestor = ancestor->parent) {
    depth++;
  }
  if (depth > ITEMSTORE_MAX_DEPTH) {
    logerr("Failed to write itemstore item '%s': depth %zu exceeds maximum "
           "%u.\n", item->name, depth, ITEMSTORE_MAX_DEPTH);
    return false;
  }

  size_t name_len = strlen(item->name);
  if (name_len > 32) {
    logerr("Failed to write itemstore item '%s': name exceeds 32 bytes.\n",
           item->name);
    return false;
  }
  if (item->parent != NULL && (name_len == 0 || !is_valid_layer(item->name))) {
    logerr("Failed to write itemstore item: invalid layer name '%s'.\n",
           item->name);
    return false;
  }
  if (!write_u8(file, (uint8_t)name_len, "item name length")
      || !write_bytes(file, item->name, name_len, "item name")) {
    return false;
  }

  ITEMSTORE_ITEM_TAG_t item_tag;
  switch (item->type) {
    case ITEM_value: item_tag = ITEMSTORE_ITEM_TAG_VALUE; break;
    case ITEM_code: item_tag = ITEMSTORE_ITEM_TAG_CODE; break;
    default:
      logerr("Failed to write itemstore item '%s': unsupported item type %d.\n",
             item->name, item->type);
      return false;
  }
  if (!write_u8(file, item_tag, "item type tag")) return false;

  if (item->type == ITEM_value) {
    ITEMSTORE_VALUE_TAG_t value_tag;
    switch (item->value.type) {
      case VALUE_nil: value_tag = ITEMSTORE_VALUE_TAG_NIL; break;
      case VALUE_int: value_tag = ITEMSTORE_VALUE_TAG_INT; break;
      case VALUE_float: value_tag = ITEMSTORE_VALUE_TAG_FLOAT; break;
      case VALUE_str: value_tag = ITEMSTORE_VALUE_TAG_STRING; break;
      case VALUE_bool: value_tag = ITEMSTORE_VALUE_TAG_BOOL; break;
      default:
        logerr("Failed to write itemstore item '%s': unsupported value type "
               "%d.\n", item->name, item->value.type);
        return false;
    }
    if (!write_u8(file, value_tag, "value type tag")) return false;

    switch (item->value.type) {
      case VALUE_nil:
        break;
      case VALUE_int:
      {
        uint64_t payload;
        memcpy(&payload, &item->value.i, sizeof(payload));
        if (!write_u64_le(file, payload,
                          "integer payload")) return false;
        break;
      }
      case VALUE_float:
        if (!write_u64_le(file, item->value.f_bits,
                          "float payload")) return false;
        break;
      case VALUE_str:
      {
        size_t length = strlen(item->value.s);
        if (length > ITEMSTORE_MAX_STRING_LEN) {
          logerr("Failed to write itemstore string payload for '%s': length "
                 "%zu exceeds maximum %u.\n", item->name, length,
                 ITEMSTORE_MAX_STRING_LEN);
          return false;
        }
        if (!write_u32_le(file, (uint32_t)length, "string length")
            || !write_bytes(file, item->value.s, length, "string payload")) {
          return false;
        }
        break;
      }
      case VALUE_bool:
        if (!write_u8(file, item->value.i ? 1u : 0u,
                      "boolean payload")) return false;
        break;
    }
  } else if (item->type == ITEM_code) {
    if (item->bytecode_len > ITEMSTORE_MAX_BYTECODE_LEN) {
      logerr("Failed to write itemstore bytecode for '%s': length %u exceeds "
             "maximum %u.\n", item->name, item->bytecode_len,
             ITEMSTORE_MAX_BYTECODE_LEN);
      return false;
    }
    if (item->bytecode_len > 0 && item->bytecode == NULL) {
      logerr("Failed to write itemstore bytecode for '%s': length is %u but "
             "bytecode is NULL.\n", item->name, item->bytecode_len);
      return false;
    }
    if (!write_u32_le(file, item->bytecode_len, "bytecode length")) return false;
    if (!write_bytes(file, item->bytecode, item->bytecode_len,
                     "bytecode payload")) return false;
  }

  size_t numchildren = item->ordered_size;
  if (numchildren > ITEMSTORE_MAX_CHILDREN_PER_ITEM) {
    logerr("Failed to write itemstore item '%s': child count %zu exceeds "
           "maximum %u.\n", item->name, numchildren,
           ITEMSTORE_MAX_CHILDREN_PER_ITEM);
    return false;
  }
  if (!write_u32_le(file, (uint32_t)numchildren, "child count")) return false;

  for (size_t i = 0; i < item->ordered_size; i++) {
    if (!write_item(file, item->ordered_array[i])) return false;
  }
  return true;
}

bool save_itemstore(const char *filename, ITEM_t *root) {
  FILE *file = NULL;
  char *temp_path = NULL;
  bool success = false;
  long pid = current_process_id();
  int temp_path_len = snprintf(NULL, 0, "%s.tmp.%ld", filename, pid);
  if (temp_path_len < 0) {
    logerr("Failed to build temporary itemstore path for %s.\n", filename);
    return false;
  }

  temp_path = malloc((size_t)temp_path_len + 1);
  if (temp_path == NULL) {
    logerr("Failed to allocate temporary itemstore path for %s.\n", filename);
    return false;
  }
  snprintf(temp_path, (size_t)temp_path_len + 1, "%s.tmp.%ld", filename, pid);

  file = fopen(temp_path, "wb");
  if (file == NULL) {
    logerr("Failed to open temporary itemstore %s for writing: %s\n",
           temp_path, strerror(errno));
    goto cleanup;
  }

  if (!write_bytes(file, ITEMSTORE_V1_MAGIC, ITEMSTORE_V1_MAGIC_SIZE,
                   "file-header magic")
      || !write_u16_le(file, ITEMSTORE_V1_FORMAT_VERSION,
                       "file-header version")
      || !write_item(file, root)) {
    goto cleanup;
  }

  if (fflush(file) != 0) {
    logerr("Failed to flush temporary itemstore %s: %s\n", temp_path,
           strerror(errno));
    goto cleanup;
  }

  if (itemstore_durability_requires_sync(config.itemstore_durability)
      && !itemstore_sync_hook(file, temp_path)) {
    goto cleanup;
  }

  if (fclose(file) != 0) {
    file = NULL;
    logerr("Failed to close temporary itemstore %s: %s\n", temp_path,
           strerror(errno));
    goto cleanup;
  }
  file = NULL;

  if (rename(temp_path, filename) != 0) {
    logerr("Failed to replace itemstore %s with %s: %s\n", filename,
           temp_path, strerror(errno));
    goto cleanup;
  }

  success = true;

cleanup:
  if (file != NULL && fclose(file) != 0) {
    logerr("Failed to close temporary itemstore %s during cleanup: %s\n",
           temp_path, strerror(errno));
  }
  if (!success) {
    if (remove(temp_path) != 0 && errno != ENOENT) {
      logerr("Failed to remove temporary itemstore %s: %s\n", temp_path,
             strerror(errno));
    }
    logerr("Failed to save itemstore '%s'; existing data was not replaced.\n",
           filename);
  }
  free(temp_path);
  return success;
}

static void detach_loaded_item(ITEM_t *item) {
  ITEM_t *parent = item->parent;
  if (parent != NULL) {
    delete_hashtable(parent->children, item->name);
    for (size_t i = 0; i < parent->ordered_size; i++) {
      if (parent->ordered_array[i] == item) {
        for (size_t j = i + 1; j < parent->ordered_size; j++) {
          parent->ordered_array[j - 1] = parent->ordered_array[j];
        }
        parent->ordered_size--;
        break;
      }
    }
  }
  destroy_item(item);
}

static ITEMSTORE_READ_CTX_t itemstore_read_context(const char *filename,
                                                   size_t depth) {
  ITEMSTORE_READ_CTX_t ctx = {
    .depth = depth,
    .max_depth = ITEMSTORE_MAX_DEPTH,
    .max_children_per_item = ITEMSTORE_MAX_CHILDREN_PER_ITEM,
    .max_string_len = ITEMSTORE_MAX_STRING_LEN,
    .max_bytecode_len = ITEMSTORE_MAX_BYTECODE_LEN,
    .filename = filename
  };
  return ctx;
}

/* Payload ownership remains here until make_item() accepts the record. */
static void free_unowned_item_payload(ITEM_e type, VALUE_t *value,
                                      uint8_t *bytecode) {
  if (type == ITEM_value) value_free(value);
  else free(bytecode);
}

static ITEM_t *read_item_record(FILE *file, ITEM_t *parent,
                                ITEMSTORE_READ_CTX_t *ctx) {
  char name[33];
  uint8_t name_len;
  uint8_t item_tag;
  uint8_t value_tag;
  uint64_t raw_value;
  uint32_t numchildren;
  uint8_t *bytecode = NULL;
  uint32_t bytecode_len = 0;
  VALUE_t itemval = {VALUE_nil, {0}};

  if (ctx->depth > ctx->max_depth) {
    logerr("Corrupt itemstore '%s': item depth %zu exceeds maximum %zu.\n",
           ctx->filename, ctx->depth, ctx->max_depth);
    return NULL;
  }

  if (!read_u8(file, &name_len, "item name length")) return NULL;
  if (name_len > 32) {
    logerr("Corrupt itemstore '%s': item name length %u exceeds 32 bytes "
           "at depth %zu.\n", ctx->filename, name_len, ctx->depth);
    return NULL;
  }
  if (!read_bytes(file, name, name_len, "item name")) return NULL;
  if (memchr(name, '\0', name_len) != NULL) {
    logerr("Corrupt itemstore '%s': item name contains an embedded NUL at "
           "depth %zu.\n", ctx->filename, ctx->depth);
    return NULL;
  }
  name[name_len] = '\0';
  if (parent != NULL && (name_len == 0 || !is_valid_layer(name))) {
    logerr("Corrupt itemstore '%s': invalid item layer name '%s' at depth "
           "%zu.\n", ctx->filename, name, ctx->depth);
    return NULL;
  }
  if (parent != NULL && search_hashtable(parent->children, name) != NULL) {
    logerr("Corrupt itemstore '%s': duplicate child name '%s' at depth %zu.\n",
           ctx->filename, name, ctx->depth);
    return NULL;
  }

  if (!read_u8(file, &item_tag, "item type tag")) return NULL;
  ITEM_e type;
  switch (item_tag) {
    case ITEMSTORE_ITEM_TAG_VALUE: type = ITEM_value; break;
    case ITEMSTORE_ITEM_TAG_CODE: type = ITEM_code; break;
    default:
      logerr("Corrupt itemstore '%s': unsupported item type tag %u for '%s'.\n",
             ctx->filename, item_tag, name);
      return NULL;
  }

  if (type == ITEM_value) {
    if (!read_u8(file, &value_tag, "value type tag")) return NULL;
    switch (value_tag) {
      case ITEMSTORE_VALUE_TAG_NIL: itemval.type = VALUE_nil; break;
      case ITEMSTORE_VALUE_TAG_INT: itemval.type = VALUE_int; break;
      case ITEMSTORE_VALUE_TAG_FLOAT: itemval.type = VALUE_float; break;
      case ITEMSTORE_VALUE_TAG_STRING: itemval.type = VALUE_str; break;
      case ITEMSTORE_VALUE_TAG_BOOL: itemval.type = VALUE_bool; break;
      default:
        logerr("Corrupt itemstore '%s': unsupported value type tag %u for "
               "'%s'.\n", ctx->filename, value_tag, name);
        return NULL;
    }

    switch (itemval.type) {
      case VALUE_nil:
        break;
      case VALUE_int:
        if (!read_u64_le(file, &raw_value, "integer payload")) return NULL;
        memcpy(&itemval.i, &raw_value, sizeof(itemval.i));
        break;
      case VALUE_float:
        if (!read_u64_le(file, &itemval.f_bits, "float payload")) return NULL;
        break;
      case VALUE_str:
      {
        uint32_t length;
        if (!read_u32_le(file, &length, "string length")) return NULL;
        if (length > ctx->max_string_len) {
          logerr("Corrupt itemstore '%s': string length %u for '%s' exceeds "
                 "maximum %u.\n", ctx->filename, length, name,
                 ctx->max_string_len);
          return NULL;
        }
        itemval.s = malloc((size_t)length + 1);
        if (!itemval.s) {
          logerr("Failed to load itemstore '%s': cannot allocate %u bytes "
                 "for string item '%s'.\n", ctx->filename, length, name);
          return NULL;
        }
        if (!read_bytes(file, itemval.s, length, "string payload")) {
          goto fail_before_item;
        }
        itemval.s[length] = '\0';
        break;
      }
      case VALUE_bool:
      {
        uint8_t boolean;
        if (!read_u8(file, &boolean, "boolean payload")) return NULL;
        if (boolean > 1) {
          logerr("Corrupt itemstore '%s': invalid boolean payload %u for "
                 "'%s'.\n", ctx->filename, boolean, name);
          return NULL;
        }
        itemval.i = boolean;
        break;
      }
    }
  } else {
    if (!read_u32_le(file, &bytecode_len, "bytecode length")) return NULL;
    if (bytecode_len > ctx->max_bytecode_len) {
      logerr("Corrupt itemstore '%s': bytecode length %u for '%s' exceeds "
             "maximum %u.\n", ctx->filename, bytecode_len, name,
             ctx->max_bytecode_len);
      return NULL;
    }
    if (bytecode_len > 0) {
      bytecode = malloc(bytecode_len);
      if (!bytecode) {
        logerr("Failed to load itemstore '%s': cannot allocate %u bytes for "
               "bytecode item '%s'.\n", ctx->filename, bytecode_len, name);
        return NULL;
      }
      if (!read_bytes(file, bytecode, bytecode_len, "bytecode payload")) {
        goto fail_before_item;
      }
    }

    if (config.strict_validation) {
      BC_VerifyOptions verify_options = bc_verify_strict_options();
      BC_VerifyResult verify = bc_verify_bytecode(bytecode, bytecode_len,
                                                  name, &verify_options);
      if (verify.status != BC_VERIFY_OK) {
        logerr("Corrupt itemstore '%s': bytecode verification failed for "
               "'%s': %s\n", ctx->filename, name,
               verify.diagnostic.message);
        goto fail_before_item;
      }
    }
  }

  if (!read_u32_le(file, &numchildren, "child count")) {
    goto fail_before_item;
  }
  if (numchildren > ctx->max_children_per_item) {
    logerr("Corrupt itemstore '%s': child count %u for '%s' exceeds maximum "
           "%u.\n", ctx->filename, numchildren, name,
           ctx->max_children_per_item);
    goto fail_before_item;
  }

  ITEM_t *item = make_loaded_item(name, parent, type, itemval, bytecode,
                                  bytecode_len, numchildren);

  for (uint32_t i = 0; i < numchildren; i++) {
    ctx->depth++;
    ITEM_t *child = read_item_record(file, item, ctx);
    ctx->depth--;
    if (!child) {
      detach_loaded_item(item);
      return NULL;
    }
  }
  return item;

fail_before_item:
  free_unowned_item_payload(type, &itemval, bytecode);
  return NULL;
}

ITEM_t *read_item(FILE *file, ITEM_t *parent) {
  size_t depth = 0;
  for (ITEM_t *ancestor = parent; ancestor != NULL;
       ancestor = ancestor->parent) {
    depth++;
  }
  ITEMSTORE_READ_CTX_t ctx = itemstore_read_context("<stream>", depth);
  return read_item_record(file, parent, &ctx);
}

static bool read_itemstore_header(FILE *file, const char *filename) {
  uint8_t magic[ITEMSTORE_V1_MAGIC_SIZE];
  uint16_t version;

  if (!read_bytes(file, magic, sizeof(magic), "file-header magic")) {
    logerr("Corrupt itemstore '%s': truncated file header magic.\n", filename);
    return false;
  }
  if (memcmp(magic, ITEMSTORE_V1_MAGIC, sizeof(magic)) != 0) {
    logerr("Corrupt itemstore '%s': invalid itemstore magic.\n", filename);
    return false;
  }
  if (!read_u16_le(file, &version, "file-header version")) {
    logerr("Corrupt itemstore '%s': truncated file header version.\n",
           filename);
    return false;
  }
  if (version != ITEMSTORE_V1_FORMAT_VERSION) {
    logerr("Unsupported itemstore version in '%s': found %u, supported "
           "version is %u.\n", filename, version,
           ITEMSTORE_V1_FORMAT_VERSION);
    return false;
  }
  return true;
}

ITEM_t *load_itemstore(const char *filename) {
  FILE *file = fopen(filename, "rb");
  if (file == NULL) {
    logerr("Failed to open itemstore '%s' for reading: %s\n",
           filename, strerror(errno));
    return NULL;
  }

  ITEMSTORE_READ_CTX_t ctx = itemstore_read_context(filename, 0);
  ITEM_t *root = NULL;
  if (read_itemstore_header(file, filename)) {
    root = read_item_record(file, NULL, &ctx);
  }
  if (root != NULL) {
    int trailing = fgetc(file);
    if (trailing != EOF) {
      logerr("Corrupt itemstore '%s': trailing data after the root record.\n",
             filename);
      destroy_item(root);
      root = NULL;
    } else if (ferror(file)) {
      logerr("Failed to verify the end of itemstore '%s': %s\n",
             filename, strerror(errno));
      destroy_item(root);
      root = NULL;
    }
  }

  if (fclose(file) != 0) {
    logerr("Failed to close itemstore '%s' after reading: %s\n",
           filename, strerror(errno));
    if (root) destroy_item(root);
    return NULL;
  }
  if (!root) {
    logerr("Failed to load itemstore '%s': invalid or truncated data.\n",
           filename);
  }
  return root;
}

void dump_item(ITEM_t *item, char *item_name, bool isroot) {
  // Recursive function to construct and print the fully-qualified itemstore
  // from a given node. If passing the root of the itemstore,
  // item_name == NULL and isroot == true
  // Base case: if the item is NULL, return
  if (item == NULL) return;
  // Buffer to hold the full name of the current item
  char currentpath[265]; // 8 layers + 7 dots + 1 \0
  if (isroot) {
    // For the root item, we initialize the path as empty
    currentpath[0] = '\0';
  } else {
    // If a path is provided, use it
    // otherwise start with the current item's name
    if (item_name && item_name[0] != '\0') {
      snprintf(currentpath, sizeof(currentpath), "%s.%s", item_name,
                                                             item->name);
    } else {
      snprintf(currentpath, sizeof(currentpath), "%s", item->name);
    }
  }
  // Only print if this is not the root item
  if (!isroot) {
    if (item->value.type == VALUE_int) {
      logmsg("Item: %s, Value: %llu\n", currentpath,
                                     (unsigned long long)item->value.i);
    } else if (item->value.type == VALUE_str) {
      logmsg("Item: %s, Value: '%s'\n", currentpath, item->value.s);
    } else {
      logmsg("Item: %s, Value: (unknown)\n", currentpath);
    }
  }
  for (size_t i = 0; i < item->ordered_size; i++) {
    dump_item(item->ordered_array[i], currentpath, false);
  }
}

bool is_valid_layer(const char *str) {
  // Courtesy of ChatGPT.
  // Layer names may be no longer than 32 characters, and may also
  // consist of characters in the set: A-Za-Z0-9_

  // Early exit if too big
  if (strlen(str) > 32) {
    return false;
  }

  // Character validation
  for (const char *p = str; *p; ++p) {
    if (!((*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z') ||
          (*p >= '0' && *p <= '9') || *p == '_')) {
      return false;
    }
  }

  return true;
}

static const char *safe_error_message(const int errnum) {
  if (errnum >= 0 && errnum < MAXERRORS && errmsg[errnum]) {
    return errmsg[errnum];
  }
  return "Unknown error";
}

static void set_string_error_field(const char *name, const char *value) {
  VALUE_t v;
  v.type = VALUE_str;
  v.s = strdup(value ? value : "");
  set_item(config.itemroot, name, v);
}

static void set_int_error_field(const char *name, int64_t value) {
  VALUE_t v;
  v.type = VALUE_int;
  v.i = value;
  set_item(config.itemroot, name, v);
}

void clear_error_item(void) {
  set_item(config.itemroot, "error", VALUE_NIL);
  set_item(config.itemroot, "error.msg", VALUE_NIL);
  set_item(config.itemroot, "error.code", VALUE_NIL);
  set_item(config.itemroot, "error.stage", VALUE_NIL);
  set_item(config.itemroot, "error.file", VALUE_NIL);
  set_item(config.itemroot, "error.line", VALUE_NIL);
  set_item(config.itemroot, "error.column", VALUE_NIL);
  set_item(config.itemroot, "error.excerpt", VALUE_NIL);
}

void set_error_item(const int errnum, const char *errdetail) {
  // Helper function to set the error item.
  VALUE_t e, emsg;
  const char *base = safe_error_message(errnum);
  e.type = VALUE_int;
  e.i = errnum;
  set_item(config.itemroot, "error", e);
  emsg.type = VALUE_str;
  if (errdetail) {
    // It's possible that there is an extended error message.
    // Allocate enough space for the two error messages, plus
    // the extra characters "errmsg (errdetail)"
    int elen = strlen(base) + strlen(errdetail) + 4;
    emsg.s = malloc((size_t)elen);
    snprintf(emsg.s, elen, "%s (%s)", base, errdetail);
  } else {
    emsg.s = strdup(base);
  }
  set_item(config.itemroot, "error.msg", emsg);
  set_item(config.itemroot, "error.code", VALUE_NIL);
  set_item(config.itemroot, "error.stage", VALUE_NIL);
  set_item(config.itemroot, "error.file", VALUE_NIL);
  set_item(config.itemroot, "error.line", VALUE_NIL);
  set_item(config.itemroot, "error.column", VALUE_NIL);
  set_item(config.itemroot, "error.excerpt", VALUE_NIL);
}

void set_compiler_error_item(const CompilerDiagnostic *diag) {
  if (!diag) {
    set_error_item(ERR_COMP_UNKNOWN, NULL);
    return;
  }

  const char *stable_code = diag->stable_code
      ? diag->stable_code
      : compiler_diag_stable_code(diag->code, diag->phase);
  const char *stage = compiler_diag_phase_name(diag->phase);
  const char *file = diag->source_name ? diag->source_name : "";
  const char *message = diag->message ? diag->message : "";
  const char *excerpt = diag->excerpt ? diag->excerpt : "";
  int line = diag->has_loc ? diag->line : 0;
  int column = diag->has_loc ? diag->column : 0;

  VALUE_t e;
  e.type = VALUE_int;
  e.i = diag->code;
  set_item(config.itemroot, "error", e);

  int needed = snprintf(NULL, 0,
      "%s stage=%s file=%s line=%d column=%d message=%s excerpt=%s",
      stable_code, stage, file, line, column, message, excerpt);
  if (needed < 0) {
    set_error_item(diag->code, message);
    return;
  }

  VALUE_t emsg;
  emsg.type = VALUE_str;
  emsg.s = malloc((size_t)needed + 1);
  snprintf(emsg.s, (size_t)needed + 1,
      "%s stage=%s file=%s line=%d column=%d message=%s excerpt=%s",
      stable_code, stage, file, line, column, message, excerpt);
  set_item(config.itemroot, "error.msg", emsg);

  set_string_error_field("error.code", stable_code);
  set_string_error_field("error.stage", stage);
  set_string_error_field("error.file", file);
  set_int_error_field("error.line", line);
  set_int_error_field("error.column", column);
  set_string_error_field("error.excerpt", excerpt);
}
