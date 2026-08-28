// The interpreter.  Receives an item, and runs the code.

// Licensed under the MIT License - see LICENSE file for details.

#pragma once

#include "item.h"
#include "value.h"
#include "runtime_context.h"
#include "bytecode_format.h"

void init_interpreter(RuntimeContext *ctx);

// Executes a code item in the supplied runtime context. The returned VALUE_t is
// transferred to the caller by value; the caller owns the returned VALUE_t and
// must call value_free() when finished. interpret() borrows
// ctx, item, the itemstore, and ctx->vm, but mutates the VM stack/callstack while
// executing. The top-level return value is popped from the stack before it is
// returned.
//
// The executing ITEM_t holds one transient execution pin while its frame is
// active; the pin protects running code from deletion/replacement and does not
// transfer item ownership. Nested calls using
// the same RuntimeContext are supported: decoder/current-item state is saved and
// restored around the call, but the VM stack and callstack remain shared and are
// therefore intentionally affected by nested execution.
VALUE_t interpret(RuntimeContext *ctx, ITEM_t *item);

/* Shared by item-fetch and sys target invocation so executable validation and
 * runtime bytecode diagnostics remain one policy. */
bool runtime_verify_code_header(RuntimeContext *ctx, ITEM_t *item,
                                BC_FormatHeader *header);
void runtime_report_call_capacity_failure(RuntimeContext *ctx);
