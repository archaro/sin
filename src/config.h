// Configuration object
// Useful for access to various bits of global data.

// Licensed under the MIT License - see LICENSE file for details.

#pragma once

#include <uv.h>

#include "vm.h"
#include "item.h"

// Default listener port (can be overriden with -p on command line)
#define LISTENER_PORT   4001

typedef struct {
  uv_loop_t *loop;      // Run loop context
  uv_tcp_t listener;    // Listener callback
  int fd;               // Listener file descriptor
  VM_t *vm;             // Virtual Machine
  ITEM_t *itemroot;     // Root of in-memory itemstore
  char *srcroot;        // Root of source tree
  char *itemstore;      // Filename of on-disk itemstore
  char *input;          // Name of the input item
  char *inputline;      // Item to receive the input line number
  char *inputtext;      // Item to receive the input data
  VM_t *input_vm;       // VM for the input task
  uint8_t maxconns;     // Maximum number of connected players
  uint8_t lastconn;     // Last connection processed by net.input
} CONFIG_t;

