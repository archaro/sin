// A simple string wrapper for the interning of strings.

#pragma once

#include <cstdint>

typedef enum {STR_comp, STR_interp}  STR_e;

class String {
  public:
    unsigned __int128 hash;
    uint16_t len;
    STR_e from;
    char *ptr;
    String *next;
};

class StrTable {
  private:
    uint16_t count;
    uint16_t capacity;
    void grow_string_table();
  public:
    String **hash;
    StrTable(uint16_t capacity);
    unsigned __int128 hash_string(char *str, STR_e from);
    void destroy_stringtable();
};

