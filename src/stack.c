//  A simple stack, stacking VALUE_t types by value

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "stack.h"
#include "memory.h"
#include "log.h"

STACK_t *make_stack() {
  // Allocate space for a new stack, and return it.
  STACK_t *stack = NULL;
  stack = GROW_ARRAY(STACK_t, stack, 0, 1);
  stack->max = STACK_SIZE;
  stack->current = -1;
  stack->base = 0;
  return stack;
}

void destroy_stack(STACK_t *stack) {
  // Byebye stack
  reset_stack(stack);
  FREE_ARRAY(STACK_t, stack, sizeof(STACK_t));
}

void reset_stack(STACK_t *stack) {
  // Given a stack, throw away everything on it.
  // Note that this includes any local variables!
  // Really simple!
  for (int v = 0; v < (stack->current + stack->locals); v++) {
    if (stack->stack[v].type == VALUE_str) {
      DEBUG_LOG("Freeing string: %s\n", stack->stack[v].s);
      FREE_ARRAY(char, stack->stack[v].s,
                                    strlen(stack->stack[v].s) + 1);
    }
  }
  stack->current = -1;
}

void reset_stack_to(STACK_t *stack, int32_t top) {
  // Like reset_stack, but only throw away values above 'top'
  while (stack->current > top) {
    stack->current--;
  }
}

void push_stack(STACK_t *stack, VALUE_t obj) {
  // Given a stack and a value, store the value on the stack
  // By value, no memory management here.
  if (stack->current < stack->max) {
    stack->current++;
    stack->stack[stack->current] = obj;
  } else {
    logerr("Stack overflow.\n");
  }
}

VALUE_t pop_stack(STACK_t *stack) {
  // Given a stack, return the value on top.
  // and decrement the stack pointer.
  // Set the type on the stack to nil, to prevent inadvertent
  // freeing of strings which may be in use elsewhere.
  if (stack->current >= 0) {
    VALUE_t val = stack->stack[stack->current];
    stack->stack[stack->current].type = VALUE_nil;
    stack->current--;
    return val;
  }
  logerr("Stack underflow.\n");
  return VALUE_NIL;
}

void throwaway_stack(STACK_t *stack) {
  // Given a stack, lose the value on top.
  // and decrement the stack pointer.
  // Set the type on the stack to nil, to prevent inadvertent
  // freeing of strings which may be in use elsewhere.
  if (stack->current >= 0) {
    if (stack->stack[stack->current].type == VALUE_str) {
      FREE_ARRAY(char, stack->stack[stack->current].s,
                                   strlen(stack->stack[stack->current].s));
    }
    stack->stack[stack->current].type = VALUE_nil;
    stack->current--;
  }
  logerr("Stack underflow.\n");
}

VALUE_t peek_stack(STACK_t *stack) {
  // Given a stack, return the value on the top of the stack
  // but DO NOT decrement the stack pointer.
  if (stack->current >= 0) {
    return stack->stack[stack->current];
  }
  logerr("Peeking at empty stack.\n");
  return VALUE_NIL;
}

int size_stack(STACK_t *stack) {
  // How many items are on me?
  // An empty stack is size -1.  Also don't include any sneaky locals
  // which are freeloading at the bottom of the stack.
  return (stack->current + 1 - stack->locals);
}

