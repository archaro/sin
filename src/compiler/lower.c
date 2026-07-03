// Lowering module implementation.
// Lowers the AST to IR.

// Licensed under the MIT License - see LICENSE file for details.

#include "lower.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "compdiag.h"
#include "error.h"
#include "libcall.h"

static void lower_set_error(LOWER_CTX *ctx, int8_t errnum, const char *detail) {
  if (!ctx) return;
  compdiag_set_once(&ctx->errnum, &ctx->errdetail, errnum, "lower", detail);
}

static void lower_set_unsupported(LOWER_CTX *ctx, const AS_NODE *node, const char *reason) {
  char buffer[128];
  int nodetype = node ? (int)node->nodetype : -1;
  snprintf(buffer, sizeof(buffer), "unsupported AST form: node=%d (%s)",
           nodetype, reason ? reason : "unknown");
  lower_set_error(ctx, ERR_COMP_SYNTAX, buffer);
}

static void lower_node(LOWER_CTX *ctx, AS_NODE *node);
static void lower_expr(LOWER_CTX *ctx, AS_NODE *node);
static void lower_stmt(LOWER_CTX *ctx, AS_NODE *node);
static void lower_stmtlist(LOWER_CTX *ctx, AS_NODE *node);
static void lower_arglist(LOWER_CTX *ctx, AS_NODE *arglist, int32_t *argc);

static void lower_item(LOWER_CTX *ctx, AS_NODE *item);
static bool lower_build_embedded_payload(LOWER_CTX *ctx, AS_NODE *node, int32_t *payload_index);
static bool lower_resolve_local_index(LOWER_CTX *ctx, AS_NODE *node, uint8_t *out_index);

static bool lower_resolve_local_index(LOWER_CTX *ctx, AS_NODE *node, uint8_t *out_index) {
  AS_VALUE *value;
  const char *name;
  uint8_t index = 0;

  if (!node || node->nodetype != N_VALUE) {
    lower_set_unsupported(ctx, node, "local target must be value(V_LOCAL)");
    return false;
  }

  value = (AS_VALUE *)node->lhs;
  if (!value || value->valtype != V_LOCAL) {
    lower_set_unsupported(ctx, node, "local target must be value(V_LOCAL)");
    return false;
  }

  name = value->value.s;
  if (!name) {
    lower_set_error(ctx, ERR_COMP_LOCALBEFOREDEF, "<null>");
    return false;
  }

  if (!ctx->sem) {
    lower_set_error(ctx, ERR_COMP_LOCALBEFOREDEF, name);
    return false;
  }

  if (!sem_get_local_index(ctx->sem, name, &index)) {
    lower_set_error(ctx, ERR_COMP_LOCALBEFOREDEF, name);
    return false;
  }

  if (out_index) *out_index = index;
  return true;
}

static void lower_layer_part(LOWER_CTX *ctx, AS_NODE *part) {
  AS_VALUE *value;

  if (!part || part->nodetype != N_VALUE) {
    lower_set_unsupported(ctx, part, "item layer is not a value");
    return;
  }

  value = (AS_VALUE *)part->lhs;
  if (!value) {
    lower_set_unsupported(ctx, part, "missing item layer payload");
    return;
  }

  switch (value->valtype) {
    case V_LAYER:
    case V_STR:
      ir_emit(ctx->ir, (IR_Inst){.op = IR_OP_ITEM_PUSH_LAYER,
                                 .imm = (int64_t)(intptr_t)value->value.s});
      return;
    case V_INT:
      {
        char buf[32];
        int len = snprintf(buf, sizeof(buf), "%lld", (long long)value->value.i);
        char *layer = NULL;
        if (len < 0 || (size_t)len >= sizeof(buf)) {
          lower_set_error(ctx, ERR_COMP_SYNTAX, "invalid integer layer literal");
          return;
        }
        layer = strdup(buf);
        if (!layer) {
          lower_set_error(ctx, ERR_COMP_SYNTAX, "out of memory formatting integer layer");
          return;
        }
        value->valtype = V_STR;
        value->value.s = layer;
        ir_emit(ctx->ir, (IR_Inst){.op = IR_OP_ITEM_PUSH_LAYER,
                                   .imm = (int64_t)(intptr_t)layer});
      }
      return;
    case V_FLOAT:
      lower_set_error(ctx, ERR_COMP_SYNTAX,
                      "float literals are not permitted as item layers");
      return;
    default:
      lower_set_unsupported(ctx, part, "unsupported item layer value type");
      return;
  }
}

