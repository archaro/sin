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
} FRAME_t;

typedef struct {
  int32_t max;
  int32_t current;
  FRAME_t entry[CALLSTACK_SIZE];
} CALLSTACK_t;


typedef struct VM {
  STACK_t *stack;
  CALLSTACK_t *callstack;
} VM_t;

CALLSTACK_t *make_callstack();
void destroy_callstack(CALLSTACK_t *stack);
void push_callstack(ITEM_t *item, uint8_t *nextop);
FRAME_t *pop_callstack();
int size_callstack(CALLSTACK_t *stack);

