// The interpreter.  Receives an item, and runs the code.

// Licensed under the MIT License - see LICENSE file for details.

#pragma once

#include "item.h"
#include "value.h"
#include "runtime_context.h"

void init_interpreter(RuntimeContext *ctx);
VALUE_t interpret(RuntimeContext *ctx, ITEM_t *item);
// Temporary compatibility wrapper for legacy callers; core execution paths should pass a RuntimeContext explicitly.
VALUE_t interpret_legacy(ITEM_t *item);
