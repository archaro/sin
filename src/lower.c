// Lowering module implementation.
// Lowers the AST to IR.

// Licensed under the MIT License - see LICENSE file for details.

#include "lower.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "error.h"

static void lower_set_error(LOWER_CTX *ctx, int8_t errnum, const char *detail) {
  if (!ctx || ctx->errnum != ERR_NOERROR) return;

  ctx->errnum = errnum;
  if (detail) {
    ctx->errdetail = strdup(detail);
  }
}

static void lower_set_unsupported(LOWER_CTX *ctx, const AS_NODE *node, const char *reason) {
  char buffer[128];
  int nodetype = node ? (int)node->nodetype : -1;
  snprintf(buffer, sizeof(buffer), "unsupported AST form: node=%d (%s)",
           nodetype, reason ? reason : "unknown");
  lower_set_error(ctx, ERR_COMP_SYNTAX, buffer);
}

static void lower_node(LOWER_CTX *ctx, AS_NODE *node);

static void lower_value_expr(LOWER_CTX *ctx, AS_NODE *node) {
  AS_VALUE *value;

  if (!node || node->nodetype != N_VALUE) {
    lower_set_unsupported(ctx, node, "expected value node");
    return;
  }

  value = (AS_VALUE *)node->lhs;
  if (!value) {
    lower_set_unsupported(ctx, node, "missing value payload");
    return;
  }

  switch (value->valtype) {
    case V_INT:
      ir_emit(ctx->ir, (IR_Inst){.op = IR_OP_PUSH_INT, .imm = value->value.i});
      return;
    case V_STR:
      lower_set_unsupported(ctx, node, "string lowering not implemented");
      return;
    case V_LOCAL: {
      uint8_t index = 0;
      if (!ctx->sem || !value->value.s ||
          !sem_get_local_index(ctx->sem, value->value.s, &index)) {
        lower_set_error(ctx, ERR_COMP_LOCALBEFOREDEF,
                        value->value.s ? value->value.s : "<null>");
        return;
      }
      ir_emit(ctx->ir, (IR_Inst){.op = IR_OP_LOAD_LOCAL, .a = index});
      return;
    }
    default:
      lower_set_unsupported(ctx, node, "value type unsupported");
      return;
  }
}

static void lower_binary_expr(LOWER_CTX *ctx, AS_NODE *node, IR_Op op) {
  lower_node(ctx, (AS_NODE *)node->lhs);
  if (ctx->errnum != ERR_NOERROR) return;
  lower_node(ctx, (AS_NODE *)node->rhs);
  if (ctx->errnum != ERR_NOERROR) return;
  ir_emit(ctx->ir, (IR_Inst){.op = op});
}

static void lower_node(LOWER_CTX *ctx, AS_NODE *node) {
  AS_STMTLIST *stmtlist;

  if (!ctx || !node || ctx->errnum != ERR_NOERROR) return;

  switch (node->nodetype) {
    case N_STMTLIST:
      stmtlist = (AS_STMTLIST *)node->lhs;
      if (!stmtlist) return;
      for (uint32_t i = 0; i < stmtlist->count; i++) {
        lower_node(ctx, stmtlist->stmts[i]);
        if (ctx->errnum != ERR_NOERROR) return;
      }
      return;

    case N_STMT:
    case N_EXPRSTMT:
      lower_node(ctx, (AS_NODE *)node->lhs);
      return;

    case N_RETURN:
      lower_node(ctx, (AS_NODE *)node->lhs);
      if (ctx->errnum != ERR_NOERROR) return;
      ir_emit(ctx->ir, (IR_Inst){.op = IR_OP_HALT});
      return;

    case N_VALUE:
      lower_value_expr(ctx, node);
      return;

    case N_ADD: lower_binary_expr(ctx, node, IR_OP_ADD); return;
    case N_SUB: lower_binary_expr(ctx, node, IR_OP_SUB); return;
    case N_MUL: lower_binary_expr(ctx, node, IR_OP_MUL); return;
    case N_DIV: lower_binary_expr(ctx, node, IR_OP_DIV); return;
    case N_EQUAL: lower_binary_expr(ctx, node, IR_OP_EQ); return;
    case N_NOTEQ: lower_binary_expr(ctx, node, IR_OP_NEQ); return;
    case N_LT: lower_binary_expr(ctx, node, IR_OP_LT); return;
    case N_LTEQ: lower_binary_expr(ctx, node, IR_OP_LE); return;
    case N_GT: lower_binary_expr(ctx, node, IR_OP_GT); return;
    case N_GTEQ: lower_binary_expr(ctx, node, IR_OP_GE); return;
    case N_AND: lower_binary_expr(ctx, node, IR_OP_AND); return;
    case N_OR: lower_binary_expr(ctx, node, IR_OP_OR); return;

    case N_NOT:
      lower_node(ctx, (AS_NODE *)node->lhs);
      if (ctx->errnum != ERR_NOERROR) return;
      ir_emit(ctx->ir, (IR_Inst){.op = IR_OP_NOT});
      return;

    default:
      lower_set_unsupported(ctx, node, "node type unsupported");
      return;
  }
}

int8_t lower_ast_to_ir(AS_NODE *root, SEM_CTX *sem, IR_Unit **out_ir, char **errdetail) {
  LOWER_CTX ctx;

  if (!out_ir) {
    if (errdetail) *errdetail = strdup("out_ir is NULL");
    return ERR_COMP_SYNTAX;
  }

  *out_ir = NULL;
  if (errdetail) *errdetail = NULL;

  memset(&ctx, 0, sizeof(ctx));
  ctx.sem = sem;
  ctx.ir = ir_create_unit();
  ctx.errnum = ERR_NOERROR;

  if (!ctx.ir) {
    if (errdetail) *errdetail = strdup("failed to allocate IR unit");
    return ERR_COMP_INUSE;
  }

  lower_node(&ctx, root);
  if (ctx.errnum == ERR_NOERROR) {
    ir_emit(ctx.ir, (IR_Inst){.op = IR_OP_HALT});
    *out_ir = ctx.ir;
    return ERR_NOERROR;
  }

  ir_destroy_unit(ctx.ir);
  if (errdetail) {
    *errdetail = ctx.errdetail;
  } else if (ctx.errdetail) {
    free(ctx.errdetail);
  }
  return ctx.errnum;
}

