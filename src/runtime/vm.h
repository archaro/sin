// The virtual machine object

// Licensed under the MIT License - see LICENSE file for details.

#pragma once

#include "stack.h"
#include "item.h"
#include "bytecode/bytecode_abi.h"
#include <stdbool.h>

#define CALLSTACK_SIZE SIN_BYTECODE_CALL_STACK_CAPACITY

typedef struct {
  // Caller continuation state (the frame to resume after callee HALT).
  ITEM_t *item;
  uint8_t *nextop; // Caller instruction pointer to resume at.
  uint8_t *bytecode_start;
  uint8_t *bytecode_end;
  int32_t current_stack;
  int32_t current_base;
  uint8_t current_locals;
  uint8_t current_params;
} FRAME_t;

typedef struct {
  int32_t max;
  int32_t current;  // Current top of stack
  FRAME_t entry[CALLSTACK_SIZE];
} CALLSTACK_t;


typedef struct VM {
  STACK_t *stack;
  CALLSTACK_t *callstack;
} VM_t;

VM_t *make_vm(void);
void destroy_vm(VM_t *vm);
CALLSTACK_t *make_callstack(void);
void destroy_callstack(CALLSTACK_t *stack);
int size_callstack(CALLSTACK_t *stack);
