// AST to IR lowering interface
//
// Licensed under the MIT License - see LICENSE file for details.

#pragma once

#include "absyn.h"
#include "semant.h"
#include "ir.h"

// Lower a semantically checked AST into backend IR.
// Returns ERR_NOERROR on success, otherwise an ERR_COMP_* code.
int lower_to_ir(AS_NODE *root, SEM_CTX *sem, IR_CTX *out, char **errdetail);

