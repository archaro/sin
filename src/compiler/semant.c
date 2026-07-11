// Semantic analysis for abstract syntax trees
//
// Licensed under the MIT License - see LICENSE file for details.

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "log.h"
#include "compiler/absyn.h"
#include "error.h"
#include "memory.h"
#include "compiler/compdiag.h"
#include "compiler/semant.h"
 
static bool sem_has_local(SEM_CTX *ctx, const char *name) {
  return sem_get_local_index(ctx, name, NULL);
}

static void sem_set_error(SEM_CTX *ctx, int8_t errnum, const char *local_name);

static int sem_local_index_cmp(const void *a, const void *b) {
  const SEM_LOCAL_INDEX *lhs = (const SEM_LOCAL_INDEX *)a;
  const SEM_LOCAL_INDEX *rhs = (const SEM_LOCAL_INDEX *)b;
  return strcmp(lhs->name, rhs->name);
}

static void sem_sort_local_index_if_needed(SEM_CTX *ctx) {
  if (!ctx || ctx->local_index_sorted || ctx->count < 2) return;
  qsort(ctx->local_index, ctx->count, sizeof(SEM_LOCAL_INDEX),
        sem_local_index_cmp);
  ctx->local_index_sorted = true;
}

static bool sem_find_local_index_slot(SEM_CTX *ctx, const char *name,
                                      uint32_t *pos_out, bool *found_out) {
  uint32_t lo = 0;
  uint32_t hi = ctx->count;

  while (lo < hi) {
    uint32_t mid = lo + (hi - lo) / 2;
    int cmp = strcmp(name, ctx->local_index[mid].name);
    if (cmp == 0) {
      if (pos_out) *pos_out = mid;
      if (found_out) *found_out = true;
      return true;
    }
    if (cmp < 0) {
      hi = mid;
    } else {
      lo = mid + 1;
    }
  }

  if (pos_out) *pos_out = lo;
  if (found_out) *found_out = false;
  return true;
}

static void sem_add_local(SEM_CTX *ctx, const char *name) {
  if (!ctx || !name || ctx->errnum != ERR_NOERROR) return;

  if (sem_get_local_index(ctx, name, NULL)) {
    return;
  }

  if (ctx->count > UINT8_MAX) {
    sem_set_error(ctx, ERR_COMP_TOOMANYLOCALS, name);
    return;
  }

  if (ctx->count == ctx->capacity) {
    ctx->capacity = ctx->capacity == 0 ? 8 : ctx->capacity * 2;
    ctx->locals = realloc(ctx->locals, sizeof *ctx->locals * ctx->capacity);
  }

  if (ctx->count == ctx->index_capacity) {
    ctx->index_capacity = ctx->index_capacity == 0 ? 8 : ctx->index_capacity * 2;
    ctx->local_index = realloc(ctx->local_index,
                               sizeof *ctx->local_index * ctx->index_capacity);
  }

  SEM_LOCAL *local = &ctx->locals[ctx->count];
  local->name = strdup(name);
  local->index = (uint8_t)ctx->count;
  local->param = false;

  ctx->local_index[ctx->count].name = local->name;
  ctx->local_index[ctx->count].index = local->index;

  ctx->count++;
  ctx->local_index_sorted = false;
}

static uint32_t sem_resolve_local_index(SEM_CTX *ctx, const char *name) {
  uint32_t index = 0;
  uint8_t narrowed = 0;

  if (!ctx || !name) return 0;
  if (ctx->errnum != ERR_NOERROR) return ctx->count > 0 ? ctx->count - 1 : 0;

  if (!sem_get_local_index(ctx, name, &narrowed)) {
    sem_add_local(ctx, name);
    if (ctx->errnum != ERR_NOERROR) {
      return ctx->count > 0 ? ctx->count - 1 : 0;
    }
    if (!sem_get_local_index(ctx, name, &narrowed)) {
      return ctx->count > 0 ? ctx->count - 1 : 0;
    }
  }
  index = (uint32_t)narrowed;

  return index;
}

static void sem_set_error(SEM_CTX *ctx, int8_t errnum, const char *local_name) {
  if (!ctx) return;
  compdiag_set_once(&ctx->errnum, &ctx->errdetail, errnum, "semant",
                    local_name ? local_name : "<null>");
}

static void sem_set_embedded_error(SEM_CTX *ctx, int8_t errnum,
                                   const char *embedded_detail) {
  if (!ctx) return;
  compdiag_setf_once(&ctx->errnum, &ctx->errdetail, errnum, "semant",
                     "embedded code: %s",
                     embedded_detail ? embedded_detail : "<null>");
}

SEM_CTX *sem_create_ctx() {
  SEM_CTX *ctx = malloc(sizeof *ctx);
  (*ctx).locals = NULL;
  (*ctx).local_index = NULL;
  (*ctx).count = 0;
  (*ctx).capacity = 0;
  (*ctx).index_capacity = 0;
  (*ctx).local_index_sorted = true;
  (*ctx).errnum = ERR_NOERROR;
  (*ctx).errdetail = NULL;

  return ctx;
}

void sem_delete_ctx(SEM_CTX *ctx) {
  for (uint32_t i = 0; i < ctx->count; i++) {
    free(ctx->locals[i].name);
  }
  free(ctx->locals);
  free(ctx->local_index);
  compdiag_reset_detail(&ctx->errdetail);
  free(ctx);
}

static void sem_walk(SEM_CTX *ctx, AS_NODE *node);

