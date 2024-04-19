// Configuration object
// Useful for access to various bits of global data.

#pragma once

#include "vm.h"
#include "item.h"

typedef struct {
  VM_t vm;              // Virtual Machine
  ITEM_t *itemroot;     // Root of in-memory itemstore
  char *srcroot;        // Root of source tree
  char *itemstore;      // Filename of on-disk itemstore
} CONFIG_t;

