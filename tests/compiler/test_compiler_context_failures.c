#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "compiler/compiler_context.h"
#include "compiler/compdiag.h"
#include "error.h"
#include "compiler/ir.h"
#include "compiler/lower.h"
#include "libcall.h"
#include "memory.h"
#include "parser.h"
#include "compiler/semant.h"
#include "test_assert.h"

enum { LOWER_ALLOC_FAILURE_TRIAL_LIMIT = 256 };

static void test_context_parse_failure_cleanup(void) {
  CompilerContext ctx;
  CompilerDiagnostic diag;
  compiler_diag_init(&diag);
  compiler_context_init(&ctx, "^;", 2);
  ASSERT_EQ_INT(0, compiler_context_prepare_bytecode_output(&ctx, 64));
  ctx.sem_ctx = sem_create_ctx();

  ParseInput input = {ctx.source, ctx.source_len, "<test>"};
  int8_t rc = parse_source_diag(&input, &ctx.ast_root, &diag, NULL);
  ASSERT_TRUE(rc != ERR_NOERROR);
  ASSERT_NOT_NULL(diag.message);

  compiler_diag_reset(&diag);
  compiler_context_destroy(&ctx);
}

static void test_context_semant_failure_cleanup(void) {
  CompilerContext ctx;
  CompilerDiagnostic diag;
  compiler_diag_init(&diag);
  const char *src = "@x;";
  compiler_context_init(&ctx, src, strlen(src));
  ASSERT_EQ_INT(0, compiler_context_prepare_bytecode_output(&ctx, 64));
  ctx.sem_ctx = sem_create_ctx();
  ParseInput input = {ctx.source, ctx.source_len, "<test>"};

  ASSERT_EQ_INT(ERR_NOERROR,
                parse_source_diag(&input, &ctx.ast_root, &diag, NULL));
  int8_t rc = sem_check_locals_diag(ctx.ast_root, &diag, ctx.sem_ctx);
  ASSERT_TRUE(rc != ERR_NOERROR);
  ASSERT_NOT_NULL(diag.message);

  compiler_diag_reset(&diag);
  compiler_context_destroy(&ctx);
}

static void test_context_lower_failure_cleanup(void) {
  CompilerContext ctx;
  CompilerDiagnostic diag;
  compiler_diag_init(&diag);
  const char *src = "@x;";
  compiler_context_init(&ctx, src, strlen(src));
  ASSERT_EQ_INT(0, compiler_context_prepare_bytecode_output(&ctx, 64));
  ctx.sem_ctx = sem_create_ctx();
  ParseInput input = {ctx.source, ctx.source_len, "<test>"};

  ASSERT_EQ_INT(ERR_NOERROR,
                parse_source_diag(&input, &ctx.ast_root, &diag, NULL));

  int8_t rc = lower_ast_to_ir_diag(ctx.ast_root, ctx.sem_ctx, &ctx.ir_unit,
                                   &diag);
  ASSERT_TRUE(rc != ERR_NOERROR);
  ASSERT_NOT_NULL(diag.message);

  compiler_diag_reset(&diag);
  compiler_context_destroy(&ctx);
}

static void test_context_emit_failure_cleanup(void) {
  CompilerContext ctx;
  CompilerDiagnostic diag;
  compiler_diag_init(&diag);
  compiler_context_init(&ctx, NULL, 0);
  ASSERT_EQ_INT(0, compiler_context_prepare_bytecode_output(&ctx, 64));

  ctx.ir_unit = ir_create_unit();
  ASSERT_NOT_NULL(ctx.ir_unit);
  ir_emit(ctx.ir_unit, (IR_Inst){.op = IR_OP_JUMP, .a = 999});
  ir_emit(ctx.ir_unit, (IR_Inst){.op = IR_OP_HALT});

  int8_t rc = emit_bytecode_diag(ctx.ir_unit, 0, 0, ctx.bytecode_out, &diag);
  ASSERT_TRUE(rc != ERR_NOERROR);
  ASSERT_NOT_NULL(diag.message);

  compiler_diag_reset(&diag);
  compiler_context_destroy(&ctx);
}

