// The interpreter

#include <string.h>
#include <stdint.h>
#include <stdlib.h>

#include "itoa.h"
#include "interpret.h"
#include "log.h"
#include "memory.h"
#include "parser.h"
#include "vm.h"
#include "value.h"
#include "stack.h"
#include "item.h"

// This is the virtual machine object, defined in sin.c
extern VM_t vm;

// This is defined in sin.c, and it is the root of the itemstore.
// It must be initialised before any function in this file is called.
extern ITEM_t *itemroot;

typedef uint8_t *(*OP_t)(uint8_t *nextop, STACK_t *stack, ITEM_t *item);
static OP_t opcode[256];

uint8_t *op_nop(uint8_t *nextop, STACK_t *stack, ITEM_t *item) {
  return nextop;
}

uint8_t *op_undefined(uint8_t *nextop, STACK_t *stack, ITEM_t *item) {
  logerr("Undefined opcode: %c\n", *(nextop-1));
  return nextop;
}

uint8_t *op_pushint(uint8_t *nextop, STACK_t *stack, ITEM_t *item) {
  // Push an int64 onto the stack.
  // Read the next 8 bytes and make an VALUE_t
  VALUE_t v;
  v.type = VALUE_int;
  v.i = *(int64_t*)nextop;
  push_stack(stack, v);
  DEBUG_LOG("OP_PUSHINT: %ld\n", v.i);
  return nextop+8;
}

