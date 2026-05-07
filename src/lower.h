// Lowering interface for compiler pipeline.

// Licensed under the MIT License - see LICENSE file for details.

#pragma once

#include <stdint.h>

#include "absyn.h"
#include "ir.h"
#include "semant.h"

typedef struct {
  SEM_CTX *sem;
  IR_Unit *ir;
  int8_t errnum;
  char *errdetail;
} LOWER_CTX;

int8_t lower_ast_to_ir(AS_NODE *root, SEM_CTX *sem, IR_Unit **out_ir, char **errdetail);