static void assert_lower_allocation_failures(const char *source) {
  CompilerContext ctx;
  CompilerDiagnostic diag;
  compiler_diag_init(&diag);
  IR_Unit *ir = NULL;
  bool saw_failure = false;
  bool saw_success_after_failure = false;

  compiler_context_init(&ctx, source, strlen(source));
  ctx.sem_ctx = sem_create_ctx();
  ASSERT_NOT_NULL(ctx.sem_ctx);
  ParseInput input = {ctx.source, ctx.source_len, "<test>"};
  ASSERT_EQ_INT(ERR_NOERROR,
                parse_source_diag(&input, &ctx.ast_root, &diag, NULL));
  ASSERT_TRUE(diag.message == NULL);
  ASSERT_EQ_INT(ERR_NOERROR,
                sem_check_locals_diag(ctx.ast_root, &diag, ctx.sem_ctx));
  ASSERT_TRUE(diag.message == NULL);

  alloc_test_fail_after(-1);
  ASSERT_EQ_INT(ERR_NOERROR,
                lower_ast_to_ir_diag(ctx.ast_root, ctx.sem_ctx, &ir, &diag));
  ASSERT_NOT_NULL(ir);
  ASSERT_TRUE(diag.message == NULL);
  ir_destroy_unit(ir);

  for (long fail_at = 0; fail_at < LOWER_ALLOC_FAILURE_TRIAL_LIMIT; fail_at++) {
    ir = NULL;
    compiler_diag_reset(&diag);
    alloc_test_fail_after(fail_at);
    int8_t rc = lower_ast_to_ir_diag(ctx.ast_root, ctx.sem_ctx, &ir,
                                     &diag);
    if (rc == ERR_NOERROR) {
      ASSERT_NOT_NULL(ir);
      ir_destroy_unit(ir);
      saw_success_after_failure = true;
      compiler_diag_reset(&diag);
      break;
    }

    saw_failure = true;
    ASSERT_TRUE(ir == NULL);
    ASSERT_EQ_INT(ERR_COMP_UNKNOWN, rc);
    ASSERT_EQ_INT(DIAG_PHASE_LOWER, diag.phase);
    ASSERT_NOT_NULL(diag.message);
    ASSERT_TRUE(strstr(diag.message, "lower:") != NULL);
    compiler_diag_reset(&diag);
  }

  alloc_test_fail_after(-1);
  ASSERT_TRUE(saw_failure);
  ASSERT_TRUE(saw_success_after_failure);
  compiler_context_destroy(&ctx);
}

static void test_lower_propagates_ir_allocation_failures(void) {
  ASSERT_TRUE(libcall_init_registry());
  assert_lower_allocation_failures(
      "1 + 2 + 3 + 4 + 5 + 6 + 7 + 8 + 9;");
  assert_lower_allocation_failures(
      "if 1 then 2; elsif 0 then 3; else 4; endif;");
  assert_lower_allocation_failures("while 1 do 2; endwhile;");
  assert_lower_allocation_failures("do 2; while 1;");
  assert_lower_allocation_failures("foo.bar = 1 + 2; sys.log{foo.bar};");
  assert_lower_allocation_failures("add = code {@a, @b} ( return @a + @b; );");
}

static bool span_matches(CompilerSourceSpan span, const CompilerDiagnostic *diag) {
  return diag->has_loc && diag->line == span.line &&
         diag->column == span.column && diag->span == span.span;
}

static void test_lower_control_flow_allocation_failure_preserves_provenance(void) {
  const char source[] = "while 1 do 2; endwhile;";
  CompilerContext ctx;
  ParseInput input;
  CompilerDiagnostic diag;
  compiler_diag_init(&diag);
  AS_STMTLIST *statements;
  AS_NODE *loop;
  AS_NODE *condition;
  AS_NODE *body_node;
  AS_STMTLIST *body;
  AS_NODE *body_statement;
  bool saw_failure = false;
  bool saw_provenance = false;

  alloc_test_fail_after(-1);
  compiler_context_init(&ctx, source, sizeof source - 1u);
  ctx.sem_ctx = sem_create_ctx();
  ASSERT_NOT_NULL(ctx.sem_ctx);
  input = (ParseInput){ctx.source, ctx.source_len, "lower-control-flow.src"};
  ASSERT_EQ_INT(ERR_NOERROR,
                parse_source_diag(&input, &ctx.ast_root, &diag, NULL));
  ASSERT_TRUE(diag.message == NULL);
  ASSERT_NOT_NULL(ctx.ast_root);
  ASSERT_EQ_INT(ERR_NOERROR,
                sem_check_locals_diag(ctx.ast_root, &diag, ctx.sem_ctx));
  ASSERT_TRUE(diag.message == NULL);

  statements = (AS_STMTLIST *)ctx.ast_root->lhs;
  ASSERT_EQ_INT(1, (int)statements->count);
  loop = statements->stmts[0];
  ASSERT_NOT_NULL(loop);
  ASSERT_EQ_INT(N_WHILESTMT, loop->nodetype);
  condition = (AS_NODE *)loop->lhs;
  body_node = (AS_NODE *)loop->rhs;
  ASSERT_NOT_NULL(condition);
  ASSERT_NOT_NULL(body_node);
  ASSERT_NOT_NULL(body_node->lhs);
  body = (AS_STMTLIST *)body_node->lhs;
  ASSERT_EQ_INT(1, (int)body->count);
  body_statement = body->stmts[0];

  for (long fail_at = 0; fail_at < LOWER_ALLOC_FAILURE_TRIAL_LIMIT; fail_at++) {
    IR_Unit *ir = NULL;
    alloc_test_fail_after(fail_at);
    int8_t rc = lower_ast_to_ir_diag(ctx.ast_root, ctx.sem_ctx, &ir,
                                     &diag);
    alloc_test_fail_after(-1);

    if (rc == ERR_NOERROR) {
      ASSERT_NOT_NULL(ir);
      ir_destroy_unit(ir);
      compiler_diag_reset(&diag);
      continue;
    }

    saw_failure = true;
    ASSERT_TRUE(ir == NULL);
    ASSERT_EQ_INT(ERR_COMP_UNKNOWN, rc);
    ASSERT_EQ_INT(DIAG_PHASE_LOWER, diag.phase);
    if (diag.has_loc) {
      ASSERT_TRUE(span_matches(loop->span, &diag) ||
                  span_matches(condition->span, &diag) ||
                  span_matches(body_node->span, &diag) ||
                  span_matches(body_statement->span, &diag));
      saw_provenance = true;
    }
    compiler_diag_reset(&diag);
  }

  ASSERT_TRUE(saw_failure);
  ASSERT_TRUE(saw_provenance);

  {
    IR_Unit *ir = NULL;
    ASSERT_EQ_INT(ERR_NOERROR,
                  lower_ast_to_ir_diag(ctx.ast_root, ctx.sem_ctx, &ir,
                                       &diag));
    ASSERT_NOT_NULL(ir);
    ASSERT_TRUE(diag.message == NULL);
    ASSERT_TRUE(!diag.has_loc);
    ir_destroy_unit(ir);
    compiler_diag_reset(&diag);
  }
  compiler_context_destroy(&ctx);
}

