// Compiler pipeline - inputs source, outputs bytecode

// Licensed under the MIT License - see LICENSE file for details.
#include "compiler/compiler_pipeline.h"

#include "compiler/compiler_context.h"
#include "compiler/compdiag.h"
#include "error.h"
#include "compiler/lower.h"
#include "parser.h"
#include <stdlib.h>

static const char *default_source_name(const ParseInput *input) {
  return input && input->source_name ? input->source_name : "<memory>";
}

static ParseInput make_default_parse_input(const char *source, size_t len) {
  ParseInput input = {source, len, "<memory>"};
  return input;
}

static int8_t compile_parse_input_to_bytecode_with_params(const ParseInput *input,
                                                          const char **params, size_t param_count,
                                                          OUTPUT_t **out, char **errdetail) {
  CompilerContext ctx;
  int8_t rc = ERR_NOERROR;

  if (!input || !input->data || !out) {
    return ERR_COMP_SYNTAX;
  }

  *out = NULL;
  compiler_context_init(&ctx, input->data, input->len);

  if (compiler_context_prepare_bytecode_output(&ctx, 1024) != 0) {
    rc = ERR_COMP_SYNTAX;
    goto done;
  }

  ctx.sem_ctx = sem_create_ctx();
  if (!ctx.sem_ctx) {
    compdiag_set_once(&rc, errdetail, ERR_COMP_UNKNOWN, "compile",
                      "semantic context allocation failed");
    goto done;
  }
  ParseInput parse_input = {ctx.source, ctx.source_len, default_source_name(input)};
  rc = parse_source(&parse_input, &ctx.ast_root, errdetail);
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

int8_t compile_source_to_bytecode_with_params(const char *source, size_t len,
                                              const char **params, size_t param_count,
                                              OUTPUT_t **out, char **errdetail) {
  ParseInput input = make_default_parse_input(source, len);
  return compile_parse_input_to_bytecode_with_params(&input, params, param_count, out, errdetail);
}

int8_t compile_source_to_bytecode(const char *source, size_t len, OUTPUT_t **out, char **errdetail) {
  return compile_source_to_bytecode_with_params(source, len, NULL, 0, out, errdetail);
}

int8_t compile_parse_input_to_bytecode(const ParseInput *input, OUTPUT_t **out, char **errdetail) {
  return compile_parse_input_to_bytecode_with_params(input, NULL, 0, out, errdetail);
}

#include <string.h>

static char *source_line(const char *source, size_t len, int line_number) {
  if (!source || line_number < 1) return NULL;

  size_t start = 0;
  for (int current_line = 1; current_line < line_number; current_line++) {
    while (start < len && source[start] != '\n' && source[start] != '\r') {
      start++;
    }
    if (start == len) return NULL;
    if (source[start] == '\r' && start + 1 < len && source[start + 1] == '\n') {
      start += 2;
    } else {
      start++;
    }
  }

  size_t line_len = 0;
  while (start + line_len < len && source[start + line_len] != '\n' &&
         source[start + line_len] != '\r') {
    line_len++;
  }
  char *line = malloc(line_len + 1);
  if (!line) return NULL;
  memcpy(line, source + start, line_len);
  line[line_len] = '\0';
  return line;
}

int8_t compile_parse_input_to_bytecode_diag(const ParseInput *input, OUTPUT_t **out, CompilerDiagnostic *out_diag) {
  char *errdetail = NULL;
  CompilerContext ctx;
  SCANNER_STATE_t parse_state = {0};
  int8_t rc = ERR_NOERROR;
  const char *source_name = default_source_name(input);

  if (out_diag) compiler_diag_reset(out_diag);
  if (out) *out = NULL;
  compiler_context_init(&ctx, input ? input->data : NULL, input ? input->len : 0);

  if (!input || !input->data || !out) {
    rc = ERR_COMP_SYNTAX;
    if (out_diag) compiler_diag_set(out_diag, rc, DIAG_PHASE_COMPILE, "compile: invalid source or output");
  } else if (compiler_context_prepare_bytecode_output(&ctx, 1024) != 0) {
    rc = ERR_COMP_SYNTAX;
    if (out_diag) compiler_diag_set(out_diag, rc, DIAG_PHASE_COMPILE, "compile: bytecode output allocation failed");
  } else {
    ctx.sem_ctx = sem_create_ctx();
    if (!ctx.sem_ctx) {
      rc = ERR_COMP_UNKNOWN;
      if (out_diag) compiler_diag_set(out_diag, rc, DIAG_PHASE_COMPILE,
                                      "compile: semantic context allocation failed");
    } else {
      ParseInput parse_input = {ctx.source, ctx.source_len, source_name};
      rc = parse_source_diag(&parse_input, &ctx.ast_root, &errdetail, &parse_state);
      if (rc != ERR_NOERROR) {
        if (out_diag) compiler_diag_set(out_diag, rc, DIAG_PHASE_PARSE, errdetail ? errdetail : "");
      }
    }
    if (rc == ERR_NOERROR) rc = sem_check_locals_diag(ctx.ast_root, &errdetail, out_diag, ctx.sem_ctx);
    if (rc == ERR_NOERROR && ctx.sem_ctx->count > UINT8_MAX) {
      compdiag_setf_once_diag(&rc, &errdetail, out_diag,
                              ERR_COMP_TOOMANYLOCALS, DIAG_PHASE_SEMANT,
                              "compile", "locals exceeds u8 max: %u",
                              ctx.sem_ctx->count);
    }
    if (rc == ERR_NOERROR) rc = lower_ast_to_ir_diag(ctx.ast_root, ctx.sem_ctx, &ctx.ir_unit, &errdetail, out_diag);
    if (rc == ERR_NOERROR) rc = ir_validate_diag(ctx.ir_unit, ctx.sem_ctx->count, &errdetail, out_diag);
    if (rc == ERR_NOERROR) {
      rc = emit_bytecode_diag(ctx.ir_unit, (uint8_t)ctx.sem_ctx->count, 0, ctx.bytecode_out, &errdetail, out_diag);
    }
    if (rc == ERR_NOERROR) {
      *out = ctx.bytecode_out;
      ctx.bytecode_out = NULL;
    }
  }

  if (out_diag && rc != ERR_NOERROR) {
    compiler_diag_set_source_name(out_diag, source_name);
    if (out_diag->phase == DIAG_PHASE_PARSE && parse_state.line > 0) {
      compiler_diag_set_location(out_diag, parse_state.line, parse_state.column, parse_state.span);
    } else {
      compiler_diag_set_location(out_diag, 1, 1, 1);
    }
    char *excerpt = source_line(input ? input->data : NULL,
                                input ? input->len : 0, out_diag->line);
    compiler_diag_set_excerpt(out_diag, excerpt ? excerpt : "");
    free(excerpt);
  }

  compdiag_reset_detail(&errdetail);
  free(parse_state.offending_token);
  compiler_context_destroy(&ctx);
  return rc;
}

int8_t compile_source_to_bytecode_diag(const char *source, size_t len, OUTPUT_t **out, CompilerDiagnostic *out_diag) {
  ParseInput input = make_default_parse_input(source, len);
  return compile_parse_input_to_bytecode_diag(&input, out, out_diag);
}
