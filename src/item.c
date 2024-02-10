// The Item.  The nub and the gist of the whole brouhaha in a nutshell.

#include <stdlib.h>
#include <string.h>

#include "memory.h"
#include "log.h"
#include "item.h"

// The slab allocator
// This is used to handle memory allocation for all uniform objects.
// It is defined in sin.c
extern Allocator allocator;

HASHTABLE_t *create_hashtable(int size) {
  // Create a hashtable with the given number of buckets
  HASHTABLE_t *hashtable = allocate_hashtable(&allocator);
  hashtable->size = size;
  hashtable->table = (ENTRY_t**)malloc(sizeof(ENTRY_t*) * size);
  for (int i = 0; i < size; i++) {
    hashtable->table[i] = NULL;
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
  // Rehash all the existing entries
  for (int i = 0; i < (oldhashtable)->size; i++) {
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
      // Remove it from the old list
      current_entry->next = NULL;
      // Add it to the new list
      // No need to allocate new memory for the entry itself
      if (newhashtable->table[newhashindex] == NULL) {
        newhashtable->table[newhashindex] = current_entry;
      } else {
        // Handle collision with separate chaining
        ENTRY_t *temp = newhashtable->table[newhashindex];
        while (temp->next != NULL) {
          temp = temp->next;
        }
        temp->next = current_entry;
      }
      // Move to the next entry
      current_entry = nextEntry;
    }
  }
  // Free the old table's array of pointers
  // - but not the entries themselves as we reused them
  free((oldhashtable)->table);
  // Free the old hash table struct
  deallocate_hashtable(&allocator, oldhashtable);
  return newhashtable;
}

float calculate_load_factor(HASHTABLE_t *hashtable) {
  int numentries = 0;
  for (int i = 0; i < hashtable->size; i++) {
    ENTRY_t *entry = hashtable->table[i];
    while (entry) {
      numentries++;
      entry = entry->next;
    }
  }
  return (float)numentries / (float)hashtable->size;
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
  ENTRY_t *newEntry = allocate_entry(&allocator);
  newEntry->key = strdup(key);
  newEntry->child = child;
  newEntry->next = NULL;

  // Insert into the hash table
  ENTRY_t* current = hashtable->table[hashindex];
  if (current == NULL) {
    hashtable->table[hashindex] = newEntry;
  }
  else {
    // Handle collision with separate chaining
    while (current->next != NULL) {
      current = current->next;
    }
    current->next = newEntry;
  }
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
      deallocate_entry(&allocator, current);
      return;
    }
    previous = current;
    current = current->next;
  }
}

