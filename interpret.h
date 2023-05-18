// The interpreter.  Receives an item, and runs the code.
// TODO: Ultimately, this should return a value somehow.
// How?

#pragma once

#include "item.h"

void init_interpreter();
int interpret(ITEM_t *item);