static void lower_deref_payload(LOWER_CTX *ctx, AS_NODE *payload) {
  if (!payload) {
    lower_set_unsupported(ctx, payload, "missing deref payload");
    return;
  }

  if (payload->nodetype == N_ITEM || payload->nodetype == N_RELITEM) {
    lower_item(ctx, payload);
    return;
  }

  lower_expr(ctx, payload);
}

static void lower_item_deref_payload(LOWER_CTX *ctx, AS_NODE *payload) {
  if (!payload) {
    lower_set_unsupported(ctx, payload, "missing item-layer deref payload");
    return;
  }

  if (payload->nodetype == N_ITEM || payload->nodetype == N_RELITEM) {
    lower_item(ctx, payload);
    return;
  }

  if (payload->nodetype == N_VALUE) {
    AS_VALUE *value = (AS_VALUE *)payload->lhs;
    uint8_t index = 0;
    if (value && value->valtype == V_LOCAL &&
        lower_resolve_local_index(ctx, payload, &index)) {
      ir_emit(ctx->ir, (IR_Inst){.op = IR_OP_ITEM_PUSH_DEREF_LOCAL,
                                 .a = index});
      return;
    }
  }

  lower_set_unsupported(ctx, payload,
                        "item-layer deref must reference a local or item");
}

