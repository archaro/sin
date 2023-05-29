// A simple string wrapper for the interning of strings.
//
// All strings are hashed and held in a global table. The table keeps
// track of whether the strings were found by the compiler, or if they
// are runtime strings - this is so that runtime strings can be
// periodically cleaned up to reduce the load factor.

#include <string.h>
#include <stdbool.h>

#include "stringtable.h"
#include "memory.h"
#include "log.h"
#include "murmur3.h"

// This is the seed for the hash function.  It must be unique across
// all programs which share the same string table.
#define MURMUR_SEED 1001

// If the string table load gets above this, the table needs to grow.
#define TABLE_MAX_LOAD  0.75

STRTABLE_t *make_stringtable(STRTABLE_t *table, uint16_t capacity) {
  // Create the global string hash table.
  table = GROW_ARRAY(STRTABLE_t, NULL, 0, sizeof(STRTABLE_t));
  table->count = 0;
  table->capacity = capacity;
  table->hash = GROW_ARRAY(STRING_t *, NULL, 0,
                                          sizeof(STRING_t *) * capacity);
  // An empty bucket is represented by NULL
  for (int i = 0; i < capacity; i++) {
    table->hash[i] = NULL;
  }
  return table;
}

unsigned __int128 hash_string(char *str, STR_e from, STRTABLE_t *table) {
  // Hash a string and insert it into the string table if not there already.
  // Grow the string table if the load factor is exceeded.
  // NOTE: once a string is added to the string table, the table
  // takes ownership - the calling function MUST NOT free the string!
  // Return the hash.

  uint16_t len = strlen(str);
  unsigned __int128 hash;
  MurmurHash3_x86_128(str, len, MURMUR_SEED, &hash);
  uint16_t index = hash % table->capacity;

  // Check to see if this hash is in the table aready
  bool found = false;
  STRING_t *next = table->hash[index];
  while (next) {
    if (next->hash == hash) {
      found = true;
      break;
    }
    next = next->next;
  }
  if (!found) {
    // Not found, so insert a new one.
    STRING_t *newstr = GROW_ARRAY(STRING_t, NULL, 0, sizeof(STRING_t));
    newstr->ptr = str;
    newstr->len = strlen(str);
    newstr->from = from;
    newstr->hash = hash;
    newstr->next = NULL;
    if (!table->hash[index]) {
      // First string in this bucket
      table->hash[index] = newstr;
    } else {
      STRING_t *lastentry = table->hash[index];
      while (lastentry->next) {
        // Find the end of the chain
        lastentry = lastentry->next;
      }
      lastentry->next = newstr;
    }
    table->count++;
    if (table->count > table->capacity * TABLE_MAX_LOAD) {
      // The hash table is getting a bit too big.  Increase the size
      // and re-balance it.
      grow_string_table(table);
    }
  } else {
    // This one already exists, so free the string that was passed in
    // and return the hash of the pre-existing string.
    FREE_ARRAY(char, str, strlen(str)+1);
  }
  // If we get to here, the string definitely exists in the table
  // - if it didn't, it was inserted.  Just return the hash value.
  return hash;
}

void grow_string_table(STRTABLE_t *table) {
  // Increase the size of the string table and reallocate the hashed
  // strings to new buckets.  This is a SLOW process, so should be done
  // infrequently!

  uint16_t newcapacity =  GROW_CAPACITY(table->capacity);
  if (newcapacity < table->capacity) {
    // We have overflowed the string table.  This is a problem.
    // (Unlikely, since we have over 2 billion entries, but still...)
    logerr("Unable to grow string interning table - overflow.\n");
    return;
  }
  STRING_t **newhash = GROW_ARRAY(STRING_t *, NULL, 0,
                                        sizeof(STRING_t *) * newcapacity);
  // An empty bucket is represented by NULL
  for (int i = 0; i < newcapacity; i++) {
    newhash[i] = NULL;
  }
  // Iterate over every string in the old table, recalculate its bucket
  // and insert it into the new table.
  for (uint16_t index = 0; index < table->capacity; index++) {
    if (!table->hash[index]) {
      // No hashes here...
      continue;
    }
    // We have at least one string in this bucket.  Iterate over the
    // chain and move each entry to the new table.
    STRING_t *moveme = table->hash[index];
    do {
      uint16_t newindex = moveme->hash % newcapacity;
      if (!newhash[newindex]) {
        newhash[newindex] = moveme;
        moveme = moveme->next;
        newhash[newindex]->next = NULL;
      } else {
        STRING_t *end = newhash[newindex];
        while (end->next) {
          end = end->next;
        }
        end->next = moveme;
        moveme = moveme->next;
        end->next->next = NULL;
      }
    } while (moveme);
  }
  // Clean up and update the STRTABLE_t record.
  FREE_ARRAY(STRING_t *, table->hash, table->capacity);
  table->capacity = newcapacity;
  table->hash = newhash;
}

void destroy_stringtable(STRTABLE_t *table) {
  // Iterate through each STRING_t*, freeing the associated char* and
  // then the STRING_t* itself.  Finally free the STRTABLE_t*
  for (uint16_t i = 0; i < table->capacity; i++) {
    if (table->hash[i]) {
      STRING_t *next = table->hash[i];
      while (next) {
        FREE_ARRAY(char, next->ptr, next->len+1);
        STRING_t *next2 = next->next;
        FREE_ARRAY(STRING_t, next, sizeof(STRING_t));
        next = next2;
      }
    }
  }
  FREE_ARRAY(STRTABLE_t *, table->hash, 
                                  sizeof(STRTABLE_t *) * table->capacity);
  FREE_ARRAY(STRTABLE_t, table, sizeof(STRTABLE_t));
}
