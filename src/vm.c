// The Sinistra VM object

#include "signal.h"

#include "config.h"
#include "memory.h"
#include "log.h"
#include "vm.h"

extern CONFIG_t config;

// Some shorthand
#define VM config.vm

VM_t *make_vm() {
  // Create a shiny new VM ready for use.
  VM_t *newvm = NULL;
  newvm = GROW_ARRAY(VM_t, newvm, 0, 1);
  newvm->callstack = make_callstack();
  newvm->stack = make_stack();
  return newvm;
}

void destroy_vm(VM_t *vm) {
  destroy_stack(vm->stack);
  destroy_callstack(vm->callstack);
  FREE_ARRAY(VM_t, vm, 1);
}

CALLSTACK_t *make_callstack() {
  // Allocate space for a new stack, and return it.
  CALLSTACK_t *stack = NULL;
  stack = GROW_ARRAY(CALLSTACK_t, stack, 0, 1);
  stack->max = CALLSTACK_SIZE - 1;
  stack->current = -1;
  return stack;
}

void destroy_callstack(CALLSTACK_t *stack) {
  // Byebye stack
  FREE_ARRAY(CALLSTACK_t, stack, 1);
}

void push_callstack(ITEM_t *item, uint8_t *nextop, uint8_t args) {
  // Store the currently-executing item on the call stack.
  // If arguments are being passed to the next item, adjust the
  // stack for this item to take into account.
  if (VM->callstack->current < VM->callstack->max) {
    VM->callstack->current++;
    VM->callstack->entry[VM->callstack->current].item = item;
    VM->callstack->entry[VM->callstack->current].nextop = nextop;
    VM->callstack->entry[VM->callstack->current].current_stack =
                                                 VM->stack->current - args;
    VM->callstack->entry[VM->callstack->current].current_base =
                                                        VM->stack->base;
    VM->callstack->entry[VM->callstack->current].current_locals =
                                                        VM->stack->locals;
    VM->callstack->entry[VM->callstack->current].current_params =
                                                        VM->stack->params;
    // The base is used when indexing into the stack in the current
    // frame (eg for accessing local variables).
    VM->stack->base = VM->stack->current + 1 - args;
  } else {
    logerr("Callstack overflow.\n");
    raise(SIGUSR1);
  }
}

FRAME_t *pop_callstack() {
  // Reset the VM to the previous stack:
  // Reset the value stack, then return a pointer to the frame
  // at the top of the callstack, so the interpreter can restore
  // its item and nextop.
  if (VM->callstack->current >= 0) {
    // First, reset the value stack to its previous state.
    reset_stack_to(VM->stack,
               VM->callstack->entry[VM->callstack->current].current_stack);
    VM->stack->locals =
                VM->callstack->entry[VM->callstack->current].current_locals;
    VM->stack->params =
                VM->callstack->entry[VM->callstack->current].current_params;
    VM->stack->base =
                VM->callstack->entry[VM->callstack->current].current_base;
    // Then decrement the callstack.
    VM->callstack->current--;
    // Finally return the old top of the callstack.
    return &VM->callstack->entry[VM->callstack->current + 1];
  }
  logerr("Callstack underflow.\n");
  raise(SIGUSR1);
  return NULL;
}

int size_callstack(CALLSTACK_t *stack) {
  // How many frames are on me?
  // An empty stack is size -1.
  return (stack->current + 1);
}

