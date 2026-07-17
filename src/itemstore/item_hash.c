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
#include "item_internal.h"

HASHTABLE_t *create_hashtable(int size) {
  // Create a hashtable with the given number of buckets
  if (size <= 0) size = 1;
  HASHTABLE_t *hashtable = allocate_hashtable();
  if (!hashtable) return NULL;
  hashtable->size = (uint32_t)size;
  hashtable->entry_count = 0;
  hashtable->table = calloc((size_t)size, sizeof *hashtable->table);
  if (!hashtable->table) {
    deallocate_hashtable(hashtable);
    return NULL;
  }
  return hashtable;
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
  if (!newhashtable) return oldhashtable;
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

bool resize_ordered_array(ITEM_t *item) {
  if (item->ordered_size < item->ordered_capacity) return true;

  size_t required = item->ordered_size + 1;
  size_t new_capacity = item->ordered_capacity;
  if (required < ITEM_ARRAY_INIT_CAPACITY) required = ITEM_ARRAY_INIT_CAPACITY;
  if (!alloc_grow_array_capacity((void **)&item->ordered_array, &new_capacity,
                                 required, sizeof *item->ordered_array)) {
    logerr("Cannot grow ordered item array beyond %zu entries.\n",
           item->ordered_capacity);
    return false;
  }
  item->ordered_capacity = new_capacity;
  return true;
}

float calculate_load_factor(HASHTABLE_t *hashtable) {
  return (float)hashtable->entry_count / (float)hashtable->size;
}

HASHTABLE_t *maybe_resize_hashtable(HASHTABLE_t *hashtable) {
  float loadfactor = calculate_load_factor(hashtable);
  const float maxloadfactor = 0.75; // Tweak for performance as needed
  if (loadfactor > maxloadfactor) {
    // Double the size - maybe tweak for performance
    uint32_t grown_size = (hashtable->size * 2u) + 1u;
    int newsize = grown_size > (uint32_t)INT_MAX ? INT_MAX : (int)grown_size;
    return resize_hashtable(hashtable, newsize);
  }
  // hashtable has not changed.
  return hashtable;
}

bool insert_hashtable(HASHTABLE_t *hashtable, const char *key, ITEM_t *child) {
  if (!hashtable || !hashtable->table || !key) return false;
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
  if (!newEntry) return false;
  newEntry->key = strdup(key);
  if (!newEntry->key) {
    deallocate_entry(newEntry);
    return false;
  }
  newEntry->child = child;
  newEntry->next = hashtable->table[hashindex];
  hashtable->table[hashindex] = newEntry;
  hashtable->entry_count++;
  return true;
}

ITEM_t *search_hashtable(HASHTABLE_t *hashtable, const char *key) {
  if (!hashtable || !hashtable->table || !key) return NULL;
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
  if (!hashtable || !hashtable->table || !key) return;
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
  if (!hashtable) return;
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
  const size_t nblocks = len / 4;
  const uint32_t* blocks = (const uint32_t*)key;
  size_t i;
  for (i = 0; i < nblocks; i++) {
    uint32_t k = blocks[i];
    k *= c1;
    k = (k << r1) | (k >> (32 - r1));
    k *= c2;
    hash ^= k;
    hash = ((hash << r2) | (hash >> (32 - r2))) * m + n;
  }
  const uint8_t *tail = (const uint8_t*)(key + nblocks * 4u);
  uint32_t k1 = 0;
  switch (len & 3) {
    case 3:
      k1 ^= ((uint32_t)tail[2]) << 16;
      __attribute__((fallthrough));
    case 2:
      k1 ^= ((uint32_t)tail[1]) << 8;
      __attribute__((fallthrough));
    case 1:
      k1 ^= tail[0];
      k1 *= c1;
      k1 = (k1 << r1) | (k1 >> (32 - r1));
      k1 *= c2;
      hash ^= k1;
  }
  hash ^= (uint32_t)len;
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

ENTRY_t *allocate_entry(void) {
  // Allocator API: Gimme a new ENTRY_t
  return malloc(sizeof(ENTRY_t));
}

HASHTABLE_t *allocate_hashtable(void) {
  // Allocator API: Gimme a new HASHTABLE_t
  return malloc(sizeof(HASHTABLE_t));
}

ITEM_t *allocate_item(void) {
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
