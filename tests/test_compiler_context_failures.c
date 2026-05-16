#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "compiler_context.h"
#include "error.h"
#include "ir.h"
#include "lower.h"
#include "memory.h"
#include "parser.h"
#include "semant.h"
#include "test_assert.h"

static void test_context_parse_failure_cleanup(void) {
  CompilerContext ctx;
  char *errdetail = NULL;
  compiler_context_init(&ctx, "^;", 2);
  ASSERT_EQ_INT(0, compiler_context_prepare_bytecode_output(&ctx, 64));
  ctx.sem_ctx = sem_create_ctx();

  int8_t rc = parse_source((char *)ctx.source, (int)ctx.source_len, &ctx.ast_root, &errdetail);
  ASSERT_TRUE(rc != ERR_NOERROR);
  ASSERT_NOT_NULL(errdetail);

  free(errdetail);
  compiler_context_destroy(&ctx);
}

static void test_context_semant_failure_cleanup(void) {
  CompilerContext ctx;
  char *errdetail = NULL;
  const char *src = "@x;";
  compiler_context_init(&ctx, src, strlen(src));
  ASSERT_EQ_INT(0, compiler_context_prepare_bytecode_output(&ctx, 64));
  ctx.sem_ctx = sem_create_ctx();

  ASSERT_EQ_INT(ERR_NOERROR,
                parse_source((char *)ctx.source, (int)ctx.source_len, &ctx.ast_root, &errdetail));
  int8_t rc = sem_check_locals(ctx.ast_root, &errdetail, ctx.sem_ctx);
  ASSERT_TRUE(rc != ERR_NOERROR);
  ASSERT_NOT_NULL(errdetail);

  free(errdetail);
  compiler_context_destroy(&ctx);
}

static void test_context_lower_failure_cleanup(void) {
  CompilerContext ctx;
  char *errdetail = NULL;
  const char *src = "@x;";
  compiler_context_init(&ctx, src, strlen(src));
  ASSERT_EQ_INT(0, compiler_context_prepare_bytecode_output(&ctx, 64));
  ctx.sem_ctx = sem_create_ctx();

  ASSERT_EQ_INT(ERR_NOERROR,
                parse_source((char *)ctx.source, (int)ctx.source_len, &ctx.ast_root, &errdetail));

  int8_t rc = lower_ast_to_ir(ctx.ast_root, ctx.sem_ctx, &ctx.ir_unit, &errdetail);
  ASSERT_TRUE(rc != ERR_NOERROR);
  ASSERT_NOT_NULL(errdetail);

  free(errdetail);
  compiler_context_destroy(&ctx);
}

static void test_context_emit_failure_cleanup(void) {
  CompilerContext ctx;
  char *errdetail = NULL;
  compiler_context_init(&ctx, NULL, 0);
  ASSERT_EQ_INT(0, compiler_context_prepare_bytecode_output(&ctx, 64));

  ctx.ir_unit = ir_create_unit();
  ASSERT_NOT_NULL(ctx.ir_unit);
  ir_emit(ctx.ir_unit, (IR_Inst){.op = IR_OP_JUMP, .a = 999});
  ir_emit(ctx.ir_unit, (IR_Inst){.op = IR_OP_HALT});

  int8_t rc = emit_bytecode(ctx.ir_unit, 0, 0, ctx.bytecode_out, &errdetail);
  ASSERT_TRUE(rc != ERR_NOERROR);
  ASSERT_NOT_NULL(errdetail);

  free(errdetail);
  compiler_context_destroy(&ctx);
}

void test_compiler_context_failures(void) {
  test_context_parse_failure_cleanup();
  test_context_semant_failure_cleanup();
  test_context_lower_failure_cleanup();
  test_context_emit_failure_cleanup();

  alloc_test_fail_after(-1);
  {
    AS_NODE *list = as_new_stmtlist_node();
    ASSERT_NOT_NULL(list);
    for (int i = 0; i < 8; i++) {
      char buf[8];
      snprintf(buf, sizeof(buf), "%d", i);
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

  alloc_test_fail_after(3);
  {
    OUTPUT_t out = {0};
    out.maxsize = 1;
    out.bytecode = malloc(1);
    out.nextbyte = out.bytecode;
    IR_Unit *u = ir_create_unit();
    ASSERT_NOT_NULL(u);
    ir_emit(u, (IR_Inst){.op=IR_OP_PUSH_INT,.imm=1});
    char *err = NULL;
    int8_t rc = emit_bytecode(u, 0, 0, &out, &err);
    ASSERT_TRUE(rc != ERR_NOERROR);
    if (err) free(err);
    ir_destroy_unit(u);
    free(out.bytecode);
  }
  alloc_test_fail_after(-1);
}
