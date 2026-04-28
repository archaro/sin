// Semantic analysis for abstract syntax trees
//
// Licensed under the MIT License - see LICENSE file for details.

#pragma once

#include <stdint.h>

#include "absyn.h"

int8_t sem_check_locals(AS_NODE *root, char **errdetail);

