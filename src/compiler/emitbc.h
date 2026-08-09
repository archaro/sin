// Bytecode emitter API

// Licensed under the MIT License - see LICENSE file for details.

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "compiler/ir.h"
#include "compiler/compdiag.h"
#include "parser.h"

bool emitbc_checked_size_add(size_t *total, size_t amount);
int8_t emit_bytecode_diag(IR_Unit *ir, uint8_t local_count, uint8_t param_count,
                          OUTPUT_t *out, char **errdetail, CompilerDiagnostic *diag);
int8_t emit_bytecode(IR_Unit *ir, uint8_t local_count, uint8_t param_count,
                     OUTPUT_t *out, char **errdetail);
