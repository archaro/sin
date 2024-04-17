// Configuration object
// Useful for access to various bits of global data.

#pragma once

#include "vm.h"
#include "item.h"

typedef struct {
  VM_t vm;
  Allocator allocator;
  ITEM_t *itemroot;
  char *srcroot;
} CONFIG_t;

