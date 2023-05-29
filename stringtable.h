// A simple string wrapper for the interning of strings.

#pragma once

#include <stdint.h>

typedef enum {STR_comp, STR_interp}  STR_e;

typedef struct STRING_t {
  unsigned __int128 hash;
  uint16_t len;
  STR_e from;
  char *ptr;
  struct STRING_t *next;
} STRING_t;

typedef struct {
  uint16_t count;
  uint16_t capacity;
  STRING_t **hash;
} STRTABLE_t;

STRTABLE_t * make_stringtable(STRTABLE_t *table, uint16_t capacity);
unsigned __int128 hash_string(char *str, STR_e from, STRTABLE_t *table);
void grow_string_table(STRTABLE_t *table);
void destroy_stringtable(STRTABLE_t *table);
