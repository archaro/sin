// Compiler pipeline - inputs source, outputs bytecode

// Licensed under the MIT License - see LICENSE file for details.
#include "compiler_pipeline.h"

#include "compiler_context.h"
#include "compdiag.h"
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
  ParseInput input = {ctx.source, ctx.source_len, "<memory>"};
  rc = parse_source(&input, &ctx.ast_root, errdetail);
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

  uint32_t emitted_param_count = 0;
  for (uint32_t i = 0; i < ctx.sem_ctx->count; i++) {
    if (ctx.sem_ctx->locals[i].param) {
      emitted_param_count++;
    }
  }

  if (ctx.sem_ctx->count > UINT8_MAX) {
    compdiag_setf_once(&rc, errdetail, ERR_COMP_TOOMANYLOCALS, "compile",
                       "locals+params exceeds u8 max: %u", ctx.sem_ctx->count);
    goto done;
  }

  if (emitted_param_count > UINT8_MAX) {
    compdiag_setf_once(&rc, errdetail, ERR_COMP_TOOMANYPARAMS, "compile",
                       "param count exceeds u8 max: %u", emitted_param_count);
    goto done;
  }

  rc = emit_bytecode(ctx.ir_unit, (uint8_t)ctx.sem_ctx->count, (uint8_t)emitted_param_count, ctx.bytecode_out, errdetail);
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

int8_t compile_parse_input_to_bytecode(const ParseInput *input, OUTPUT_t **out, char **errdetail) {
  if (!input) return ERR_COMP_SYNTAX;
  return compile_source_to_bytecode(input->data, input->len, out, errdetail);
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

static char *first_source_line(const char *source, size_t len) {
  if (!source) return NULL;
  size_t line_len = 0;
  while (line_len < len && source[line_len] != '\n' && source[line_len] != '\r') {
    line_len++;
  }
  char *line = malloc(line_len + 1);
  if (!line) return NULL;
  memcpy(line, source, line_len);
  line[line_len] = '\0';
  return line;
}

int8_t compile_source_to_bytecode_diag(const char *source, size_t len, OUTPUT_t **out, CompilerDiagnostic *out_diag) {
  char *errdetail = NULL;
  CompilerContext ctx;
  SCANNER_STATE_t parse_state = {0};
  int8_t rc = ERR_NOERROR;

  if (out) *out = NULL;
  compiler_context_init(&ctx, source, len);

  if (!source || !out) {
    rc = ERR_COMP_SYNTAX;
    errdetail = strdup("compile: invalid source or output");
  } else if (compiler_context_prepare_bytecode_output(&ctx, 1024) != 0) {
    rc = ERR_COMP_SYNTAX;
    errdetail = strdup("compile: bytecode output allocation failed");
  } else {
    ctx.sem_ctx = sem_create_ctx();
    ParseInput input = {ctx.source, ctx.source_len, "<memory>"};
    rc = parse_source_diag(&input, &ctx.ast_root, &errdetail, &parse_state);
    if (rc == ERR_NOERROR) rc = sem_check_locals(ctx.ast_root, &errdetail, ctx.sem_ctx);
    if (rc == ERR_NOERROR) rc = lower_ast_to_ir(ctx.ast_root, ctx.sem_ctx, &ctx.ir_unit, &errdetail);
    if (rc == ERR_NOERROR) rc = ir_validate(ctx.ir_unit, ctx.sem_ctx->count, &errdetail);
    if (rc == ERR_NOERROR) {
      rc = emit_bytecode(ctx.ir_unit, (uint8_t)ctx.sem_ctx->count, 0, ctx.bytecode_out, &errdetail);
    }
    if (rc == ERR_NOERROR) {
      *out = ctx.bytecode_out;
      ctx.bytecode_out = NULL;
    }
  }

  if (out_diag) {
    compiler_diag_reset(out_diag);
    if (rc != ERR_NOERROR) {
      DiagPhase phase = rc == ERR_COMP_SYNTAX || rc == ERR_COMP_UNKNOWNCHAR ? DIAG_PHASE_PARSE : phase_from_detail(errdetail);
      compiler_diag_set(out_diag, rc, phase, errdetail ? errdetail : "");
      compiler_diag_set_source_name(out_diag, "<memory>");
      if (phase == DIAG_PHASE_PARSE && parse_state.line > 0) {
        compiler_diag_set_location(out_diag, parse_state.line, parse_state.column, parse_state.span);
      } else {
        compiler_diag_set_location(out_diag, 1, 1, 1);
      }
      char *excerpt = first_source_line(source, len);
      compiler_diag_set_excerpt(out_diag, excerpt ? excerpt : "");
      free(excerpt);
    }
  }

  free(errdetail);
  free(parse_state.offending_token);
  compiler_context_destroy(&ctx);
  return rc;
}
