// Runtime interpreter context

// Licensed under the MIT License - see LICENSE file for details.

#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <uv.h>

#include "item.h"
#include "vm.h"
#include "runtime_decode.h"

typedef struct RuntimeContext RuntimeContext;

// Opcode functions have this form.  The runtime context owns the VM and
// per-invocation interpreter state that handlers need while executing.
typedef uint8_t *(*OP_t)(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item);

struct RuntimeContext {
  // Borrowed runtime dependencies supplied by process startup. Runtime
  // execution mutates the VM stack/callstack, the item tree contents, the
  // event loop handles, *maxconns, *lastconn, and *safe_shutdown, but does not
  // own or free these pointers or strings.
  VM_t *vm;
  ITEM_t *itemroot;
  uv_loop_t *loop;
  const char *itemstore_filename;
  ITEMSTORE_DURABILITY_e itemstore_durability;
  const char *srcroot;
  const char *input_name;
  const char *inputline_name;
  const char *inputtext_name;
  size_t *maxconns;
  size_t *lastconn;
  bool *safe_shutdown;
  bool strict_validation;

  // Owned by this RuntimeContext invocation and freely mutated while the
  // interpreter runs. The bytecode and ITEM_t objects referenced by these
  // fields remain borrowed from the itemstore or caller.
  RuntimeDecoder decoder;
  ITEM_t *current_item;
  ITEM_t *pending_call_item;
  OP_t opcode[256];
  bool interpreter_initialized;
};

void runtime_context_init(RuntimeContext *ctx, VM_t *vm);
