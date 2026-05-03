// Semantic analysis for abstract syntax trees
//
// Licensed under the MIT License - see LICENSE file for details.

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "log.h"
#include "absyn.h"
#include "error.h"
#include "memory.h"
#include "semant.h"
 
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

static bool sem_has_local(SEM_CTX *ctx, const char *name) {
  for (uint32_t i = 0; i < ctx->count; i++) {
    if (strcmp(ctx->locals[i].name, name) == 0) {
      return true;
    }
  }
  return false;
}

static void sem_add_local(SEM_CTX *ctx, const char *name) {
  if (sem_has_local(ctx, name)) {
    return;
  }
  if (ctx->count == ctx->capacity) {
    uint32_t oldcap = ctx->capacity;
    ctx->capacity = ctx->capacity == 0 ? 8 : ctx->capacity * 2;
    ctx->locals = GROW_ARRAY(SEM_LOCAL, ctx->locals, oldcap, ctx->capacity);
  }
  SEM_LOCAL *local = &ctx->locals[ctx->count];
  local->name = strdup(name);
  local->index = ctx->count;
  local->param = false;
  ctx->count++;
}

static void sem_set_error(SEM_CTX *ctx, int8_t errnum, const char *local_name) {
  if (ctx->errnum != ERR_NOERROR) {
    return;
  }
  ctx->errnum = errnum;
  if (local_name) {
    ctx->errdetail = strdup(local_name);
  }
}

static void sem_free_ctx(SEM_CTX *ctx) {
  for (uint32_t i = 0; i < ctx->count; i++) {
    free(ctx->locals[i].name);
  }
  FREE_ARRAY(SEM_LOCAL, ctx->locals, ctx->capacity);
}

static void sem_walk(SEM_CTX *ctx, AS_NODE *node);

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

int8_t sem_check_locals(AS_NODE *root, char **errdetail) {
  SEM_CTX ctx;
  ctx.locals = NULL;
  ctx.count = 0;
  ctx.capacity = 0;
  ctx.errnum = ERR_NOERROR;
  ctx.errdetail = NULL;

  sem_walk(&ctx, root);

  if (errdetail) {
    *errdetail = ctx.errdetail;
  } else if (ctx.errdetail) {
    free(ctx.errdetail);
  }

  // Local table!
  logmsg("Local table:\n");
  for (int i = 0; i < ctx.count; i++) {
    logmsg("Index %d: %s%s\n", ctx.locals[i].index, ctx.locals[i].name, ctx.locals[i].param?" (param)":"");
  }
  sem_free_ctx(&ctx);
  return ctx.errnum;
}