void test_compiler_context_failures(void) {
  test_context_parse_failure_cleanup();
  test_context_semant_failure_cleanup();
  test_context_lower_failure_cleanup();
  test_context_emit_failure_cleanup();
  test_lower_propagates_ir_allocation_failures();
  test_lower_control_flow_allocation_failure_preserves_provenance();

  alloc_test_fail_after(-1);
  {
    AS_NODE *list = as_new_stmtlist_node();
    ASSERT_NOT_NULL(list);
    for (int i = 0; i < 8; i++) {
      char buf[16];
      int written = snprintf(buf, sizeof(buf), "%d", i);
      ASSERT_TRUE(written > 0 && (size_t)written < sizeof(buf));
      ASSERT_TRUE(as_stmtlist_append_checked(list, as_new_valnode(V_INT, strdup(buf))));
    }
    AS_NODE *stmt2 = as_new_valnode(V_INT, strdup("9"));
    alloc_test_fail_after(1);
    ASSERT_TRUE(!as_stmtlist_append_checked(list, stmt2));
    as_delete(stmt2);
    as_delete(list);
  }
  alloc_test_fail_after(-1);

  alloc_test_fail_after(1);
  {
    IR_Unit *u = ir_create_unit();
    if (u) {
      ASSERT_EQ_INT((int)SIZE_MAX, (int)ir_emit(u, (IR_Inst){.op=IR_OP_HALT}));
      ir_destroy_unit(u);
    }
  }
  alloc_test_fail_after(-1);

  {
    IR_EmbeddedCodePayload payload = {0};
    AS_NODE *params = as_new_node(N_ARGLIST, as_new_valnode(V_LOCAL, strdup("who")), NULL);
    ASSERT_NOT_NULL(params);
    alloc_test_fail_after(0);
    ASSERT_TRUE(!ir_embedded_locals_from_params(params, &payload));
    ASSERT_TRUE(payload.params == NULL);
    ASSERT_TRUE(payload.locals == NULL);
    ASSERT_TRUE(payload.param_count == 0);
    ASSERT_TRUE(payload.local_count == 0);
    alloc_test_fail_after(-1);
    as_delete(params);
  }

  alloc_test_fail_after(3);
  {
    OUTPUT_t out = {0};
    out.maxsize = 1;
    out.bytecode = malloc(1);
    out.nextbyte = out.bytecode;
    IR_Unit *u = ir_create_unit();
    ASSERT_NOT_NULL(u);
    ir_emit(u, (IR_Inst){.op=IR_OP_PUSH_INT,.imm=1});
    CompilerDiagnostic diag;
    compiler_diag_init(&diag);
    int8_t rc = emit_bytecode_diag(u, 0, 0, &out, &diag);
    ASSERT_TRUE(rc != ERR_NOERROR);
    compiler_diag_reset(&diag);
    ir_destroy_unit(u);
    free(out.bytecode);
  }
  alloc_test_fail_after(-1);
}
