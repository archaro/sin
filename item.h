// The item is the basic unit of storage.  It may contain a value or code.
// It may also contain nested items.  All items are evaluated.  A value item
// pushes a value onto the stack.  A code item is executed, and the value of
// the executed item is pushed onto the stack.
//
// Each code item is an isolated unit, with its own stack, etc.

#pragma once

#include "value.h"
#include "stack.h"

#define STACK_DEFAULT_SIZE 1024

typedef enum {ITEM_value, ITEM_code} ITEM_e;
typedef struct {
  ITEM_e type;
  char *name;
  STACK_t *stack;
  char *bytecode, *ip;
  int bytecode_len;
  VALUE_t val;
} ITEM_t;

ITEM_t *make_item(char *name, ITEM_e type, VALUE_t val, char *code, int len);
void free_item(ITEM_t *item);
