// AST to IR lowering
//
// Licensed under the MIT License - see LICENSE file for details.

#include <stdbool.h>
#include <stdio.h>

#include "absyn.h"
#include "error.h"
#include "lower.h"
#include "semant.h"

typedef struct {
  SEM_CTX *sem;
  IR_CTX *out;
  char **errdetail;
} LOWER_CTX;

static int lower_expr(LOWER_CTX *ctx, AS_NODE *n);
static int lower_stmt(LOWER_CTX *ctx, AS_NODE *n);

static int lower_comp_fail(char **errdetail, int errnum, const char *msg) {
  if (errdetail) {
    *errdetail = (char *)msg;
  }
  return errnum;
}

// Net stack effect: +1 (pushes expression result).
static int lower_value(LOWER_CTX *ctx, AS_NODE *n) {
  (void)ctx;
  (void)n;
  return ERR_NOERROR;
}

// Net stack effect: +1 (consumes two operands, pushes one result).
static int lower_binary_expr(LOWER_CTX *ctx, AS_NODE *n) {
  int rc = lower_expr(ctx, (AS_NODE *)n->lhs);
  if (rc != ERR_NOERROR) return rc;
  rc = lower_expr(ctx, (AS_NODE *)n->rhs);
  if (rc != ERR_NOERROR) return rc;
  return ERR_NOERROR;
}

// Net stack effect: +1 (consumes one operand, pushes one result).
static int lower_unary_expr(LOWER_CTX *ctx, AS_NODE *n) {
  return lower_expr(ctx, (AS_NODE *)n->lhs);
}

// Net stack effect: +1 (result of function call).
static int lower_call_expr(LOWER_CTX *ctx, AS_NODE *n) {
  // lhs is callable, rhs is argument list; both are expression trees.
  int rc = lower_expr(ctx, (AS_NODE *)n->lhs);
  if (rc != ERR_NOERROR) return rc;
  if (n->rhs) {
    rc = lower_expr(ctx, (AS_NODE *)n->rhs);
    if (rc != ERR_NOERROR) return rc;
  }
  return ERR_NOERROR;
}

// Net stack effect: +1 (resolved item value).
static int lower_item_expr(LOWER_CTX *ctx, AS_NODE *n) {
  if (n->lhs) {
    int rc = lower_expr(ctx, (AS_NODE *)n->lhs);
    if (rc != ERR_NOERROR) return rc;
  }
  if (n->rhs) {
    int rc = lower_expr(ctx, (AS_NODE *)n->rhs);
    if (rc != ERR_NOERROR) return rc;
  }
  return ERR_NOERROR;
}

static int lower_expr(LOWER_CTX *ctx, AS_NODE *n) {
  if (!n) {
    return lower_comp_fail(ctx->errdetail, ERR_COMP_SYNTAX, "null expression node");
  }

  switch (n->nodetype) {
    case N_VALUE: {
      AS_VALUE *v = (AS_VALUE *)n->lhs;
      if (v && v->valtype == V_LOCAL) {
        uint8_t idx;
        if (!sem_get_local_index(ctx->sem, v->value.s, &idx)) {
          static char msg[256];
          snprintf(msg, sizeof(msg), "local lookup failed for '%s'", v->value.s);
          return lower_comp_fail(ctx->errdetail, ERR_COMP_LOCALBEFOREDEF, msg);
        }
      }
      return lower_value(ctx, n);
    }
    case N_ADD: case N_SUB: case N_MUL: case N_DIV:
    case N_EQUAL: case N_NOTEQ: case N_LT: case N_LTEQ: case N_GT: case N_GTEQ:
    case N_AND: case N_OR:
      return lower_binary_expr(ctx, n);
    case N_NOT:
      return lower_unary_expr(ctx, n);
    case N_CALL: case N_LIBCALL:
      return lower_call_expr(ctx, n);
    case N_ITEM: case N_NTHNAME: case N_ROOTNAME: case N_DEREF: case N_EXISTS: case N_DELETE:
      return lower_item_expr(ctx, n);
    default:
      return lower_comp_fail(ctx->errdetail, ERR_COMP_SYNTAX, "unsupported expression node");
  }
}

