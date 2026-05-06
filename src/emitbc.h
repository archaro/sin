// IR to bytecode emitter interface
//
// Licensed under the MIT License - see LICENSE file for details.

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ir.h"

typedef struct {
  uint8_t *data;
  size_t len;
  size_t cap;
} BC_BUF;

// Emit a complete bytecode stream (header + opcodes + halt) from IR.
// Returns true on success.
bool emit_bytecode_from_ir(const IR_CTX *ir,
                           uint8_t numlocals,
                           uint8_t numparams,
                           BC_BUF *out,
                           char **errdetail);

void emitbc_free(BC_BUF *buf);
