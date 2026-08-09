// Configuration object
// Useful for access to various bits of global data.

// Licensed under the MIT License - see LICENSE file for details.

#pragma once

#include <stddef.h>
#include <uv.h>

#include "vm.h"
#include "item.h"

// Default listener port (can be overriden with -p on command line)
#define LISTENER_PORT   4001

typedef struct {
  uv_loop_t *loop;      // Run loop context
  int fd;               // Listener file descriptor
  VM_t *vm;             // Virtual Machine
  ITEMSTORE_t *itemstore_ctx; // Owned in-memory itemstore
  char *srcroot;        // Root of source tree
  char *itemstore;      // Filename of on-disk itemstore
  ITEMSTORE_DURABILITY_e itemstore_durability;
  char *input;          // Name of the input item
  char *inputline;      // Item to receive the input line number
  char *inputtext;      // Item to receive the input data
  VM_t *input_vm;       // VM for the input task
  bool safe_shutdown;   // Determines how to shut down.
  bool shutdown_requested; // True after sys.shutdown or sys.abort stops loop.
  bool strict_validation; // Verify bytecode while loading itemstores.
  bool strict_runtime_contracts; // Report runtime contract violations.
} CONFIG_t;
