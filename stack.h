//  A simple stack, stacking VALUE_t types by value

#pragma once

#include <stdint.h>
#include <stdlib.h>

#include "value.h"

typedef struct {
  int32_t max;
  int32_t current;
  VALUE_t *stack;
} STACK_t;

STACK_t *new_stack(uint32_t size);
void free_stack(STACK_t* stack);
void flatten_stack(STACK_t* stack);
void push_stack(STACK_t* stack, VALUE_t obj);
VALUE_t pop_stack(STACK_t* stack);
VALUE_t peek_stack(STACK_t* stack);
int size_stack(STACK_t* stack);
