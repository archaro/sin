// Semantic analysis for abstract syntax trees
//
// Licensed under the MIT License - see LICENSE file for details.

#pragma once

#include <stdint.h>

#include "absyn.h"

typedef struct {
  char *name;
  int8_t index;
  bool param;
} SEM_LOCAL;

typedef struct {
  SEM_LOCAL *locals;
  uint32_t count;
  uint32_t capacity;
  int8_t errnum;
  char *errdetail;
} SEM_CTX;

SEM_CTX *sem_create_ctx();
void sem_delete_ctx(SEM_CTX *ctx);
int8_t sem_check_locals(AS_NODE *root, char **errdetail, SEM_CTX *ctx);

