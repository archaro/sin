// Compiler context API

// Licensed under the MIT License - see LICENSE file for details.

#pragma once

#include <stddef.h>
#include <stdint.h>

#include "compiler/absyn.h"
#include "compiler/emitbc.h"
#include "compiler/ir.h"
#include "compiler/semant.h"

typedef struct {
  const char *source;
  size_t source_len;

  AS_NODE *ast_root;
  SEM_CTX *sem_ctx;
  IR_Unit *ir_unit;
  OUTPUT_t *bytecode_out;

} CompilerContext;

void compiler_context_init(CompilerContext *ctx, const char *source, size_t source_len);
void compiler_context_reset(CompilerContext *ctx);
void compiler_context_destroy(CompilerContext *ctx);

int8_t compiler_context_prepare_bytecode_output(CompilerContext *ctx, size_t initial_size);