static void lower_item(LOWER_CTX *ctx, AS_NODE *item) {
  AS_NODE *cursor;
  bool relative = false;
  if (!item || (item->nodetype != N_ITEM && item->nodetype != N_RELITEM)) {
    lower_set_unsupported(ctx, item, "expected item node");
    return;
  }

  if (item->nodetype == N_RELITEM) {
    relative = true;
    item = (AS_NODE *)item->lhs;
    if (!item || item->nodetype != N_ITEM) {
      lower_set_unsupported(ctx, item, "invalid relative item payload");
      return;
    }
  }

  ir_emit(ctx->ir, (IR_Inst){.op = relative ? IR_OP_ITEM_BEGIN_REL : IR_OP_ITEM_BEGIN});
  cursor = item;
  while (cursor && ctx->errnum == ERR_NOERROR) {
    AS_NODE *part = (AS_NODE *)cursor->lhs;
    if (!part) {
      lower_set_unsupported(ctx, cursor, "missing item component");
      return;
    }

    if (part->nodetype == N_DEREF) {
      ir_emit(ctx->ir, (IR_Inst){.op = IR_OP_ITEM_PUSH_DEREF});
      lower_item_deref_payload(ctx, (AS_NODE *)part->lhs);
      if (ctx->errnum != ERR_NOERROR) return;
    } else {
      lower_layer_part(ctx, part);
      if (ctx->errnum != ERR_NOERROR) return;
    }

    cursor = (AS_NODE *)cursor->rhs;
  }

  if (ctx->errnum == ERR_NOERROR) {
    ir_emit(ctx->ir, (IR_Inst){.op = IR_OP_ITEM_END});
  }
}

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
    case V_FLOAT:
      ir_emit(ctx->ir, (IR_Inst){.op = IR_OP_PUSH_FLOAT, .imm = (int64_t)value->value.f_bits});
      return;
    case V_BOOLTRUE:
    case V_BOOLFALSE:
      ir_emit(ctx->ir, (IR_Inst){.op = IR_OP_PUSH_BOOL, .a = value->value.i ? 1 : 0});
      return;
    case V_STR:
      ir_emit(ctx->ir, (IR_Inst){.op = IR_OP_PUSH_STRING, .imm = (int64_t)(intptr_t)value->value.s});
      return;
    case V_LOCAL: {
      uint8_t index = 0;
      if (!lower_resolve_local_index(ctx, node, &index)) {
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
  lower_expr(ctx, (AS_NODE *)node->lhs);
  if (ctx->errnum != ERR_NOERROR) return;
  lower_expr(ctx, (AS_NODE *)node->rhs);
  if (ctx->errnum != ERR_NOERROR) return;
  ir_emit(ctx->ir, (IR_Inst){.op = op});
}

static void lower_expr(LOWER_CTX *ctx, AS_NODE *node) {
  if (!ctx || !node || ctx->errnum != ERR_NOERROR) return;

  switch (node->nodetype) {
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
      lower_expr(ctx, (AS_NODE *)node->lhs);
      if (ctx->errnum != ERR_NOERROR) return;
      ir_emit(ctx->ir, (IR_Inst){.op = IR_OP_NOT});
      return;

    case N_ITEM:
    case N_RELITEM:
      lower_item(ctx, node);
      return;

    case N_DEREF:
      lower_deref_payload(ctx, (AS_NODE *)node->lhs);
      if (ctx->errnum != ERR_NOERROR) return;
      ir_emit(ctx->ir, (IR_Inst){.op = IR_OP_ITEM_DEREF});
      return;

    case N_CALL: {
      int32_t argc = 0;
      lower_arglist(ctx, (AS_NODE *)node->rhs, &argc);
      if (ctx->errnum != ERR_NOERROR) return;
      // Call opcode expects stack top to be the item name, with arguments
      // below it. Emit argument expressions first, then the item expression.
      lower_expr(ctx, (AS_NODE *)node->lhs);
      if (ctx->errnum != ERR_NOERROR) return;
      ir_emit(ctx->ir, (IR_Inst){.op = IR_OP_CALL, .a = argc});
      return;
    }

    case N_LIBCALL: {
      AS_NODE *libitem = (AS_NODE *)node->lhs;
      AS_NODE *libnode;
      AS_NODE *funcnode;
      AS_VALUE *libval;
      AS_VALUE *funcval;
      uint8_t expected_args = 0;
      uint8_t token = 0;
      int32_t argc = 0;

      if (!libitem || libitem->nodetype != N_ITEM || !libitem->rhs) {
        lower_set_unsupported(ctx, node, "libcall target must be two-layer item");
        return;
      }
      libnode = (AS_NODE *)libitem->lhs;
      funcnode = (AS_NODE *)((AS_NODE *)libitem->rhs)->lhs;
      if (!libnode || !funcnode || libnode->nodetype != N_VALUE || funcnode->nodetype != N_VALUE) {
        lower_set_unsupported(ctx, node, "libcall target layers must be values");
        return;
      }

      libval = (AS_VALUE *)libnode->lhs;
      funcval = (AS_VALUE *)funcnode->lhs;
      if (!libval || !funcval || libval->valtype != V_LAYER || funcval->valtype != V_LAYER) {
        lower_set_unsupported(ctx, node, "libcall names must be layer identifiers");
        return;
      }

      lower_arglist(ctx, (AS_NODE *)node->rhs, &argc);
      if (ctx->errnum != ERR_NOERROR) return;
      if (!libcall_lookup_token(libval->value.s, funcval->value.s, &token, &expected_args)) {
        lower_set_unsupported(ctx, node, "unknown libcall target");
        return;
      }
      if ((uint8_t)argc != expected_args) {
        lower_set_unsupported(ctx, node, "invalid libcall argument count");
        return;
      }
      ir_emit(ctx->ir, (IR_Inst){.op = IR_OP_LIBCALL_TOKEN, .a = token});
      return;
    }

    case N_EXISTS:
      lower_item(ctx, (AS_NODE *)node->lhs);
      if (ctx->errnum != ERR_NOERROR) return;
      ir_emit(ctx->ir, (IR_Inst){.op = IR_OP_EXISTS});
      return;

    case N_DELETE:
      lower_item(ctx, (AS_NODE *)node->lhs);
      if (ctx->errnum != ERR_NOERROR) return;
      ir_emit(ctx->ir, (IR_Inst){.op = IR_OP_DELETE});
      return;

    case N_NTHNAME:
      lower_item(ctx, (AS_NODE *)node->lhs);
      if (ctx->errnum != ERR_NOERROR) return;
      lower_expr(ctx, (AS_NODE *)node->rhs);
      if (ctx->errnum != ERR_NOERROR) return;
      ir_emit(ctx->ir, (IR_Inst){.op = IR_OP_NTHNAME});
      return;

    case N_ROOTNAME:
      lower_expr(ctx, (AS_NODE *)node->lhs);
      if (ctx->errnum != ERR_NOERROR) return;
      ir_emit(ctx->ir, (IR_Inst){.op = IR_OP_ROOTNAME});
      return;

    case N_CODE: {
      int32_t payload_index = -1;
      if (!lower_build_embedded_payload(ctx, node, &payload_index)) return;
      ir_emit(ctx->ir, (IR_Inst){.op = IR_OP_ITEM_SAVE_CODE, .a = payload_index});
      return;
    }

    default:
      lower_set_unsupported(ctx, node, "expression node type unsupported");
      return;
  }
}

static void lower_arglist(LOWER_CTX *ctx, AS_NODE *arglist, int32_t *argc) {
  AS_NODE *cursor = arglist;
  if (argc) *argc = 0;
  while (cursor && ctx->errnum == ERR_NOERROR) {
    if (cursor->nodetype != N_ARGLIST) {
      lower_set_unsupported(ctx, cursor, "expected arglist node");
      return;
    }
    lower_expr(ctx, (AS_NODE *)cursor->lhs);
    if (ctx->errnum != ERR_NOERROR) return;
    if (argc) (*argc)++;
    cursor = (AS_NODE *)cursor->rhs;
  }
}

static void lower_stmtlist(LOWER_CTX *ctx, AS_NODE *node) {
  AS_STMTLIST *stmtlist;

  if (!ctx || !node || ctx->errnum != ERR_NOERROR) return;
  if (node->nodetype != N_STMTLIST) {
    lower_set_unsupported(ctx, node, "expected statement list");
    return;
  }

  stmtlist = (AS_STMTLIST *)node->lhs;
  if (!stmtlist) return;
  for (uint32_t i = 0; i < stmtlist->count; i++) {
    lower_stmt(ctx, stmtlist->stmts[i]);
    if (ctx->errnum != ERR_NOERROR) return;
  }
}

static void lower_stmt(LOWER_CTX *ctx, AS_NODE *node) {
  if (!ctx || !node || ctx->errnum != ERR_NOERROR) return;

  switch (node->nodetype) {
    case N_STMTLIST:
      lower_stmtlist(ctx, node);
      return;

    case N_STMT:
      lower_stmt(ctx, (AS_NODE *)node->lhs);
      return;

    case N_EXPRSTMT:
      /* Expression statements rely on existing interpreter behavior and do not
       * emit an explicit discard opcode. */
      lower_expr(ctx, (AS_NODE *)node->lhs);
      if (ctx->errnum != ERR_NOERROR) return;
      return;

    case N_RETURN:
      lower_expr(ctx, (AS_NODE *)node->lhs);
      if (ctx->errnum != ERR_NOERROR) return;
      ir_emit(ctx->ir, (IR_Inst){.op = IR_OP_HALT});
      return;

    case N_ASSLOCAL: {
      AS_NODE *local = (AS_NODE *)node->lhs;
      uint8_t index = 0;
      if (!lower_resolve_local_index(ctx, local, &index)) {
        return;
      }

      lower_expr(ctx, (AS_NODE *)node->rhs);
      if (ctx->errnum != ERR_NOERROR) return;
      ir_emit(ctx->ir, (IR_Inst){.op = IR_OP_STORE_LOCAL, .a = index});
      return;
    }

    case N_INC:
    case N_DEC: {
      AS_NODE *local = (AS_NODE *)node->lhs;
      uint8_t index = 0;
      if (!lower_resolve_local_index(ctx, local, &index)) {
        return;
      }

      ir_emit(ctx->ir, (IR_Inst){.op = node->nodetype == N_INC ? IR_OP_INC_LOCAL : IR_OP_DEC_LOCAL,
                                 .a = index});
      return;
    }

    case N_ASSITEM:
      lower_item(ctx, (AS_NODE *)node->lhs);
      if (ctx->errnum != ERR_NOERROR) return;
      if (((AS_NODE *)node->rhs)->nodetype == N_CODE) {
        int32_t payload_index = -1;
        if (!lower_build_embedded_payload(ctx, (AS_NODE *)node->rhs, &payload_index)) return;
        ir_emit(ctx->ir, (IR_Inst){.op = IR_OP_ITEM_SAVE_CODE, .a = payload_index});
      } else {
        lower_expr(ctx, (AS_NODE *)node->rhs);
        if (ctx->errnum != ERR_NOERROR) return;
        ir_emit(ctx->ir, (IR_Inst){.op = IR_OP_ITEM_SAVE});
      }
      return;

    case N_IFSTMT: {
      AS_IF *branch = (AS_IF *)node->lhs;
      int32_t end_label = ir_new_label(ctx->ir);

      while (branch != NULL && ctx->errnum == ERR_NOERROR) {
        int32_t else_label = ir_new_label(ctx->ir);

        if (branch->condition != NULL) {
          lower_expr(ctx, branch->condition);
          if (ctx->errnum != ERR_NOERROR) return;
          ir_emit(ctx->ir, (IR_Inst){.op = IR_OP_JUMP_IF_FALSE, .a = else_label});
        }

        lower_stmtlist(ctx, branch->then);
        if (ctx->errnum != ERR_NOERROR) return;

        ir_emit(ctx->ir, (IR_Inst){.op = IR_OP_JUMP, .a = end_label});
        ir_bind_label(ctx->ir, else_label);
        ir_emit(ctx->ir, (IR_Inst){.op = IR_OP_LABEL, .a = else_label});
        branch = branch->elsif;
      }

      ir_bind_label(ctx->ir, end_label);
      ir_emit(ctx->ir, (IR_Inst){.op = IR_OP_LABEL, .a = end_label});
      return;
    }

    case N_WHILESTMT: {
      int32_t start_label = ir_new_label(ctx->ir);
      int32_t end_label = ir_new_label(ctx->ir);

      ir_bind_label(ctx->ir, start_label);
      ir_emit(ctx->ir, (IR_Inst){.op = IR_OP_LABEL, .a = start_label});

      lower_expr(ctx, (AS_NODE *)node->lhs);
      if (ctx->errnum != ERR_NOERROR) return;
      ir_emit(ctx->ir, (IR_Inst){.op = IR_OP_JUMP_IF_FALSE, .a = end_label});

      lower_stmtlist(ctx, (AS_NODE *)node->rhs);
      if (ctx->errnum != ERR_NOERROR) return;
      ir_emit(ctx->ir, (IR_Inst){.op = IR_OP_JUMP, .a = start_label});

      ir_bind_label(ctx->ir, end_label);
      ir_emit(ctx->ir, (IR_Inst){.op = IR_OP_LABEL, .a = end_label});
      return;
    }

    default:
      lower_set_unsupported(ctx, node, "node type unsupported");
      return;
  }
}

static bool lower_build_embedded_payload(LOWER_CTX *ctx, AS_NODE *node, int32_t *payload_index) {
  AS_NODE *source;
  AS_VALUE *val;
  IR_EmbeddedCodePayload payload = {0};

  if (!node || node->nodetype != N_CODE) {
    lower_set_unsupported(ctx, node, "expected code node");
    return false;
  }
  source = (AS_NODE *)node->rhs;
  if (!source || source->nodetype != N_VALUE) {
    lower_set_unsupported(ctx, node, "code body must be value node");
    return false;
  }
  val = (AS_VALUE *)source->lhs;
  if (!val || val->valtype != V_STR) {
    lower_set_unsupported(ctx, node, "code body must be string");
    return false;
  }
  payload.source = val->value.s;
  if (!ir_embedded_locals_from_params((AS_NODE *)node->lhs, &payload)) {
    lower_set_unsupported(ctx, node, "code params must be local arg list");
    return false;
  }
  *payload_index = ir_add_embedded_code_payload(ctx->ir, payload);
  return true;
}

static void lower_node(LOWER_CTX *ctx, AS_NODE *node) {
  lower_stmt(ctx, node);
}

int8_t lower_ast_to_ir_diag(AS_NODE *root, SEM_CTX *sem, IR_Unit **out_ir, char **errdetail, CompilerDiagnostic *diag) {
  if (diag) compiler_diag_reset(diag);
  LOWER_CTX ctx;
  int8_t startup_errnum = ERR_NOERROR;

  if (!out_ir) {
    compdiag_set_once_diag(&startup_errnum, errdetail, diag, ERR_COMP_SYNTAX, DIAG_PHASE_LOWER, "lower", "out_ir is NULL");
    return startup_errnum;
  }

  *out_ir = NULL;

  memset(&ctx, 0, sizeof(ctx));
  ctx.sem = sem;
  ctx.ir = ir_create_unit();
  ctx.errnum = ERR_NOERROR;

  if (!libcall_init_registry()) {
    compdiag_set_once_diag(&startup_errnum, errdetail, diag, ERR_COMP_INUSE, DIAG_PHASE_LOWER, "lower",
                          "failed to initialize libcall registry");
    return startup_errnum;
  }

  if (!ctx.ir) {
    compdiag_set_once_diag(&startup_errnum, errdetail, diag, ERR_COMP_INUSE, DIAG_PHASE_LOWER, "lower", "failed to allocate IR unit");
    return startup_errnum;
  }

  lower_node(&ctx, root);
  if (ctx.errnum == ERR_NOERROR) {
    ir_emit(ctx.ir, (IR_Inst){.op = IR_OP_HALT});
    *out_ir = ctx.ir;
    return ERR_NOERROR;
  }

  ir_destroy_unit(ctx.ir);
  if (diag && ctx.errnum != ERR_NOERROR) {
    compiler_diag_set(diag, ctx.errnum, DIAG_PHASE_LOWER, ctx.errdetail ? ctx.errdetail : "");
  }
  if (errdetail) {
    *errdetail = ctx.errdetail;
  } else if (ctx.errdetail) {
    free(ctx.errdetail);
  }
  return ctx.errnum;
}

int8_t lower_ast_to_ir(AS_NODE *root, SEM_CTX *sem, IR_Unit **out_ir, char **errdetail) {
  return lower_ast_to_ir_diag(root, sem, out_ir, errdetail, NULL);
}
