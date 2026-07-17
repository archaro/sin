// Semantic analysis for abstract syntax trees
//
// Licensed under the MIT License - see LICENSE file for details.

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "compiler/absyn.h"
#include "compiler/compdiag.h"

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
  // Locals in first-seen append order; semantic local indices are stable and
  // match positions in this array.
  SEM_LOCAL *locals;
  // Name->index table built in append order and batch-sorted on demand before
  // lookups, trading O(n) insertion shifts for amortized O(1) appends.
  SEM_LOCAL_INDEX *local_index;
  uint32_t count;
  uint32_t capacity;
  uint32_t index_capacity;
  bool local_index_sorted;
  int8_t errnum;
  char *errdetail;
} SEM_CTX;

SEM_CTX *sem_create_ctx(void);
void sem_delete_ctx(SEM_CTX *ctx);

// Reusable per context:
// - preserves discovered locals in ctx
// - resets ctx error state on each call
// - if errdetail is non-NULL, returns an owned heap copy (caller frees)
int8_t sem_check_locals_diag(AS_NODE *root, char **errdetail, CompilerDiagnostic *diag, SEM_CTX *ctx);
int8_t sem_check_locals(AS_NODE *root, char **errdetail, SEM_CTX *ctx);
bool sem_get_local_index(SEM_CTX *ctx, const char *name, uint8_t *index_out);
void sem_seed_params(SEM_CTX *ctx, const char **params, size_t count);
