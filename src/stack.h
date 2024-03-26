//  A simple stack, stacking VALUE_t types by value

#pragma once

#include <stdint.h>
#include <stdlib.h>

#include "value.h"

#define STACK_SIZE 1024

typedef struct {
  int32_t max;
  int32_t current;  // Current top of the stack
  int32_t base;     // Base of the stack in this frame
  uint8_t locals;   // Locals in this frame
  uint8_t params;   // Of which, this many are parameters
  VALUE_t stack[STACK_SIZE];
} STACK_t;

STACK_t *make_stack();
void destroy_stack(STACK_t *stack);
void reset_stack(STACK_t *stack);
void reset_stack_to(STACK_t *stack, int32_t top);
void push_stack(STACK_t *stack, VALUE_t obj);
VALUE_t pop_stack(STACK_t *stack);
void throwaway_stack(STACK_t *stack);
VALUE_t peek_stack(STACK_t *stack);
int size_stack(STACK_t *stack);