void free_hashtable(HASHTABLE_t* hashtable) {
  for (int i = 0; i < hashtable->size; i++) {
    ENTRY_t *current = hashtable->table[i];
    while (current) {
      ENTRY_t *temp = current;
      current = current->next;
      destroy_item(temp->child);
      free(temp->key);
      deallocate_entry(&allocator, temp);
    }
  }
  free(hashtable->table);
  deallocate_hashtable(&allocator, hashtable);
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
    case 2:
      k1 ^= tail[1] << 8;
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

void init_allocator(Allocator *allocator) {
  // Allocator API: Create the slab allocator with a separate pool for each
  // block size: ITEM_t, HASHTABLE_t and ENTRY_t
  init_slab(&allocator->entry_slab, sizeof(ENTRY_t));
  init_slab(&allocator->hashtable_slab, sizeof(HASHTABLE_t));
  init_slab(&allocator->item_slab, sizeof(ITEM_t));
}

void destroy_allocator(Allocator *allocator) {
  // Allocator API: Finalise the slab allocator, releasing all memory.
  destroy_slab(&allocator->entry_slab);
  destroy_slab(&allocator->hashtable_slab);
  destroy_slab(&allocator->item_slab);
}

ENTRY_t *allocate_entry(Allocator *allocator) {
  // Allocator API: Gimme a new ENTRY_t
  return (ENTRY_t*)allocate_from_slab(&allocator->entry_slab);
}

HASHTABLE_t *allocate_hashtable(Allocator *allocator) {
  // Allocator API: Gimme a new HASHTABLE_t
  return (HASHTABLE_t*)allocate_from_slab(&allocator->hashtable_slab);
}

ITEM_t *allocate_item(Allocator *allocator) {
  // Allocator API: Gimme a new Item
  return (ITEM_t*)allocate_from_slab(&allocator->item_slab);
}

void deallocate_entry(Allocator *allocator, ENTRY_t *entry) {
  // Allocator API: Take this ENTRY_t back.
  deallocate_to_slab(&allocator->entry_slab, entry);
}

void deallocate_hashtable(Allocator *allocator, HASHTABLE_t *hashtable) {
  // Allocator API: Take this HashTable back.
  deallocate_to_slab(&allocator->hashtable_slab, hashtable);
}

void deallocate_item(Allocator *allocator, ITEM_t *item) {
  // Allocator API: Take this Item back.
  deallocate_to_slab(&allocator->item_slab, item);
}

ITEM_t *make_item(const char *name, ITEM_t *parent, ITEM_e type,
                                VALUE_t value, uint8_t *bytecode, int len) {
  // Note that for performance reasons this function does not check
  // to see if the item already exists at this layer.  You MUST
  // check that before you call this function!
  ITEM_t *item = allocate_item(&allocator);
  item->parent = parent;
  item->type = type;
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
  item->children = create_hashtable(16); // Size is chosen arbitrarily
  // Now add the newly-created item to its parent's hashtable
  insert_hashtable(parent->children, name, item);
  // Maybe resize the hashtable?
  parent->children = maybe_resize_hashtable(parent->children);
  return item;
}

ITEM_t *make_root_item(const char* name) {
  // This is exactly the same as make_item, except that it doesn't try to
  // insert the item into its parent's hashtable.  This is a separate
  // function for performance reasons - it is only ever used ONCE, so there
  // is no point having an additional if statement that always evaluates
  // one way.
  ITEM_t *item = allocate_item(&allocator);
  item->parent = NULL;
  item->type = ITEM_value;
  item->value.type = VALUE_int;
  item->value.i = 0; // Root item is never reference, so this doesn't matter
  strncpy(item->name, name, strlen(name)+1);
  item->children = create_hashtable(16); // Size is chosen arbitrarily
  return item;
}

void destroy_item(ITEM_t *item) {
  if (item->type == ITEM_code) {
    free(item->bytecode);
  } else if (item->type == ITEM_value && item->value.type == VALUE_str) {
    free(item->value.s);
  }
  // Free the item's hashtable
  free_hashtable(item->children);
  // Then free the item
  deallocate_item(&allocator, item);
}

ITEM_t *insert_item(ITEM_t *root, const char *item_name, VALUE_t value) {
  // Function to insert a new item into the tree at the specified node.
  // If layers of the item don't exist, they are created with a default
  // value of 0.
  ITEM_t *current_item = root;
  const char *current_pos = item_name;
  // Buffer to hold each layer of the item, with space for null terminator
  char layer[33];
  DEBUG_LOG("Creating new item %s\n", item_name);
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
      // Set the value
      if (current_item->value.type == VALUE_str) {
        FREE_ARRAY(char, current_item->value.s,
                                           strlen(current_item->value.s+1));
      }
      current_item->value = value;
      break;
    }
    // Otherwise, move past the dot to the beginning of the next layer
    current_pos = next_dot + 1;
  }
  // Return a pointer to the last-created item
  return current_item;
}

ITEM_t *insert_code_item(ITEM_t *root, const char *item_name, uint32_t len,
                                                      uint8_t *bytecode) {
  // This function is basically the same as insert_item() but creates a
  // code item instead of a value item.
  ITEM_t *current_item = root;
  const char *current_pos = item_name;
  // Buffer to hold each layer of the item, with space for null terminator
  char layer[33];
  DEBUG_LOG("Creating new item %s\n", item_name);
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
      if (current_item->type == ITEM_value
                              && current_item->value.type == VALUE_str) {
        FREE_ARRAY(char, current_item->value.s,
                                           strlen(current_item->value.s+1));
      }
      current_item->type = ITEM_code;
      current_item->value.type = VALUE_nil; // Just to be safe
      if (current_item->bytecode_len > 0) {
        FREE_ARRAY(unsigned char, current_item->bytecode,
                                           current_item->bytecode_len);
      }
      current_item->bytecode_len = len;
      current_item->bytecode = bytecode;
      break;
    }
    // Otherwise, move past the dot to the beginning of the next layer
    current_pos = next_dot + 1;
  }
  // Return a pointer to the last-created item
  return current_item;
}

