#include <stdlib.h>
#include <string.h>

#include "compiler_context.h"
#include "error.h"
#include "ir.h"
#include "lower.h"
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
}
