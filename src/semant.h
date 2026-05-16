// Semantic analysis for abstract syntax trees
//
// Licensed under the MIT License - see LICENSE file for details.

#pragma once

#include <stdint.h>
#include <stdbool.h>

#include "absyn.h"

typedef struct {
  char *name;
  uint8_t index;
  bool param;
} SEM_LOCAL;

typedef struct {
  const char *name;
  uint8_t index;
} SEM_LOCAL_INDEX;

typedef struct {
  SEM_LOCAL *locals;
  SEM_LOCAL_INDEX *local_index;
  uint32_t count;
  uint32_t capacity;
  uint32_t index_capacity;
  int8_t errnum;
  char *errdetail;
} SEM_CTX;

SEM_CTX *sem_create_ctx();
void sem_delete_ctx(SEM_CTX *ctx);

// Reusable per context:
// - preserves discovered locals in ctx
// - resets ctx error state on each call
// - if errdetail is non-NULL, returns an owned heap copy (caller frees)
int8_t sem_check_locals(AS_NODE *root, char **errdetail, SEM_CTX *ctx);
bool sem_get_local_index(SEM_CTX *ctx, const char *name, uint8_t *index_out);
