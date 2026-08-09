//  A simple stack, stacking VALUE_t types by value

// Licensed under the MIT License - see LICENSE file for details.

#pragma once

#include <stdint.h>
#include <stdlib.h>

#include "bytecode/bytecode_abi.h"
#include "value.h"

#define STACK_SIZE SIN_BYTECODE_VALUE_STACK_CAPACITY

typedef struct {
  int32_t max;
  int32_t current;  // Current top of the stack
  int32_t base;     // Base of the stack in this frame
  uint8_t locals;   // Locals in this frame
  uint8_t params;   // Of which, this many are parameters
  VALUE_t stack[STACK_SIZE];
} STACK_t;

STACK_t *make_stack(void);
void destroy_stack(STACK_t *stack);
void reset_stack(STACK_t *stack);
// Free owned values above top and make top the new current stack index.
void reset_stack_to(STACK_t *stack, int32_t top);
void push_stack(STACK_t *stack, VALUE_t obj);
VALUE_t pop_stack(STACK_t *stack);
void throwaway_stack(STACK_t *stack);
VALUE_t *peek_stack(STACK_t *stack);
int size_stack(STACK_t *stack);
