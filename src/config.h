// Configuration object
// Useful for access to various bits of global data.

#pragma once

#include <uv.h>

#include "vm.h"
#include "item.h"

// Default listener port (can be overriden with -p on command line)
#define LISTENER_PORT   4001

typedef struct {
  uv_loop_t *loop;      // Run loop context
  int fd;               // Listener file descriptor
  VM_t vm;              // Virtual Machine
  ITEM_t *itemroot;     // Root of in-memory itemstore
  char *srcroot;        // Root of source tree
  char *itemstore;      // Filename of on-disk itemstore
} CONFIG_t;

