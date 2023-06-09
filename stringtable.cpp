// A simple string wrapper for the interning of strings.
//
// All strings are hashed and held in a global table. The table keeps
// track of whether the strings were found by the compiler, or if they
// are runtime strings - this is so that runtime strings can be
// periodically cleaned up to reduce the load factor.

#include <cstring>
#include <cstdbool>

#include "stringtable.h"
#include "memory.h"
#include "log.h"
#include "murmur3.h"

// This is the seed for the hash function.  It must be unique across
// all programs which share the same string table.
#define MURMUR_SEED 1001

// If the string table load gets above this, the table needs to grow.
#define TABLE_MAX_LOAD  0.75

StrTable::StrTable(uint16_t capacity) {
  // Initialise the global string hash table.
  count = 0;
  capacity = capacity;
  hash = GROW_ARRAY(String *, NULL, 0, sizeof(String *) * capacity);
  // An empty bucket is represented by NULL
  for (int i = 0; i < capacity; i++) {
    hash[i] = NULL;
  }
}

unsigned __int128 StrTable::hash_string(char *str, STR_e from) {
  // Hash a string and insert it into the string table if not there already.
  // Grow the string table if the load factor is exceeded.
  // NOTE: once a string is added to the string table, the table
  // takes ownership - the calling function MUST NOT free the string!
  // Return the hash.

  uint16_t len = strlen(str);
  unsigned __int128 hash;
  MurmurHash3_x86_128(str, len, MURMUR_SEED, &hash);
  uint16_t index = hash % this->capacity;

  // Check to see if this hash is in the table aready
  bool found = false;
  String *next = this->hash[index];
  while (next) {
    if (next->hash == hash) {
      found = true;
      break;
    }
    next = next->next;
  }
  if (!found) {
    // Not found, so insert a new one.
    String *newstr = GROW_ARRAY(String, NULL, 0, sizeof(String));
    newstr->ptr = str;
    newstr->len = strlen(str);
    newstr->from = from;
    newstr->hash = hash;
    newstr->next = NULL;
    if (!this->hash[index]) {
      // First string in this bucket
      this->hash[index] = newstr;
    } else {
      String *lastentry = this->hash[index];
      while (lastentry->next) {
        // Find the end of the chain
        lastentry = lastentry->next;
      }
      lastentry->next = newstr;
    }
    this->count++;
    if (this->count > this->capacity * TABLE_MAX_LOAD) {
      // The hash table is getting a bit too big.  Increase the size
      // and re-balance it.
      grow_string_table();
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

void StrTable::grow_string_table() {
  // Increase the size of the string table and reallocate the hashed
  // strings to new buckets.  This is a SLOW process, so should be done
  // infrequently!

  uint16_t newcapacity =  GROW_CAPACITY(this->capacity);
  if (newcapacity < this->capacity) {
    // We have overflowed the string table.  This is a problem.
    // (Unlikely, since we have over 2 billion entries, but still...)
    logerr("Unable to grow string interning table - overflow.\n");
    return;
  }
  String **newhash = GROW_ARRAY(String *, NULL, 0,
                                        sizeof(String *) * newcapacity);
  // An empty bucket is represented by NULL
  for (int i = 0; i < newcapacity; i++) {
    newhash[i] = NULL;
  }
  // Iterate over every string in the old table, recalculate its bucket
  // and insert it into the new table.
  for (uint16_t index = 0; index < this->capacity; index++) {
    if (!this->hash[index]) {
      // No hashes here...
      continue;
    }
    // We have at least one string in this bucket.  Iterate over the
    // chain and move each entry to the new table.
    String *moveme = this->hash[index];
    do {
      uint16_t newindex = moveme->hash % newcapacity;
      if (!newhash[newindex]) {
        newhash[newindex] = moveme;
        moveme = moveme->next;
        newhash[newindex]->next = NULL;
      } else {
        String *end = newhash[newindex];
        while (end->next) {
          end = end->next;
        }
        end->next = moveme;
        moveme = moveme->next;
        end->next->next = NULL;
      }
    } while (moveme);
  }
  // Clean up and update the StrTable record.
  FREE_ARRAY(String *, this->hash, this->capacity);
  this->capacity = newcapacity;
  this->hash = newhash;
}

void StrTable::destroy_stringtable() {
  // Iterate through each String*, freeing the associated char* and
  // then the String* itself.
  for (uint16_t i = 0; i < this->capacity; i++) {
    if (this->hash[i]) {
      String *next = this->hash[i];
      while (next) {
        FREE_ARRAY(char, next->ptr, next->len+1);
        String *next2 = next->next;
        FREE_ARRAY(String, next, sizeof(String));
        next = next2;
      }
    }
  }
  FREE_ARRAY(StrTable *, this->hash, sizeof(StrTable *) * this->capacity);
}