// Net stack effect: 0 (assignment consumes RHS value).
static int lower_assign_local_stmt(LOWER_CTX *ctx, AS_NODE *n) {
  AS_VALUE *dst = (AS_VALUE *)n->lhs;
  if (!dst || dst->valtype != V_LOCAL) {
    return lower_comp_fail(ctx->errdetail, ERR_COMP_SYNTAX, "invalid local assignment target");
  }
  uint8_t idx;
  if (!sem_get_local_index(ctx->sem, dst->value.s, &idx)) {
    static char msg[256];
    snprintf(msg, sizeof(msg), "local lookup failed for '%s'", dst->value.s);
    return lower_comp_fail(ctx->errdetail, ERR_COMP_LOCALBEFOREDEF, msg);
  }
  return lower_expr(ctx, (AS_NODE *)n->rhs);
}

// Net stack effect: 0 (item+RHS consumed by store operation).
static int lower_assign_item_stmt(LOWER_CTX *ctx, AS_NODE *n) {
  int rc = lower_expr(ctx, (AS_NODE *)n->lhs);
  if (rc != ERR_NOERROR) return rc;
  return lower_expr(ctx, (AS_NODE *)n->rhs);
}

// Net stack effect: 0 (expression value popped/discarded).
static int lower_expr_stmt(LOWER_CTX *ctx, AS_NODE *n) {
  return lower_expr(ctx, (AS_NODE *)n->lhs);
}

// Net stack effect: 0 (optional return value consumed by return op).
static int lower_return_stmt(LOWER_CTX *ctx, AS_NODE *n) {
  if (n->lhs) {
    return lower_expr(ctx, (AS_NODE *)n->lhs);
  }
  return ERR_NOERROR;
}

// Net stack effect: 0 (condition consumed by branch; branch bodies balanced).
static int lower_if_stmt(LOWER_CTX *ctx, AS_NODE *n) {
  AS_IF *branch = (AS_IF *)n->lhs;
  while (branch) {
    int rc = lower_expr(ctx, branch->condition);
    if (rc != ERR_NOERROR) return rc;
    rc = lower_stmt(ctx, branch->then);
    if (rc != ERR_NOERROR) return rc;
    branch = branch->elsif;
  }
  return ERR_NOERROR;
}

// Net stack effect: 0 (loop condition consumed each iteration; body balanced).
static int lower_while_stmt(LOWER_CTX *ctx, AS_NODE *n) {
  int rc = lower_expr(ctx, (AS_NODE *)n->lhs);
  if (rc != ERR_NOERROR) return rc;
  return lower_stmt(ctx, (AS_NODE *)n->rhs);
}

static int lower_stmt(LOWER_CTX *ctx, AS_NODE *n) {
  if (!n) {
    return ERR_NOERROR;
  }
  switch (n->nodetype) {
    case N_STMTLIST: {
      AS_STMTLIST *list = (AS_STMTLIST *)n->lhs;
      for (uint32_t i = 0; list && i < list->count; ++i) {
        int rc = lower_stmt(ctx, list->stmts[i]);
        if (rc != ERR_NOERROR) return rc;
      }
      return ERR_NOERROR;
    }
    case N_STMT:
      return lower_stmt(ctx, (AS_NODE *)n->lhs);
    case N_ASSLOCAL:
      return lower_assign_local_stmt(ctx, n);
    case N_ASSITEM:
      return lower_assign_item_stmt(ctx, n);
    case N_EXPRSTMT:
      return lower_expr_stmt(ctx, n);
    case N_RETURN:
      return lower_return_stmt(ctx, n);
    case N_IFSTMT:
      return lower_if_stmt(ctx, n);
    case N_WHILESTMT:
      return lower_while_stmt(ctx, n);
    default:
      return lower_comp_fail(ctx->errdetail, ERR_COMP_SYNTAX, "unsupported statement node");
  }
}

int lower_to_ir(AS_NODE *root, SEM_CTX *sem, IR_CTX *out, char **errdetail) {
  LOWER_CTX ctx = {.sem = sem, .out = out, .errdetail = errdetail};
  if (errdetail) *errdetail = NULL;
  if (!root || !sem || !out) {
    return lower_comp_fail(errdetail, ERR_COMP_SYNTAX, "lower_to_ir invalid arguments");
  }
  return lower_stmt(&ctx, root);
}