uint8_t *op_inclocal(uint8_t *nextop, STACK_t *stack, ITEM_t *item) {
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

uint8_t *op_declocal(uint8_t *nextop, STACK_t *stack, ITEM_t *item) {
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

uint8_t *op_jump(uint8_t *nextop, STACK_t *stack, ITEM_t *item) {
  // Unconditional jump.  Interpret the next two bytes as a
  // SIGNED int, and then modify the bytecode pointer by that amount.
  int16_t offset;
  memcpy(&offset, nextop, 2);
  DEBUG_LOG("OP_JUMP: offset is  %d.\n", offset);
  return nextop + offset;
}

uint8_t *op_jumpfalse(uint8_t *nextop, STACK_t *stack, ITEM_t *item) {
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

uint8_t *op_savelocal(uint8_t *nextop, STACK_t *stack, ITEM_t *item) {
  // This is the quickest way, without extra pushes and pops.
  // Interpret the next byte as an index into the stack.
  uint8_t index = *nextop;
  // First check if the current value is a string.  If so, free it.
  if (stack->stack[index].type == VALUE_str) {
    free(stack->stack[index].s);
  }
  // Then copy the top of the stack into that location.
  memcpy(&(stack->stack[index]), &(stack->stack[stack->current]),
                                                    sizeof(VALUE_t));
  // Then reduce the size of the stack.
  stack->current--;
  DEBUG_LOG("OP_SAVELOCAL: index %d\n", index);
  return nextop+1;
}

uint8_t *op_getlocal(uint8_t *nextop, STACK_t *stack, ITEM_t *item) {
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
  switch (v.type) {
    case VALUE_int:
      DEBUG_LOG("OP_GETLOCAL: index %d value %d.\n", index, v.i);
      break;
    case VALUE_str:
      DEBUG_LOG("OP_GETLOCAL: index %d value '%s'.\n", index, v.s);
      break;
    default:
      DEBUG_LOG("OP_GETLOCAL: index %d type %d.\n", index, v.type);
  }
#endif
  return nextop+1;
}

uint8_t *op_pushstr(uint8_t *nextop, STACK_t *stack, ITEM_t *item) {
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

uint8_t *op_add(uint8_t *nextop, STACK_t *stack, ITEM_t *item) {
  // Pop two values from the stack.  If both ints, add them and push the
  // result onto the stack.  If both strings, concatenate them and do same.
  // If disparate types, push NIL onto the stack.
  VALUE_t v1, v2;
  v1 = pop_stack(stack);
  v2 = pop_stack(stack);
  // It makes sense to treat nil as 0 in this context.
  if ((v1.type == VALUE_nil || v1.type == VALUE_int) &&
                            (v2.type==VALUE_nil || v2.type == VALUE_int)) {
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

uint8_t *op_subtractint(uint8_t *nextop, STACK_t *stack, ITEM_t *item) {
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

uint8_t *op_divideint(uint8_t *nextop, STACK_t *stack, ITEM_t *item) {
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

uint8_t *op_multiplyint(uint8_t *nextop, STACK_t *stack, ITEM_t *item) {
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

uint8_t *op_negateint(uint8_t *nextop, STACK_t *stack, ITEM_t *item) {
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

uint8_t *op_equal(uint8_t *nextop, STACK_t *stack, ITEM_t *item) {
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

uint8_t *op_notequal(uint8_t *nextop, STACK_t *stack, ITEM_t *item) {
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

uint8_t *op_lessthan(uint8_t *nextop, STACK_t *stack, ITEM_t *item) {
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

uint8_t *op_lessthanorequal(uint8_t *nextop, STACK_t *stack, ITEM_t *item) {
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

uint8_t *op_greaterthan(uint8_t *nextop, STACK_t *stack, ITEM_t *item) {
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

uint8_t *op_greaterthanorequal(uint8_t *nextop, STACK_t *stack, ITEM_t *item) {
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

uint8_t *op_logicalnot(uint8_t *nextop, STACK_t *stack, ITEM_t *item) {
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

uint8_t *op_logicaland(uint8_t *nextop, STACK_t *stack, ITEM_t *item) {
  // Pop two values from the stack, convert to bools
  // AND the result and push it.
  VALUE_t v1 = convert_to_bool(pop_stack(stack));
  VALUE_t v2 = convert_to_bool(pop_stack(stack));
  // v2 is guaranteed to be boolean now, whatever it was.
  v2.i = v1.i && v2.i; // Logical AND
  push_stack(stack, v2);
  return nextop;
}

uint8_t *op_logicalor(uint8_t *nextop, STACK_t *stack, ITEM_t *item) {
  // Pop two values from the stack, convert to bools
  // OR the result and push it.
  VALUE_t v1 = convert_to_bool(pop_stack(stack));
  VALUE_t v2 = convert_to_bool(pop_stack(stack));
  // v2 is guaranteed to be boolean now, whatever it was.
  v2.i = v1.i || v2.i; // Logical OR
  push_stack(stack, v2);
  return nextop;
}

void assignitem(VALUE_t *itemname, VALUE_t val) {
  // Given two values, use the first as the name of an item, and
  // the second as the value to assign to it.  The item name must be
  // freed after insertion.
  // If the itemname does not resolve into a valid item, this function
  // must fail silently (log messages are fine).  In this case, if the
  // value to be saved has memory allocated to it, that must be freed.
  // In other words, this is an end stage for values - they are either
  // used or discarded.  The interpreter no longer cares.
  if (itemname->type == VALUE_str) {
    ITEM_t *i = insert_item(itemroot, itemname->s, val);
    if (!i) {
      logerr("Unable to create item '%s'.\n", itemname->s);
      if (val.type == VALUE_str) {
        FREE_ARRAY(char, val.s, strlen(val.s));
      }
    }
    DEBUG_LOG("Saved value of type %d in item %s\n", val.type, itemname->s);
  } else {
    logerr("Unable to create item: invalid name type %d\n", itemname->type);
    if (val.type == VALUE_str) {
      FREE_ARRAY(char, val.s, strlen(val.s));
    }
  }
  FREE_ARRAY(char, itemname->s, strlen(itemname->s));
}

uint8_t *op_assigncodeitem(uint8_t *nextop, STACK_t *stack, ITEM_t *item) {
  // Extract the embedded code from the bytestream, and compile it.
  // If the compilation is successful, assign its value to the item
  // on the top of the stack.  Otherwise, assign nil to the item.

  VALUE_t itemname = pop_stack(stack);
  // First, how much code do we have?
  uint16_t len;
  memcpy(&len, nextop, 2);
  nextop += 2;
  // Now create a temporary buffer to hold the source code.
  char *sourcecode = GROW_ARRAY(char, NULL, 0, len + 1);
  memcpy(sourcecode, nextop, len);
  sourcecode[len] = '\0';
  nextop += len;

  // We have the source.  Compile it.
  DEBUG_LOG("Source to compile: %s\n", sourcecode);
  OUTPUT_t *out = GROW_ARRAY(OUTPUT_t, NULL, 0, sizeof(OUTPUT_t));
  out->maxsize = 1024;
  out->bytecode = GROW_ARRAY(unsigned char, NULL, 0, out->maxsize);
  out->nextbyte = out->bytecode;
  bool result =  parse_source(sourcecode, len, out);

  if (result) {
    // Compilation succeeded.  Assign it to the item.
    // The item type is ITEM_code.
    uint32_t len = out->nextbyte - out->bytecode;
    insert_code_item(itemroot, itemname.s, len, out->bytecode);
    // Set the error item to a nil value.
    set_item(itemroot, "sys.error", VALUE_NIL);
  } else {
    // Compilation failed.
    // Assign nil to the item.
    assignitem(&itemname, VALUE_NIL);
    // Set the error item to the compiler error.
    // FIXME: This will need correcting when the compiler
    // returns proper error codes!
    set_item(itemroot, "sys.error", VALUE_NIL);
    FREE_ARRAY(unsigned char, out->bytecode, out->maxsize);
  }

  // Clean up.
  FREE_ARRAY(OUTPUT_t, out, sizeof(OUTPUT_t));
  FREE_ARRAY(char, sourcecode, len + 1);
  FREE_ARRAY(char, itemname.s, strlen(itemname.s));
  return nextop;
}

uint8_t *op_assignitem(uint8_t *nextop, STACK_t *stack, ITEM_t *item) {
  // Save a value into an item.
  VALUE_t val = pop_stack(stack); // value to be saved
  VALUE_t itemname = pop_stack(stack); // Name of item to save into
  assignitem(&itemname, val);
  return nextop;
}

uint8_t *op_fetchitem(uint8_t *nextop, STACK_t *stack, ITEM_t *item) {
  // Fetch a value from an item, and push it onto the stack.
  // The item name is a string at the top of the stack.
  // If the item is a code item, it is executed and the result pushed
  // onto the stack.
  // If the item does not exist, nil is pushed onto the stack.
  VALUE_t itemname = pop_stack(stack);

  // First check to see if there is a valid item to look up
  if (itemname.type == VALUE_str) {
    ITEM_t *i = find_item(itemroot, itemname.s);
    if (i) {
      DEBUG_LOG("Fetched item %s\n", itemname.s);
      // Just push the item value onto the stack.
      if (i->type == ITEM_value) {
        VALUE_t v;
        v.type = i->value.type;
        if (v.type == VALUE_str) {
          v.s = strdup(i->value.s);
        } else {
          v.i = i->value.i;
        }
        push_stack(stack, v);
      } else {
        // Save our current state.
        push_callstack(item, nextop);
        // Execute the item.
        DEBUG_LOG("Executing item %s\n", i->name);
        VALUE_t value = interpret(i);
        // Now go back to the status quo ante.
        FRAME_t *prev_frame = pop_callstack();
        item = prev_frame->item;
        nextop = prev_frame->nextop;
        // Having restored the old state, push the result
        // of the executed item.
        push_stack(stack, value);
      }
    } else {
      // Item not found.
      push_stack(stack, VALUE_NIL);
    }
    FREE_ARRAY(char, itemname.s, strlen(itemname.s));
  } else {
    logerr("Unable to fetch item: invalid item type for name: %d.\n", itemname.type);
    push_stack(stack, VALUE_NIL);
  }
  return nextop;
}

uint8_t *assembleitem_helper(uint8_t *nextop, STACK_t *stack, ITEM_t *item) {
  // Interpret the following bytecode as an item.  If an item can be
  // assembled, push the full item name onto the stack as a string.
  // Return a pointer to the bytecode after the item assembly.
  // May recurse - necessary for the handling of nested derefs.
  bool invalid = false;
  int size = 128;
  char *itemname = GROW_ARRAY(char, NULL, 0, size+2);
  itemname[0] = '\0';

  while (*nextop != 'E' && !invalid) {
    switch (*nextop++) {
      case 'L': {
        // Simple layer
        int s = *nextop++; // Length of layer name
        if (strlen(itemname) + s + 2 >= size) {
          itemname = GROW_ARRAY(char, itemname, size, (size*2)+2);
          size = (size * 2) + 2;
        }
        strncat(itemname, (char *)nextop, s);
        nextop += s;
        break;
      }
      case 'D': {
        // Deref layer - either a V (localvar) or another I (item)
        switch (*nextop++) {
          case 'V': {
            int idx = *nextop++; // Local variable index
            switch (stack->stack[idx].type) {
              case VALUE_str: {
                // This is easy, just concatenate the context of this local
                // Assuming it is a valid layer name, anyway.
                if (is_valid_layer(stack->stack[idx].s)) {
                  int sl = strlen(stack->stack[idx].s);
                  if (strlen(itemname) + sl + 2 >= size) {
                    itemname = GROW_ARRAY(char, itemname, size, (size*2)+2);
                    size = (size * 2) + 2;
                  }
                  strncat(itemname, stack->stack[idx].s, sl);
                } else {
                  logerr("Invalid layer name '%s'.\n", stack->stack[idx].s);
                  invalid = true;
                }
                break;
              }
              case VALUE_int: {
                // Slightly more complicated.  Turn the int into a string.
                char str[22]; // Big enough for MAXINT.
                itoa(stack->stack[idx].i, str, 10);
                int sl = strlen(str);
                if (strlen(itemname) + sl + 2 >= size) {
                  itemname = GROW_ARRAY(char, itemname, size, (size*2)+2);
                  size = (size * 2) + 2;
                }
                strncat(itemname, str, sl);
                break;
              }
              default: {
                // Not a valid value type to convert into a layer name.
                logerr("Layer type (%d) not int or string.\n", *nextop);
                invalid = true;
              }
            }
            break;
          }
          case 'I': {
            // This is a bit more complicated.  We need to dereference an
            // item, then evaluate it, and use the result as the layer name.
            nextop = assembleitem_helper(nextop, stack, item);
            VALUE_t layername = pop_stack(stack);
            if (layername.type == VALUE_str) {
              //  This is basically the same as op_fetchitem
              ITEM_t *i = find_item(itemroot, layername.s);
              if (i) {
                // We have an item.  Only two value types are allowed.
                switch (i->value.type) {
                  case VALUE_str: {
                    // This is the easiest one
                    if (is_valid_layer(i->value.s)) {
                      int sl = strlen(i->value.s);
                      if (strlen(itemname) + sl + 2 >= size) {
                        itemname = GROW_ARRAY(char, itemname, size, (size*2)+2);
                        size = (size * 2) + 2;
                      }
                      strncat(itemname, i->value.s, sl);
                    } else {
                      logerr("Invalid layer name '%s'.\n", i->value.s);
                      invalid = true;
                    }
                    break;
                  }
                  case VALUE_int: {
                    // This needs to be converted to a string.
                    char str[22]; // Big enough for MAXINT.
                    itoa(i->value.i, str, 10);
                    int sl = strlen(str);
                    if (strlen(itemname) + sl + 2 >= size) {
                      itemname = GROW_ARRAY(char, itemname, size, (size*2)+2);
                      size = (size * 2) + 2;
                    }
                    strncat(itemname, str, sl);
                    break;
                  }
                  default: {
                    logerr("Item dereference failed for '%s': invalid type.\n", layername.s);
                    invalid = true;
                  }
                }
              } else {
                logerr("Item dereference failed for '%s'.\n", layername.s);
                invalid = true;
              }
              FREE_ARRAY(char, layername.s, strlen(layername.s));
            } else {
              logerr("Invalid item layer type %d.\n", layername.type);
              invalid = true;
            }
            break;
          }
          default: {
            logerr("Invalid dereference layer type '%c' (%d).\n", *nextop, *nextop);
            invalid = true;
          }
        }
        break;
      }
      default: {
        logerr("Invalid layer type '%c' (%d).\n", *nextop, *nextop);
        invalid = true;
      }
    }
    if (*nextop != 'E') {
      // Another layer to process, so add a dot separator.
      strcat(itemname, ".");
    } else {
      // End of item definition.
      break;
    }
  }

  if (invalid) {
    // Not a valid item name, so push nil.
    FREE_ARRAY(char, itemname, size);
    push_stack(stack, VALUE_NIL);
  } else {
    VALUE_t name;
    name.type = VALUE_str;
    name.s = itemname; // Don't free itemname - it's on the stack!
    push_stack(stack, name);
    DEBUG_LOG("Item assembled: %s\n", itemname);
  }

  return nextop + 1;
}

uint8_t *op_assembleitem(uint8_t *nextop, STACK_t *stack, ITEM_t *item) {
  // Here beginneth an item definition.  Items are made up of layers, and
  // each layer may be either a simple layer name (a string matching the
  // regexp [_a-z0-9]), or it may be a dereference.  Dereferences are
  // either items (which may also contain dereferences), or they are
  // local variables.  In both cases, once the local variable or the item
  // has been fully determined, it needs to be looked up, and the
  // content should be substituted into that layer.  Nil values and empty
  // strings are prohibited.

  // When this function is called, the I opcode has been eaten.

  // To facilitate ease of recusive dereferences, this is just a wrapper
  // to the help function which does all the work.
  nextop = assembleitem_helper(nextop, stack, item);
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
  opcode['B'] = op_assigncodeitem;
  opcode['C'] = op_assignitem;
  opcode['F'] = op_fetchitem;
  opcode['I'] = op_assembleitem;
}

VALUE_t interpret(ITEM_t *item) {
  // Given some bytecode, interpret it until the HALT instruction is seen
  // NB: The HALT opcode (currently represented by the character 'h') does
  // not have an associated function.

  // First set up the locals
  uint8_t numlocals = *item->bytecode;

  // Set up the stack before executing it
  vm.stack->current += numlocals;
  vm.stack->locals = numlocals;
  DEBUG_LOG("Making space for %d locals.\n", numlocals);
  DEBUG_LOG("Stack size before interpreting begins: %d\n", size_stack(vm.stack));
  // The actual bytecode starts at the second byte.
  uint8_t *op = item->bytecode + 1; 
  while (*op != 'h') {
    // We do it this way to avoid undefined behaviour between
    // two sequence points:
    uint8_t *nextop = op + 1;
    op = opcode[*op](nextop, vm.stack, item);
  }

  // There should be no more than one value on the stack
  int stacksize = size_stack(vm.stack);
  if (stacksize > 1) {
    logerr("Error! Stack contains %d entries at end of intepretation.\n",
                                                  size_stack(vm.stack));
  }

  if (stacksize > 0) {
    return pop_stack(vm.stack);
  } else {
    // Otherwise return a nil.
    return VALUE_NIL;
  }
}
