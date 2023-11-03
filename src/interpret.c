// The interpreter

#include <string.h>
#include <stdint.h>

#include "interpret.h"
#include "log.h"
#include "memory.h"
#include "value.h"
#include "stack.h"

typedef uint8_t *(*OP_t)(uint8_t *nextop, STACK_t* stack);
static OP_t opcode[256];

uint8_t *op_nop(uint8_t *nextop, STACK_t *stack) {
  return nextop;
}

uint8_t *op_undefined(uint8_t *nextop, STACK_t *stack) {
  logerr("Undefined opcode: %c\n", *(nextop-1));
  return nextop;
}

uint8_t *op_pushint(uint8_t *nextop, STACK_t *stack) {
  // Push an int64 onto the stack.
  // Read the next 8 bytes and make an VALUE_t
  VALUE_t v;
  v.type = VALUE_int;
  v.i = *(int64_t*)nextop;
  push_stack(stack, v);
  DEBUG_LOG("OP_PUSHINT: %ld\n", v.i);
  return nextop+8;
}

uint8_t *op_inclocal(uint8_t *nextop, STACK_t *stack) {
  // Interpret the next byte as an index into the locals.
  // If that local is an int, increment it.  Otherwise complain.
  uint8_t index = *nextop;
  if (stack->stack[index].type == VALUE_int) {
    stack->stack[index].i++;
  } else {
    logerr("Trying to increment non integer local variable.\n");
  }
  DEBUG_LOG("OP_INCLOCAL: index %d\n", index);
  return nextop+1;
}

uint8_t *op_declocal(uint8_t *nextop, STACK_t *stack) {
  // Interpret the next byte as an index into the locals.
  // If that local is an int, decrement it.  Otherwise complain.
  uint8_t index = *nextop;
  if (stack->stack[index].type == VALUE_int) {
    stack->stack[index].i--;
  } else {
    logerr("Trying to decrement non integer local variable.\n");
  }
  DEBUG_LOG("OP_DECLOCAL: index %d\n", index);
  return nextop+1;
}

uint8_t *op_jump(uint8_t *nextop, STACK_t *stack) {
  // Unconditional jump.  Interpret the next two bytes as a
  // SIGNED int, and then modify the bytecode pointer by that amount.
  int16_t offset;
  memcpy(&offset, nextop, 2);
  DEBUG_LOG("OP_JUMP: offset is  %d.\n", offset);
  return nextop + offset;
}

uint8_t *op_jumpfalse(uint8_t *nextop, STACK_t *stack) {
  // Evaluate the top of the stack.  If false, interpret next
  // two bytes as a SIGNED int, and modify the bytecode pointer
  // by that amount.  Alternatively, if true, simply skip the next
  // two bytes and go on to the next instruction.

  VALUE_t v1;
  v1 = pop_stack(stack);
  // "true" is a true bool value, or an int value != 0.
  // Everything else is false.
  if ((v1.type == VALUE_bool || v1.type == VALUE_int) && v1.i != 0) {
    // A true value means that we don't branch.  Skip over
    // the next two bytes.
    DEBUG_LOG("OP_JUMPFALSE: evaluates to true (no jump).\n");
    return nextop + 2;
  } else {
    // If not true then it must be false.  That's logic.
    int16_t offset;
    memcpy(&offset, nextop, 2);
    DEBUG_LOG("OP_JUMPFALSE: evaluates to false (jump offset %d).\n", offset);
    return nextop + offset;
  }
}

uint8_t *op_savelocal(uint8_t *nextop, STACK_t *stack) {
  // This is the quickest way, without extra pushes and pops.
  // Interpret the next byte as an index into the stack.
  uint8_t index = *nextop;

  // Then copy the top of the stack into that location.
  memcpy(&(stack->stack[index]), &(stack->stack[stack->current]),
                                                    sizeof(VALUE_t));
  // Then reduce the size of the stack.
  stack->current--;
  DEBUG_LOG("OP_SAVELOCAL: index %d\n", index);
  return nextop+1;
}

