// The interpreter.  Receives an item, and runs the code.

// Licensed under the MIT License - see LICENSE file for details.

#pragma once

#include "item.h"
#include "value.h"

// opcode functions have this form
typedef uint8_t *(*OP_t)(uint8_t *nextop, ITEM_t *item);

void init_interpreter();
VALUE_t interpret(ITEM_t *item);