ITEM_t *find_item(ITEM_t *root, const char *item_name) {
  // Function to dereference an item by a multi-layer item.
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

void delete_item(ITEM_t *root, const char *item_name) {
  // Find an item and then delete it and all of its children.
  ITEM_t *item = find_item(root, item_name);
  if (item) {
    // We don't care about items that don't exist, just silently ignore the
    // delete request.  It's not there anyway, so why the complaining?
    // First, remove the item from its parent's hashtable:
    delete_hashtable(item->parent->children, item->name);
    // Now we have isolated this item, delete it and all its children.
    destroy_item(item);
    DEBUG_LOG("Item %s has been deleted, along with all of its children.\n",
                                                                 item_name);
  }
}

void set_item(ITEM_t *root, const char *item_name, VALUE_t value) {
  // Find an item, and set its value.
  // If the item does not exist, it will be created, and then set.
  ITEM_t *item = find_item(root, item_name);
  if (item) {
    // Item exists, so just update its value.
    if (item->value.type == VALUE_str) {
      free(item->value.s);
    }
    item->value = value;
  } else {
    // Item doesn't exist, so create it.
    insert_item(root, item_name, value);
  }
}

void write_item(FILE *file, ITEM_t *item) {
  // Write the item name as a fixed size of 32 bytes
  char name[33]; // 32 characters + 1 for null-terminator
  strncpy(name, item->name, 32);
  name[32] = '\0'; // Ensure null-termination
  fwrite(name, sizeof(char), 33, file); // Write the fixed size name
  // Write the type of the item - there are several types but on disk they
  // degrade to either an int or a string.  When reading the item back
  // from disk, the item type determines how exactly it is created.
  fwrite(&(item->type), sizeof(item->type), 1, file);
  if (item->type == ITEM_value) {
    fwrite(&(item->value.type), sizeof(item->value.type), 1, file);
    if (item->value.type == VALUE_str) {
      int l = strlen(item->value.s);
      fwrite(&(l), sizeof(l), 1, file);
      fwrite(item->value.s, sizeof(char), l, file);
    } else {
      fwrite(&(item->value.i), sizeof(item->value.i), 1, file);
    }
  } else if (item->type == ITEM_code) {
    fwrite(&(item->bytecode_len), sizeof(item->bytecode_len), 1, file);
    fwrite(item->bytecode, sizeof(item->bytecode_len), item->bytecode_len, file);
  }
  // Write the number of children
  uint32_t numchildren = 0;
  for (int i = 0; i < item->children->size; i++) {
    ENTRY_t *entry = item->children->table[i];
    while (entry) {
      numchildren++;
      entry = entry->next;
    }
  }
  fwrite(&numchildren, sizeof(numchildren), 1, file);
  // Write each child
  for (int i = 0; i < item->children->size; i++) {
    ENTRY_t *entry = item->children->table[i];
    while (entry) {
      // Write the child item
      write_item(file, entry->child);
      entry = entry->next;
    }
  }
}

void save_itemstore(const char *filename, ITEM_t *root) {
  FILE* file = fopen(filename, "wb");
  if (file == NULL) {
    logerr("Failed to open itemstore for writing");
    return;
  } else {
    write_item(file, root);
    fclose(file);
  }
}

ITEM_t *read_item(FILE *file, ITEM_t *parent) {
  char name[33];
  fread(name, sizeof(char), 33, file);
  name[32] = '\0'; // Ensure null-termination
  ITEM_e type;
  fread(&type, sizeof(ITEM_e), 1, file);
  int64_t value;
  char *strvalue;
  uint8_t *bytecode;
  uint32_t bytecode_len;
  VALUE_e valtype;
  VALUE_t itemval;
  if (type == ITEM_value) {
    fread(&valtype, sizeof(valtype), 1, file);
    itemval.type = valtype;
    switch (valtype) {
      // These types are all represented as an int - only the type differs
      case VALUE_nil:
      case VALUE_int:
      case VALUE_bool:
      {
        fread(&value, sizeof(value), 1, file);
        itemval.i = value;
        break;
      }
      case VALUE_str:
      {
        int l;
        fread(&l, sizeof(l), 1, file); // length of string
        strvalue = (char*)malloc(l+1); // +1 for terminator
        fread(strvalue, sizeof(char), l, file);
        strvalue[l] = '\0';
        itemval.s = strvalue;
        break;
      }
    }
  } else if(type == ITEM_code) {
    fread(&bytecode_len, sizeof(bytecode_len), 1, file);
    bytecode = (uint8_t*)malloc(bytecode_len);
    fread(bytecode, sizeof(uint8_t), bytecode_len, file);
  }
  uint32_t numchildren;
  fread(&numchildren, sizeof(numchildren), 1, file);
    // Create the item with its value
  ITEM_t *item = (parent == NULL) ? make_root_item(name)
         : make_item(name, parent, type, itemval, bytecode, bytecode_len);
  // Read children if they exist
  for (uint32_t i = 0; i < numchildren; i++) {
    read_item(file, item);
  }
  return item;
}

ITEM_t *load_itemstore(const char *filename) {
  FILE *file = fopen(filename, "rb");
  if (file == NULL) {
    logerr("Failed to open itemstore for reading");
    return NULL;
  } else {
    ITEM_t *root = read_item(file, NULL); // Build the itemstore from root.
    fclose(file);
    return root;
  }
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
  // If the item has children, iterate over the hash table and calli
  // dumpItem on each
  if (item->children != NULL) {
    for (int i = 0; i < item->children->size; ++i) {
      ENTRY_t *entry = item->children->table[i];
      while (entry != NULL) {
        // Pass isroot as false because we are past the root now
        dump_item(entry->child, currentpath, false);
        entry = entry->next;
      }
    }
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