uint8_t *op_getlocal(uint8_t *nextop, STACK_t *stack) {
  // This is the quickest way, without extra pushes and pops.
  // Interpret the next byte as an index into the stack.
  uint8_t index = *nextop;

  // Then increase the size of the stack.
  stack->current++;
  // Then copy that location to the top of the stack.
  memcpy(&(stack->stack[stack->current]), &(stack->stack[index]),
                                                    sizeof(VALUE_t));
#ifdef DEBUG
  VALUE_t v;
  v = peek_stack(stack);
#endif
  DEBUG_LOG("OP_GETLOCAL: index %d value %d.\n", index, v.i);
  return nextop+1;
}

uint8_t *op_pushstr(uint8_t *nextop, STACK_t *stack) {
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
  DEBUG_LOG("OP_PUSHSTR: %s\n", v.s);
  return nextop + len;
}

uint8_t *op_add(uint8_t *nextop, STACK_t *stack) {
  // Pop two values from the stack.  If both ints, add them and push the
  // result onto the stack.  If both strings, concatenate them and do same.
  // If disparate types, push NIL onto the stack.
  VALUE_t v1, v2;
  v1 = pop_stack(stack);
  v2 = pop_stack(stack);
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
    logerr("Trying to add mismatched types '%c' and '%c'.  Result is NIL.\n", v1.type, v2.type);
    push_stack(stack, VALUE_NIL);
  }
  DEBUG_LOG("OP_ADD: types %d and %d\n", v1.type, v2.type);
  return nextop;
}

uint8_t *op_subtractint(uint8_t *nextop, STACK_t *stack) {
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
  DEBUG_LOG("OP_SUB: types %d and %d\n", v1.type, v2.type);
  return nextop;
}

uint8_t *op_divideint(uint8_t *nextop, STACK_t *stack) {
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
  DEBUG_LOG("OP_DIV: types %d and %d\n", v1.type, v2.type);
  return nextop;
}

uint8_t *op_multiplyint(uint8_t *nextop, STACK_t *stack) {
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
  DEBUG_LOG("OP_MUL: types %d and %d\n", v1.type, v2.type);
  return nextop;
}

uint8_t *op_negateint(uint8_t *nextop, STACK_t *stack) {
  // If the top value on the stack is an int, negate it.
  //  Complain bitterly if not.
  if (stack->stack[stack->current].type == VALUE_int) {
    stack->stack[stack->current].i = -stack->stack[stack->current].i;
  } else {
    logerr("Attempt to negate a value of type '%d'.\n",
                                    stack->stack[stack->current].type);
  }
  DEBUG_LOG("OP_NEGATE: type %d\n", stack->stack[stack->current].type);
  return nextop;
}

uint8_t *op_equal(uint8_t *nextop, STACK_t *stack) {
  // Compare the top two items on the stack and push back a VALUE_bool
  // that is either true or false.  Be sensible about what is equal.
  // At the moment pairs of bools, ints, or strings are considered.
  VALUE_t v1, v2, result;
  v1 = pop_stack(stack);
  v2 = pop_stack(stack);
  result.type = VALUE_bool;
  result.i = 1; // default to true
  if (v1.type == VALUE_int && v2.type == VALUE_int && v1.i == v2.i) {
    push_stack(stack, result);
    return nextop;
  } else if (v1.type == VALUE_str && v2.type == VALUE_str &&
                                                strcmp(v1.s, v2.s) == 0) {
    push_stack(stack, result);
    return nextop;
  } else if (v1.type == VALUE_bool && v2.type == VALUE_bool
                                                   && v1.i == v2.i) {
    push_stack(stack, result);
    return nextop;
  } 
  // If we get here, there is no equality
  result.i = 0;
  push_stack(stack, result);
  DEBUG_LOG("OP_EQUAL: types %d and %d\n", v1.type, v2.type);
  return nextop;
}

uint8_t *op_notequal(uint8_t *nextop, STACK_t *stack) {
  // The logical reverse of op_equal.
  // Note that mismatched types are always not equal.
  VALUE_t v1, v2, result;
  v1 = pop_stack(stack);
  v2 = pop_stack(stack);
  result.type = VALUE_bool;
  result.i = 1; // default to false
  if (v1.type == VALUE_int && v2.type == VALUE_int && v1.i != v2.i) {
    push_stack(stack, result);
    return nextop;
  } else if (v1.type == VALUE_str && v2.type == VALUE_str &&
                                                strcmp(v1.s, v2.s) != 0) {
    push_stack(stack, result);
    return nextop;
  } else if (v1.type == VALUE_bool && v2.type == VALUE_bool
                                                   && v1.i != v2.i) {
    push_stack(stack, result);
    return nextop;
  } else if (v1.type != v2.type) {
    // If the types do not match, there is no equality
    push_stack(stack, result);
    return nextop;
  }
  // If we get here there is equality, so return false.
  result.i = 0;
  push_stack(stack, result);
  DEBUG_LOG("OP_NOTEQUAL: types %d and %d\n", v1.type, v2.type);
  return nextop;
}

