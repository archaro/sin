// The library call lookup
// Library calls are pseudo items, that are always of the form:
//   libname.callname{args}
// and always return a value.

// Licensed under the MIT License - see LICENSE file for details.

#pragma once

#include "runtime_context.h"
#include "libcall_registry.h"

// Libcall handlers use the OP_t opcode-handler ABI:
//   uint8_t *handler(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item)
// The compiler emits argument-evaluation bytecode before OP_LIBCALL, so handlers
// receive their arguments on ctx->vm->stack. A handler must consume exactly the
// number of arguments declared in the registry entry and leave exactly one
// VALUE_t return value on the stack before returning nextop.
//
// Popped argument VALUE_t objects become owned by the handler. String payloads
// in popped arguments must be freed unless the handler transfers ownership into
// the itemstore or returns the same value to Sinistra code. Handlers that mutate
// an argument in place without popping it leave that stack-owned value as the
// return value. Return values pushed onto the stack transfer ownership to the VM
// stack.
//
// Return nextop after normal completion, including Sinistra-visible failures that
// push nil/false. Return NULL only for fatal interpreter failures; interpret()
// treats NULL as an abort and returns VALUE_NIL.
