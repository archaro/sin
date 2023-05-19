// The interpreter

#include <string.h>

#include "interpret.h"
#include "log.h"
#include "value.h"
#include "stack.h"

typedef char *(*OP_t)(char *nextop, STACK_t* stack);
static OP_t opcode[256];

char *op_nop(char *nextop, STACK_t *stack) {
  return nextop;
}

char *op_undefined(char *nextop, STACK_t *stack) {
  logerr("Undefined opcode: %c\n", *(nextop-1));
  return nextop;
}

char *op_pushint(char *nextop, STACK_t *stack) {
  // Push an int64 onto the stack.
  // Read the next 8 bytes and make an VALUE_t
  VALUE_t v;
  v.type = 'i';
  memcpy(&v.i, nextop, 8);
  push_stack(stack, v);
  return nextop+8;
}

char *op_addint(char *nextop, STACK_t *stack) {
  // Pop two int64s, add them together, then push the result onto the stack.
  // We assume that the parser and the programmer know what they are doing,
  // So whatever the two values are on the stack, this will be an integer
  // addition, and the result will also be an integer.
  VALUE_t v1, v2;
  v1 = pop_stack(stack);
  v2 = pop_stack(stack);
  v2.i += v1.i;
  v2.type = 'i';
  push_stack(stack, v2);
  return nextop;
}

char *op_subtractint(char *nextop, STACK_t *stack) {
  // Pop two int64s, subtract the last from the first, then push the result
  // onto the stack. onto the stack. We assume that the parser and the
  // programmer know what they are doing, so whatever the two values are on
  // the stack, this will be an integer result.
  VALUE_t v1, v2;
  v1 = pop_stack(stack);
  v2 = pop_stack(stack);
  v2.i -= v1.i;
  v2.type = 'i';
  push_stack(stack, v2);
  return nextop;
}

void init_interpreter() {
  // This function simply sets up the opcode dispatch table.
  for (int o=0; o<256; o++) {
    opcode[o] = op_undefined;
  }
  opcode[0] = op_nop;
  opcode['a'] = op_addint;
  opcode['p'] = op_pushint;
  opcode['s'] = op_subtractint;
}

int interpret(ITEM_t *item) {
  // Given some bytecode, interpret it until the HALT instruction is seen
  // NB: The HALT opcode (currently represented by the character 'h') does
  // not have an associated 
  char *op = item->bytecode;
  while (*op != 'h') {
    op = opcode[*op](++op, item->stack);
  }
  // THERE MUST BE SOMETHING ON THE STACK!
  // Pop it, and return its integer value, whatever it actuallt is.
  VALUE_t v = pop_stack(item->stack);
  return v.i;
}
