//  A simple stack, stacking VALUE_t types by value

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "stack.h"
#include "memory.h"
#include "log.h"

void reset_stack(STACK_t *stack) {
  // Given a stack, throw away everything on it.
  // Really simple!
  for (int v = 0; v < stack->current; v++) {
    if (stack->stack[v].type == VALUE_str) {
      logmsg("Freeing string #%d: %s\n", v, stack->stack[v].s);
      FREE_ARRAY(char, stack->stack[v].s,
                                    strlen(stack->stack[v].s) + 1);
    }
  }
  stack->current = -1;
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
  // Given a stack, return the value on the top of the stack
  // and decrement the stack pointer.
  if (stack->current >= 0) {
    stack->current--;
    return stack->stack[stack->current + 1];
  }
  logerr("Stack underflow.\n");
  return VALUE_NIL;
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

int size_stack(STACK_t stack) {
  // How many items are on me?
  // An empty stack is size -1.  Also don't include any sneaky locals
  // which are freeloading at the bottom of the stack.
  return (stack.current + 1 - stack.locals);
}

