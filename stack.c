//  A simple stack, stacking VALUE_t types by value

#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>

#include "stack.h"
#include "memory.h"

STACK_t *new_stack(uint32_t size) {
  // Create a new stack of VALUE_t structs, and return a pointer to it.
  STACK_t *newstack = NULL;
  newstack = GROW_ARRAY(STACK_t, newstack, 0, sizeof(STACK_t));
  newstack->max = size;
  newstack->current = -1;
  newstack->stack = NULL;
  newstack->stack = GROW_ARRAY(VALUE_t, newstack->stack, 0,
                                            sizeof(VALUE_t) * size);
  return newstack;
}

void free_stack(STACK_t* stack) {
  // Given a stack, free its associated memorY
  if (stack) {
    if (stack->stack) {
      FREE_ARRAY(VALUE_t, stack->stack, stack->max);
    }
    FREE_ARRAY(STACK_t, stack, sizeof(STACK_t));
  }
}

void flatten_stack(STACK_t* stack) {
  // Given a stack, throw away everything on it.
  // Really simple!
  if (stack) {
    stack->current = -1;
  }
}

void push_stack(STACK_t* stack, VALUE_t obj) {
  // Given a stack and a value, store the value on the stack
  // By value, no memory management here.
  if (stack) {
    if (stack->current < stack->max) {
      stack->current++;
      stack->stack[stack->current] = obj;
    } else {
      printf("Stack overflow.\n");
    }
  }
}

VALUE_t pop_stack(STACK_t* stack) {
  // Given a stack, return the value on the top of the stack
  // and decrement the stack pointer.
  if (stack) {
    if (stack->current >= 0) {
      stack->current--;
      return stack->stack[stack->current + 1];
    } else {
      printf("Stack underflow.\n");
    }
  }
}

VALUE_t peek_stack(STACK_t* stack) {
  // Given a stack, return the value on the top of the stack
  // but DO NOT decrement the stack pointer.
  if (stack) {
    if (stack->current >= 0) {
      return stack->stack[stack->current];
    } else {
      printf("Peeking at empty stack.\n");
    }
  }
}

int size_stack(STACK_t* stack) {
  // How many items are on me?
  return (stack->current+1); // An empty stack is size -1
}

