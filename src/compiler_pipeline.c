#include "compiler_pipeline.h"

#include "compiler_context.h"
#include "error.h"
#include "lower.h"
#include "parser.h"
#include <stdlib.h>

int8_t compile_source_to_bytecode_with_params(const char *source, size_t len,
                                              const char **params, size_t param_count,
                                              OUTPUT_t **out, char **errdetail) {
  CompilerContext ctx;
  int8_t rc = ERR_NOERROR;

  if (!source || !out) {
    return ERR_COMP_SYNTAX;
  }

  *out = NULL;
  compiler_context_init(&ctx, source, len);

  if (compiler_context_prepare_bytecode_output(&ctx, 1024) != 0) {
    rc = ERR_COMP_SYNTAX;
    goto done;
  }

  ctx.sem_ctx = sem_create_ctx();
  rc = parse_source((char *)ctx.source, (int)ctx.source_len, &ctx.ast_root, errdetail);
  if (rc != ERR_NOERROR) {
    goto done;
  }

  sem_seed_params(ctx.sem_ctx, params, param_count);

  rc = sem_check_locals(ctx.ast_root, errdetail, ctx.sem_ctx);
  if (rc != ERR_NOERROR) {
    goto done;
  }

  rc = lower_ast_to_ir(ctx.ast_root, ctx.sem_ctx, &ctx.ir_unit, errdetail);
  if (rc != ERR_NOERROR) {
    goto done;
  }

  rc = ir_validate(ctx.ir_unit, ctx.sem_ctx->count, errdetail);
  if (rc != ERR_NOERROR) {
    goto done;
  }

  uint8_t emitted_param_count = 0;
  for (uint32_t i = 0; i < ctx.sem_ctx->count; i++) {
    if (ctx.sem_ctx->locals[i].param) {
      emitted_param_count++;
    }
  }

  rc = emit_bytecode(ctx.ir_unit, (uint8_t)ctx.sem_ctx->count, emitted_param_count, ctx.bytecode_out, errdetail);
  if (rc != ERR_NOERROR) {
    goto done;
  }

  *out = ctx.bytecode_out;
  ctx.bytecode_out = NULL;

done:
  compiler_context_destroy(&ctx);
  return rc;
}

int8_t compile_source_to_bytecode(const char *source, size_t len, OUTPUT_t **out, char **errdetail) {
  return compile_source_to_bytecode_with_params(source, len, NULL, 0, out, errdetail);
}

#include <string.h>

static DiagPhase phase_from_detail(const char *d){
  if(!d) return DIAG_PHASE_NONE;
  if(strncmp(d,"semant:",7)==0) return DIAG_PHASE_SEMANT;
  if(strncmp(d,"lower:",6)==0) return DIAG_PHASE_LOWER;
  if(strncmp(d,"ir:",3)==0) return DIAG_PHASE_IR_VALIDATE;
  if(strncmp(d,"emitbc:",7)==0) return DIAG_PHASE_EMITBC;
  if(strstr(d,"syntax error")||strcmp(d,"^")==0) return DIAG_PHASE_PARSE;
  return DIAG_PHASE_NONE;
}

int8_t compile_source_to_bytecode_diag(const char *source, size_t len, OUTPUT_t **out, CompilerDiagnostic *out_diag){
 char *errdetail=NULL;
 int8_t rc = compile_source_to_bytecode(source,len,out,&errdetail);
 if(out_diag){ compiler_diag_reset(out_diag); if(rc!=ERR_NOERROR){ compiler_diag_set(out_diag,rc,phase_from_detail(errdetail),errdetail?errdetail:""); }}
 if(errdetail) free(errdetail);
 return rc;
}
