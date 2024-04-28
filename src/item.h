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

#include "value.h"

// Items are up to 8 layers deep, and each layer name is a maximum of
// 32 characters.  There is a dot separating each layer name (7 in total)
// and a terminating null.  So the maximum size is (32 * 8) + 7 + 1.
#define MAX_ITEM_NAME 264

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
  uint32_t size;
  ENTRY_t **table; // An array of pointers to ENTRY_t
};

typedef enum {ITEM_value, ITEM_code} ITEM_e;
struct Item {
  ITEM_e type;           // 4 bytes
  uint32_t bytecode_len; // 4 bytes
  char name[33];         // 33 bytes (32 characters + null terminator)
  bool inuse;            // Set when an item is being executed.
  uint8_t pad[7];        // 6 bytes of padding for 8-byte alignment
  ITEM_t *parent;        // 8 bytes - Pointer to the parent item
  HASHTABLE_t *children; // 8 bytes - Hash table for immediate children
  uint8_t *bytecode;     // 8 bytes - Bytecode if a code item
  VALUE_t value;         // 16 bytes - (at present)
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
ENTRY_t *allocate_entry();
HASHTABLE_t *allocate_hashtable();
ITEM_t *allocate_item();
void deallocate_entry(ENTRY_t *entry);
void deallocate_hashtable(HASHTABLE_t *hashtable);
void deallocate_item(ITEM_t *item);

// Itemstore API
ITEM_t *make_root_item(const char *name);
ITEM_t *make_item(const char *name, ITEM_t *parent, ITEM_e type,
                                VALUE_t value, uint8_t *bytecode, int len);
void destroy_item(ITEM_t *item);    
ITEM_t *insert_item(ITEM_t *root, const char *item_name, VALUE_t value);
ITEM_t *insert_code_item(ITEM_t *root, const char *item_name, uint32_t len,
                                                        uint8_t *bytecode);
ITEM_t *find_item(ITEM_t *root, const char *item_name);
void delete_item(ITEM_t *root, const char *item_name);
void set_item(ITEM_t *root, const char *item_name, VALUE_t value);
void get_itemname(ITEM_t *item, char *itemname);
char *get_itemfilename(ITEM_t *item);
bool save_itemsource(ITEM_t *item, char *source);
void save_itemstore(const char *filename, ITEM_t *root); 
ITEM_t *load_itemstore(const char *filename); 
void dump_item(ITEM_t *item, char *item_name, bool isroot);

// Other item-related API functions
bool is_valid_layer(const char *str);
void set_error_item(const int errnum);
