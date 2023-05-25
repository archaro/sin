// The interpreter

#include <string.h>

#include "interpret.h"
#include "log.h"
#include "memory.h"
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
  v.type = VALUE_int;
  memcpy(&v.i, nextop, 8);
  push_stack(stack, v);
  return nextop+8;
}

char *op_inclocal(char *nextop, STACK_t *stack) {
  // Interpret the next byte as an index into the locals.
  // If that local is an int, increment it.  Otherwise complain.
  uint8_t index = *nextop;
  if (stack->stack[index].type == VALUE_int) {
    stack->stack[index].i++;
  } else {
    logerr("Trying to increment non integer local variable.\n");
  }

  return nextop+1;
}

char *op_declocal(char *nextop, STACK_t *stack) {
  // Interpret the next byte as an index into the locals.
  // If that local is an int, decrement it.  Otherwise complain.
  uint8_t index = *nextop;
  if (stack->stack[index].type == VALUE_int) {
    stack->stack[index].i--;
  } else {
    logerr("Trying to decrement non integer local variable.\n");
  }

  return nextop+1;
}

char *op_savelocal(char *nextop, STACK_t *stack) {
  // This is the quickest way, without extra pushes and pops.
  // Interpret the next byte as an index into the stack.
  uint8_t index = *nextop;

  // Then copy the top of the stack into that location.
  memcpy(&(stack->stack[index]), &(stack->stack[stack->current]),
                                                    sizeof(VALUE_t));

  // Then reduce the size of the stack.
  stack->current--;

  return nextop+1;
}

char *op_getlocal(char *nextop, STACK_t *stack) {
  // This is the quickest way, without extra pushes and pops.
  // Interpret the next byte as an index into the stack.
  uint8_t index = *nextop;

  // Then increase the size of the stack.
  stack->current++;

  // Then copy that location to the top of the stack.
  memcpy(&(stack->stack[stack->current]), &(stack->stack[index]),
                                                    sizeof(VALUE_t));

  return nextop+1;
}

char *op_pushstr(char *nextop, STACK_t *stack) {
  // Push a string literal onto the stack.
  VALUE_t v;
  v.type = VALUE_str;
  uint16_t len;
  // Get the length
  memcpy(&len, nextop, 2);
  nextop += 2;
  v.s = GROW_ARRAY(char, NULL, 0, len+1);
  memcpy(v.s, nextop, len);
  v.s[len] = 0;
  push_stack(stack, v);
  return nextop + len;
}

char *op_add(char *nextop, STACK_t *stack) {
  // Pop two values from the stack.  If both ints, add them and push the
  // result onto the stack.  If both strings, concatenate them and do same.
  // If disparate types, push NIL onto the stack.
  VALUE_t v1, v2;
  v1 = pop_stack(stack);
  v2 = pop_stack(stack);
#ifdef DEBUG
  logmsg("OP_ADD: types %d and %d\n", v1.type, v2.type);
#endif
  if (v1.type == VALUE_int && v2.type == VALUE_int) {
    v2.i += v1.i;
    v2.type = VALUE_int;
    push_stack(stack, v2);
  } else if (v1.type == VALUE_str && v2.type == VALUE_str) {
    char *newstring;
    newstring = GROW_ARRAY(char, NULL, 0, strlen(v1.s) + strlen(v2.s) + 1);
    memcpy(newstring, v2.s, strlen(v2.s));
    memcpy(newstring + strlen(v2.s), v1.s, strlen(v1.s) + 1);
    FREE_ARRAY(char, v1.s, strlen(v1.s) + 1);
    FREE_ARRAY(char, v2.s, strlen(v2.s) + 1);
    v2.s = newstring;
    push_stack(stack, v2);
  } else {
    if (v1.type == VALUE_str) {
      free(v1.s);
    }
    if (v2.type == VALUE_str) {
      free(v2.s);
    }
    logerr("Trying to add mismatched types '%c' and '%c'.  Result is NIL.\n",
    v1.type, v2.type);
    push_stack(stack, VALUE_NIL);
  }
  return nextop;
}

