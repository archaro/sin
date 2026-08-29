// Lowering interface for compiler pipeline.

// Licensed under the MIT License - see LICENSE file for details.

#pragma once

#include <stdint.h>

#include "compiler/absyn.h"
#include "compiler/ir.h"
#include "compiler/semant.h"
#include "compiler/compdiag.h"

typedef struct {
  SEM_CTX *sem;
  IR_Unit *ir;
  int8_t errnum;
  CompilerDiagnostic diagnostic;
  int32_t break_label;
  int32_t continue_label;
  uint32_t foreach_depth;
  CompilerSourceSpan error_span;
  CompilerSourceSpan current_span;
} LOWER_CTX;

int8_t lower_ast_to_ir_diag(AS_NODE *root, SEM_CTX *sem, IR_Unit **out_ir,
                            CompilerDiagnostic *diag);