uint8_t *op_lessthan(uint8_t *nextop, STACK_t *stack) {
  // Compare the top two items on the stack and push back a VALUE_bool
  // that is either true or false.
  // At the moment pairs of bools or ints are considered.
  VALUE_t v1, v2, result;
  v1 = pop_stack(stack);
  v2 = pop_stack(stack);
  result.type = VALUE_bool;
  result.i = 1; // default to true
  if (v1.type == VALUE_int && v2.type == VALUE_int && v2.i < v1.i) {
    push_stack(stack, result);
    return nextop;
  } else if (v1.type == VALUE_bool && v2.type == VALUE_bool
                                                   && v2.i < v1.i) {
    push_stack(stack, result);
    return nextop;
  } 
  // If we get here the comparison is false
  result.i = 0;
  push_stack(stack, result);
  DEBUG_LOG("OP_LESSTHAN: types %d and %d\n", v1.type, v2.type);
  return nextop;
}

uint8_t *op_lessthanorequal(uint8_t *nextop, STACK_t *stack) {
  // Compare the top two items on the stack and push back a VALUE_bool
  // that is either true or false.
  // At the moment pairs of bools or ints are considered.
  VALUE_t v1, v2, result;
  v1 = pop_stack(stack);
  v2 = pop_stack(stack);
  result.type = VALUE_bool;
  result.i = 1; // default to true
  if (v1.type == VALUE_int && v2.type == VALUE_int && v2.i <= v1.i) {
    push_stack(stack, result);
    return nextop;
  } else if (v1.type == VALUE_bool && v2.type == VALUE_bool
                                                   && v2.i <= v1.i) {
    push_stack(stack, result);
    return nextop;
  } 
  // If we get here the comparison is false
  result.i = 0;
  push_stack(stack, result);
  DEBUG_LOG("OP_LTEQ: types %d and %d\n", v1.type, v2.type);
  return nextop;
}

uint8_t *op_greaterthan(uint8_t *nextop, STACK_t *stack) {
  // Compare the top two items on the stack and push back a VALUE_bool
  // that is either true or false.
  // At the moment pairs of bools or ints are considered.
  VALUE_t v1, v2, result;
  v1 = pop_stack(stack);
  v2 = pop_stack(stack);
  result.type = VALUE_bool;
  result.i = 1; // default to true
  if (v1.type == VALUE_int && v2.type == VALUE_int && v2.i > v1.i) {
    push_stack(stack, result);
    return nextop;
  } else if (v1.type == VALUE_bool && v2.type == VALUE_bool
                                                   && v2.i > v1.i) {
    push_stack(stack, result);
    return nextop;
  } 
  // If we get here the comparison is false
  result.i = 0;
  push_stack(stack, result);
  DEBUG_LOG("OP_GREATERTHAN: types %d and %d\n", v1.type, v2.type);
  return nextop;
}

uint8_t *op_greaterthanorequal(uint8_t *nextop, STACK_t *stack) {
  // Compare the top two items on the stack and push back a VALUE_bool
  // that is either true or false.
  // At the moment pairs of bools or ints are considered.
  VALUE_t v1, v2, result;
  v1 = pop_stack(stack);
  v2 = pop_stack(stack);
  result.type = VALUE_bool;
  result.i = 1; // default to true
  if (v1.type == VALUE_int && v2.type == VALUE_int && v2.i >= v1.i) {
    push_stack(stack, result);
    return nextop;
  } else if (v1.type == VALUE_bool && v2.type == VALUE_bool
                                                   && v2.i >= v1.i) {
    push_stack(stack, result);
    return nextop;
  } 
  // If we get here the comparison is false
  result.i = 0;
  push_stack(stack, result);
  DEBUG_LOG("OP_GTEQ: types %d and %d\n", v1.type, v2.type);
  return nextop;
}

