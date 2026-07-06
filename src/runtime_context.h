// Runtime interpreter context

// Licensed under the MIT License - see LICENSE file for details.

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "item.h"
#include "vm.h"

typedef struct RuntimeContext RuntimeContext;

// Opcode functions have this form.  The runtime context owns the VM and
// per-invocation interpreter state that handlers need while executing.
typedef uint8_t *(*OP_t)(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item);

struct RuntimeContext {
  VM_t *vm;
  uint8_t *current_frame_start;
  uint8_t *current_frame_end;
  ITEM_t *current_item;
  ITEM_t *pending_call_item;
  OP_t opcode[256];
  bool interpreter_initialized;
};

void runtime_context_init(RuntimeContext *ctx, VM_t *vm);