char *op_subtractint(char *nextop, STACK_t *stack) {
  // Pop two int64s, subtract the last from the first, then push the result
  // onto the stack. We assume that the parser and the programmer know what
  // they are doing, so whatever the two values are on the stack, this will
  // be an integer result.
  VALUE_t v1, v2;
  v1 = pop_stack(stack);
  v2 = pop_stack(stack);
  v2.i -= v1.i;
  v2.type = VALUE_int;
  push_stack(stack, v2);
  return nextop;
}

char *op_divideint(char *nextop, STACK_t *stack) {
  // Pop two int64s, divide the last by the first, then push the result
  // onto the stack. We assume that the parser and the programmer know what
  // they are doing, so whatever the two values are on the stack, this will
  // be an integer result.
  // Trap divide by zero and substitute a result of zero.
  VALUE_t v1, v2;
  v1 = pop_stack(stack);
  v2 = pop_stack(stack);
  if (v1.i == 0) {
    logerr("Attempt to divide by zero.  Substitute zero as result.\n");
    v2.i = 0;
  } else {
    v2.i /= v1.i;
  }
  v2.type = VALUE_int;
  push_stack(stack, v2);
  return nextop;
}

char *op_multiplyint(char *nextop, STACK_t *stack) {
  // Pop two int64s, multiply them together, then push the result onto the stack.
  // We assume that the parser and the programmer know what they are doing,
  // So whatever the two values are on the stack, this will be an integer
  // addition, and the result will also be an integer.
  VALUE_t v1, v2;
  v1 = pop_stack(stack);
  v2 = pop_stack(stack);
  v2.i *= v1.i;
  v2.type = VALUE_int;
  push_stack(stack, v2);
  return nextop;
}

char *op_negateint(char *nextop, STACK_t *stack) {
  // If the top value on the stack is an int, negate it.
  //  Complain bitterly if not.
  if (stack->stack[stack->current].type == VALUE_int) {
    stack->stack[stack->current].i = -stack->stack[stack->current].i;
  } else {
    logerr("Attempt to negate a value of type '%d'.\n",
                                    stack->stack[stack->current].type);
  }
  return nextop;
}

void init_interpreter() {
  // This function simply sets up the opcode dispatch table.
  for (int o=0; o<256; o++) {
    opcode[o] = op_undefined;
  }
  opcode[0] = op_nop;
  opcode['a'] = op_add;
  opcode['c'] = op_savelocal;
  opcode['d'] = op_divideint;
  opcode['e'] = op_getlocal;
  opcode['f'] = op_inclocal;
  opcode['g'] = op_declocal;
  opcode['l'] = op_pushstr;
  opcode['m'] = op_multiplyint;
  opcode['n'] = op_negateint;
  opcode['p'] = op_pushint;
  opcode['s'] = op_subtractint;
}

VALUE_t interpret(ITEM_t *item) {
  // Given some bytecode, interpret it until the HALT instruction is seen
  // NB: The HALT opcode (currently represented by the character 'h') does
  // not have an associated function.

  // First set up the locals
  uint8_t numlocals = *item->bytecode;
  item->stack->current += numlocals;
  item->stack->locals = numlocals;
#ifdef DEBUG
  logmsg("Making space for %d locals.\n", numlocals);
  logmsg("Stack size before interpreting begins: %d\n", size_stack(item->stack));
#endif
  // The actual bytecode starts at the second byte.
  char *op = item->bytecode + 1; 
  while (*op != 'h') {
    op = opcode[*op](++op, item->stack);
  }

  // There maybe somethign on the stack.  Return it if there is.
  if (size_stack(item->stack)) {
    return pop_stack(item->stack);
  }

  // Otherwise return a nill.
  return VALUE_NIL;
}