uint8_t *op_logicalnot(uint8_t *nextop, STACK_t *stack) {
  // Logically negate the value on top of the stack.
  // Note that this operation CONVERTS the value on top of the stack to a
  // VALUE_bool if it is not already.
  switch (stack->stack[stack->current].type) {
    case VALUE_bool:
      stack->stack[stack->current].i = !(stack->stack[stack->current].i);
      break;
    case VALUE_int:
      // If the int value is nonzero, then false, else true.
      stack->stack[stack->current].type = VALUE_bool;
      if (stack->stack[stack->current].i) {
        stack->stack[stack->current].i = 0;
      } else {
        stack->stack[stack->current].i = 1;
      }
      break;
    case VALUE_nil:
      // A logically-negated nil value is always true
      stack->stack[stack->current].type = VALUE_bool;
      stack->stack[stack->current].i = 1;
      break;
    case VALUE_str:
      // A logically-negated string value is always false
      // Tidy up the old string value
      FREE_ARRAY(char, stack->stack[stack->current].s,
                              strlen(stack->stack[stack->current].s) + 1);
      stack->stack[stack->current].type = VALUE_bool;
      stack->stack[stack->current].i = 0;
      break;
  }
  return nextop;
}

uint8_t *op_logicaland(uint8_t *nextop, STACK_t *stack) {
  // Pop two values from the stack, convert to bools
  // AND the result and push it.
  VALUE_t v1 = convert_to_bool(pop_stack(stack));
  VALUE_t v2 = convert_to_bool(pop_stack(stack));
  // v2 is guaranteed to be boolean now, whatever it was.
  v2.i = v1.i && v2.i; // Logical AND
  push_stack(stack, v2);
  return nextop;
}

uint8_t *op_logicalor(uint8_t *nextop, STACK_t *stack) {
  // Pop two values from the stack, convert to bools
  // OR the result and push it.
  VALUE_t v1 = convert_to_bool(pop_stack(stack));
  VALUE_t v2 = convert_to_bool(pop_stack(stack));
  // v2 is guaranteed to be boolean now, whatever it was.
  v2.i = v1.i || v2.i; // Logical OR
  push_stack(stack, v2);
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
  opcode['j'] = op_jump;
  opcode['k'] = op_jumpfalse;
  opcode['l'] = op_pushstr;
  opcode['m'] = op_multiplyint;
  opcode['n'] = op_negateint;
  opcode['o'] = op_equal;
  opcode['p'] = op_pushint;
  opcode['q'] = op_notequal;
  opcode['r'] = op_lessthan;
  opcode['s'] = op_subtractint;
  opcode['t'] = op_greaterthan;
  opcode['u'] = op_lessthanorequal;
  opcode['v'] = op_greaterthanorequal;
  opcode['x'] = op_logicalnot;
  opcode['y'] = op_logicaland;
  opcode['z'] = op_logicalor;
}

VALUE_t interpret(ITEM_t *item) {
  // Given some bytecode, interpret it until the HALT instruction is seen
  // NB: The HALT opcode (currently represented by the character 'h') does
  // not have an associated function.

  // First set up the locals
  uint8_t numlocals = *item->bytecode;
  item->stack.current += numlocals;
  item->stack.locals = numlocals;
  DEBUG_LOG("Making space for %d locals.\n", numlocals);
  DEBUG_LOG("Stack size before interpreting begins: %d\n", size_stack(item->stack));
  // The actual bytecode starts at the second byte.
  uint8_t *op = item->bytecode + 1; 
  while (*op != 'h') {
    // We do it this way to avoid undefined behaviour between
    // two sequence points:
    uint8_t *nextop = op + 1;
    op = opcode[*op](nextop, &item->stack);
  }

  // There maybe somethign on the stack.  Return it if there is.
  if (size_stack(item->stack)) {
    return pop_stack(&item->stack);
  }

  // There shouldn't be anything else on the stack!
  if (size_stack(item->stack) > 0) {
    logerr("Error! Stack contains %d entries at end of intepretation.\n",
                                                  size_stack(item->stack));
  }

  // Otherwise return a nill.
  return VALUE_NIL;
}
