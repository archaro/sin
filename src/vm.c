// The Sinistra VM object

#include "memory.h"
#include "log.h"
#include "vm.h"

// This is the virtual machine object, defined in sin.c
extern VM_t vm;

CALLSTACK_t *make_callstack() {
  // Allocate space for a new stack, and return it.
  CALLSTACK_t *stack = NULL;
  stack = GROW_ARRAY(CALLSTACK_t, stack, 0, 1);
  stack->max = CALLSTACK_SIZE;
  stack->current = -1;
  return stack;
}

void destroy_callstack(CALLSTACK_t *stack) {
  // Byebye stack
  FREE_ARRAY(CALLSTACK_t, stack, sizeof(CALLSTACK_t));
}

void push_callstack(ITEM_t *item, uint8_t *nextop) {
  // Store the currently-executing item on the call stack.
  if (vm.callstack->current < vm.callstack->max) {
    vm.callstack->current++;
    vm.callstack->entry[vm.callstack->current].item = item;
    vm.callstack->entry[vm.callstack->current].nextop = nextop;
    vm.callstack->entry[vm.callstack->current].current_stack =
                                                          vm.stack->current;
  } else {
    logerr("Callstack overflow.\n");
  }
}

FRAME_t *pop_callstack() {
  // Reset the VM to the previous stack:
  // Reset the value stack, then return a pointer to the frame
  // at the top of the callstack, so the interpreter can restore
  // its item and nextop.
  if (vm.callstack->current >= 0) {
    // First, reset the value stack to its previous state.
    reset_stack_to(vm.stack,
                 vm.callstack->entry[vm.callstack->current].current_stack);
    // Then decrement the callstack.
    vm.callstack->current--;
    // Finally return the old top of the callstack.
    return &vm.callstack->entry[vm.callstack->current + 1];
  }
  logerr("Callstack underflow.\n");
  return NULL;
}

int size_callstack(CALLSTACK_t *stack) {
  // How many frames are on me?
  // An empty stack is size -1.
  return (stack->current + 1);
}
