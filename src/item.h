// The item is the basic unit of storage.  It may contain a value or code.
// It may also contain nested items.  All items are evaluated.  A value item
// pushes a value onto the stack.  A code item is executed, and the value of
// the executed item is pushed onto the stack.
//
// Each code item is an isolated unit, with its own stack, etc.

#pragma once

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

#include "slab.h"
#include "value.h"
#include "stack.h"

typedef struct Item ITEM_t;
typedef struct Entry ENTRY_t;
typedef struct HashTable HASHTABLE_t;

// Define the hash table entry
struct Entry {
  char *key;
  ITEM_t *child;
  ENTRY_t *next;
};

// This hashtable contains pointers to all the children of this Item.
struct HashTable {
  int size;
  ENTRY_t **table; // An array of pointers to ENTRY_t
};

typedef enum {ITEM_value, ITEM_code} ITEM_e;
struct Item {
  ITEM_e type;           // 4 bytes
  int bytecode_len;      // 4 bytes
  char name[33];         // 33 bytes (32 characters + null terminator)
  uint8_t pad[7];        // 7 bytes of padding for 8-byte alignment
  ITEM_t *parent;        // 8 bytes - Pointer to the parent item
  HASHTABLE_t *children; // 8 bytes - Hash table for immediate children
  uint8_t *bytecode;     // 8 bytes - Bytecode if a code item
  STACK_t *stack;        // 8 bytes - Stack for when this item runs
  VALUE_t value;         // 16 bytes - At present
};

// These functions are not intended to be called externally.
HASHTABLE_t *create_hashtable(int size);
uint32_t simple_hash(const char *key, size_t len);
HASHTABLE_t *resize_hashtable(HASHTABLE_t *oldhashtable, int newsize);
float calculate_load_factor(HASHTABLE_t *hashTable);
HASHTABLE_t *maybe_resize_hashtable(HASHTABLE_t *hashtable);
void insert_hashtable(HASHTABLE_t *hashtable, const char *key, ITEM_t *child);
ITEM_t *search_hashtable(HASHTABLE_t *hashtable, const char *key);
void delete_hashtable(HASHTABLE_t *hashtable, const char *key);
void free_hashtable(HASHTABLE_t *hashtable);
uint32_t murmur3_32(const char *key, size_t len, uint32_t seed);
char *substr(const char *str, size_t begin, size_t len);
void write_item(FILE *file, ITEM_t *item);
ITEM_t *read_item(FILE *file, ITEM_t *parent);

// Allocator API
// Defined in the itemstore as it defines allocators for specific
// object types.  Probably needs to be defined elsewhere, though.
void init_allocator(Allocator *allocator);
void destroy_allocator(Allocator *allocator);
ENTRY_t *allocate_entry(Allocator *allocator);
HASHTABLE_t *allocate_hashtable(Allocator *allocator);
ITEM_t *allocate_item(Allocator *allocator);
void deallocate_entry(Allocator *allocator, ENTRY_t *entry);
void deallocate_hashtable(Allocator *allocator, HASHTABLE_t *hashtable);
void deallocate_item(Allocator *allocator, ITEM_t *item);

// Itemstore API
ITEM_t *make_root_item(const char *name);
ITEM_t *make_item(const char *name, ITEM_t *parent, ITEM_e type,
                                VALUE_t value, uint8_t *bytecode, int len);
void destroy_item(ITEM_t *item);    
ITEM_t *insert_item(ITEM_t *root, const char *item_name, VALUE_t value);
ITEM_t *find_item(ITEM_t *root, const char *item_name);
void delete_item(ITEM_t *root, const char *item_name);
void set_item(ITEM_t *root, const char *item_name, VALUE_t value);
void save_itemstore(const char *filename, ITEM_t *root); 
ITEM_t *load_itemstore(const char *filename); 
void dump_item(ITEM_t *item, char *item_name, bool isroot);

// Other item-related API functions
bool is_valid_layer(const char *str);
