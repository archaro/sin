// The interpreter

#include <stdio.h>
#include <string.h>

#include "interpret.h"
#include "value.h"
#include "stack.h"

typedef char *(*OP_t)(char *nextop, STACK_t* stack);
static OP_t opcode[256];

char *op_nop(char *nextop, STACK_t *stack) {
  return nextop;
}

char *op_undefined(char *nextop, STACK_t *stack) {
  printf("Undefined opcode: %c\n", *(nextop-1));
  return nextop;
}

char *op_pushint(char *nextop, STACK_t *stack) {
  // Push an int64 onto the stack.
  // Read the next 8 bytes and make an VALUE_t
  VALUE_t o;
  o.type = 'i';
  memcpy(&o.i, nextop, 8);
  push_stack(stack, o);
  return nextop+8;
}

void init_interpreter() {
  // This function simply sets up the opcode dispatch table.
  for (int o=0; o<256; o++) {
    opcode[o] = op_undefined;
  }
  opcode[0] = op_nop;
  opcode['p'] = op_pushint;
}

int interpret(ITEM_t *item) {
  // Given some bytecode, interpret it until the HALT instruction is seen
  // NB: The HALT opcode (currently represented by the character 'h') does
  // not have an associated 
  char *op = item->bytecode;
  while (*op != 'h') {
    op = opcode[*op](++op, item->stack);
  }
  return 0;
}
