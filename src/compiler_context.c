// Compiler context - a handy package to hold compiler state

// Licensed under the MIT License - see LICENSE file for details.
#include "compiler_context.h"

#include <string.h>

#include "memory.h"

void compiler_context_init(CompilerContext *ctx, const char *source, size_t source_len) {
  memset(ctx, 0, sizeof(*ctx));
  ctx->source = source;
  ctx->source_len = source_len;
}

void compiler_context_reset(CompilerContext *ctx) {
  if (!ctx) {
    return;
  }

  if (ctx->ast_root) {
    as_delete(ctx->ast_root);
    ctx->ast_root = NULL;
  }
  if (ctx->sem_ctx) {
    sem_delete_ctx(ctx->sem_ctx);
    ctx->sem_ctx = NULL;
  }
  if (ctx->ir_unit) {
    ir_destroy_unit(ctx->ir_unit);
    ctx->ir_unit = NULL;
  }
  if (ctx->bytecode_out) {
    if (ctx->bytecode_out->bytecode) {
      FREE_ARRAY(unsigned char, ctx->bytecode_out->bytecode, ctx->bytecode_out->maxsize);
    }
    FREE_ARRAY(OUTPUT_t, ctx->bytecode_out, 1);
    ctx->bytecode_out = NULL;
  }
  if (ctx->diagnostic) {
    FREE_ARRAY(char, ctx->diagnostic, 1);
    ctx->diagnostic = NULL;
  }
}

void compiler_context_destroy(CompilerContext *ctx) {
  compiler_context_reset(ctx);
  if (ctx) {
    memset(ctx, 0, sizeof(*ctx));
  }
}

int8_t compiler_context_prepare_bytecode_output(CompilerContext *ctx, size_t initial_size) {
  if (!ctx || initial_size == 0) {
    return -1;
  }

  if (!alloc_grow_array((void **)&ctx->bytecode_out, 0, 1, sizeof(OUTPUT_t))) return -1;
  ctx->bytecode_out->maxsize = initial_size;
  if (!alloc_grow_array((void **)&ctx->bytecode_out->bytecode, 0, ctx->bytecode_out->maxsize, sizeof(unsigned char))) return -1;
  ctx->bytecode_out->nextbyte = ctx->bytecode_out->bytecode;
  return 0;
}
