// The interpreter.  Receives an item, and runs the code.
// TODO: Ultimately, this should return a value somehow.
// How?

#pragma once

#include "item.h"
#include "value.h"

void init_interpreter();
VALUE_t interpret(ITEM_t *item);