static void sem_seed_code_params(SEM_CTX *ctx, AS_NODE *params) {
  AS_NODE *cursor = params;
  while (cursor) {
    if (cursor->nodetype != N_ARGLIST) {
      return;
    }

    AS_NODE *param = (AS_NODE *)cursor->lhs;
    if (param && param->nodetype == N_VALUE) {
      AS_VALUE *value = (AS_VALUE *)param->lhs;
      if (value && value->valtype == V_LOCAL && value->value.s) {
        uint32_t index = sem_resolve_local_index(ctx, value->value.s);
        ctx->locals[index].param = true;
      }
    }

    cursor = (AS_NODE *)cursor->rhs;
  }
}

static void sem_walk_if(SEM_CTX *ctx, AS_IF *ifstmt) {
  if (!ifstmt || ctx->errnum != ERR_NOERROR) return;

  sem_walk(ctx, ifstmt->condition);
  sem_walk(ctx, ifstmt->then);
  sem_walk_if(ctx, ifstmt->elsif);
}

static void sem_visit_value(SEM_CTX *ctx, AS_NODE *node) {
  if (!node || node->nodetype != N_VALUE || ctx->errnum != ERR_NOERROR) return;

  AS_VALUE *value = (AS_VALUE *)node->lhs;
  if (value->valtype != V_LOCAL) {
    return;
  }

  if (!sem_has_local(ctx, value->value.s)) {
    sem_set_error(ctx, ERR_COMP_LOCALBEFOREDEF, value->value.s);
  }
}

static void sem_walk(SEM_CTX *ctx, AS_NODE *node) {
  if (!node || ctx->errnum != ERR_NOERROR) return;

  switch (node->nodetype) {
    case N_STMTLIST: {
      AS_STMTLIST *stmtlist = (AS_STMTLIST *)node->lhs;
      for (uint32_t i = 0; i < stmtlist->count; i++) {
        sem_walk(ctx, stmtlist->stmts[i]);
      }
      return;
    }
    case N_ASSLOCAL: {
      // Bottom-up, left-to-right for assignment: evaluate RHS first,
      // then mark LHS local as defined.
      sem_walk(ctx, (AS_NODE *)node->rhs);
      if (ctx->errnum != ERR_NOERROR) return;

      AS_NODE *lhs = (AS_NODE *)node->lhs;
      if (!lhs || lhs->nodetype != N_VALUE) return;
      AS_VALUE *value = (AS_VALUE *)lhs->lhs;
      if (value->valtype == V_LOCAL) {
        sem_add_local(ctx, value->value.s);
      }
      return;
    }
    case N_IFSTMT: {
      sem_walk_if(ctx, (AS_IF *)node->lhs);
      return;
    }
    case N_CODE: {
      SEM_CTX *embedded = sem_create_ctx();
      sem_seed_code_params(embedded, (AS_NODE *)node->lhs);
      sem_walk(embedded, (AS_NODE *)node->rhs);

      if (embedded->errnum != ERR_NOERROR) {
        // Preserve embedded scope provenance in the final semantic error:
        // semant: embedded code: semant: <detail>
        sem_set_embedded_error(ctx, embedded->errnum, embedded->errdetail);
      }

      sem_delete_ctx(embedded);
      return;
    }
    case N_VALUE: {
      // This could be any sort of value, but right now we are only
      // interested if it is of type V_LOCAL
      sem_visit_value(ctx, node);
      return;
    }
    default: {
      sem_walk(ctx, (AS_NODE *)node->lhs);
      sem_walk(ctx, (AS_NODE *)node->rhs);
      sem_visit_value(ctx, node);
      return;
    }
  }
}

int8_t sem_check_locals_diag(AS_NODE *root, char **errdetail, CompilerDiagnostic *diag, SEM_CTX *ctx) {
  // sem_check_locals is reusable per SEM_CTX. It preserves discovered locals
  // across calls, but resets and re-computes per-call error state.
  //
  // Error detail text is stable and phase-qualified. Parent scope errors use:
  //   semant: <detail>
  // Embedded N_CODE errors use:
  //   semant: embedded code: semant: <detail>
  //
  // When an output buffer is requested, the detail is copied exactly once and
  // owned by the caller.
  compdiag_reset_detail(&ctx->errdetail);
  ctx->errnum = ERR_NOERROR;

  sem_walk(ctx, root);

  if (errdetail) {
    *errdetail = compdiag_copy_detail(ctx->errdetail);
  }

  if (diag && ctx->errnum != ERR_NOERROR) {
    compiler_diag_set(diag, ctx->errnum, DIAG_PHASE_SEMANT, ctx->errdetail ? ctx->errdetail : "");
  }

  return ctx->errnum;
}

int8_t sem_check_locals(AS_NODE *root, char **errdetail, SEM_CTX *ctx) {
  return sem_check_locals_diag(root, errdetail, NULL, ctx);
}

bool sem_get_local_index(SEM_CTX *ctx, const char *name, uint8_t *index_out) {
  // Find a local in the locals lookup table.
  // Return true or false depending on whether the local is found.
  // If the local is found, return its index in *index_out.
  if (!ctx || !name) {
    return false;
  }
  uint32_t slot = 0;
  bool found = false;

  sem_sort_local_index_if_needed(ctx);
  sem_find_local_index_slot(ctx, name, &slot, &found);
  if (!found) return false;

  if (index_out) *index_out = ctx->local_index[slot].index;
  return true;
}

void sem_seed_params(SEM_CTX *ctx, const char **params, size_t count) {
  if (!ctx || !params) return;
  for (size_t i = 0; i < count; i++) {
    if (!params[i]) continue;
    uint32_t index = sem_resolve_local_index(ctx, params[i]);
    ctx->locals[index].param = true;
  }
}
