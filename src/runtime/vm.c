// The Sinistra VM object

// Licensed under the MIT License - see LICENSE file for details.

#include <stdlib.h>
#include "config.h"
#include "memory.h"
#include "log.h"
#include "vm.h"

VM_t *make_vm(void) {
  // Create a shiny new VM ready for use.
  VM_t *newvm = malloc(sizeof *newvm);
  if (!newvm) return NULL;
  newvm->callstack = make_callstack();
  newvm->stack = make_stack();
  if (!newvm->callstack || !newvm->stack) {
    destroy_vm(newvm);
    return NULL;
  }
  return newvm;
}

void destroy_vm(VM_t *vm) {
  if (!vm) return;
  destroy_stack(vm->stack);
  destroy_callstack(vm->callstack);
  free(vm);
}

CALLSTACK_t *make_callstack(void) {
  // Allocate space for a new stack, and return it.
  CALLSTACK_t *stack = malloc(sizeof *stack);
  if (!stack) return NULL;
  stack->max = CALLSTACK_SIZE - 1;
  stack->current = -1;
  return stack;
}

void destroy_callstack(CALLSTACK_t *stack) {
  // Byebye stack
  if (!stack) return;
  free(stack);
}

int size_callstack(CALLSTACK_t *stack) {
  // How many frames are on me?
  // An empty stack is size -1.
  return (stack->current + 1);
}
