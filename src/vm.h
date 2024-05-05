// The virtual machine object
// There can be only one of these.

#pragma once

#include "stack.h"
#include "item.h"

#define CALLSTACK_SIZE 1024

typedef struct {
  ITEM_t *item;
  uint8_t *nextop;
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

VM_t *make_vm();
void destroy_vm(VM_t *vm);
CALLSTACK_t *make_callstack();
void destroy_callstack(CALLSTACK_t *stack);
void push_callstack(ITEM_t *item, uint8_t *nextop, uint8_t args);
FRAME_t *pop_callstack();
int size_callstack(CALLSTACK_t *stack);

