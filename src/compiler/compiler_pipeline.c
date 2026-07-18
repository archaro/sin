// Compiler pipeline - inputs source, outputs bytecode

// Licensed under the MIT License - see LICENSE file for details.
#include "compiler/compiler_pipeline.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include "compiler/compiler_context.h"
#include "compiler/compdiag.h"
#include "compiler/lower.h"
#include "error.h"
#include "parser.h"

static const char *default_source_name(const ParseInput *input) {
  return input && input->source_name ? input->source_name : "<memory>";
}

static ParseInput make_default_parse_input(const char *source, size_t len) {
  ParseInput input = {source, len, "<memory>"};
  return input;
}

static char *source_line(const char *source, size_t len, int line_number);

static void set_pipeline_startup_diag(CompilerDiagnostic *diag, int8_t code,
                                      DiagPhase phase, const char *message) {
  if (diag) compiler_diag_set(diag, code, phase, message);
}

/*
 * All public entry points use this runner.  Passing a NULL diagnostic keeps
 * the legacy API's detail strings, while the stage implementations still
 * share the exact same parse, semantic, lower, validate, and emit sequence.
 */
static int8_t compile_pipeline_run(const ParseInput *input,
                                   const char **params, size_t param_count,
                                   OUTPUT_t **out, char **errdetail,
                                   CompilerDiagnostic *diag) {
  CompilerContext ctx;
  SCANNER_STATE_t parse_state = {0};
  char *local_errdetail = NULL;
  char **pipeline_errdetail = errdetail ? errdetail : &local_errdetail;
  int8_t rc = ERR_NOERROR;
  const char *source_name = default_source_name(input);

  if (out) *out = NULL;
  compdiag_reset_detail(pipeline_errdetail);
  if (diag) compiler_diag_reset(diag);
  compiler_context_init(&ctx, input ? input->data : NULL,
                        input ? input->len : 0);

  if (!input || !input->data || !out) {
    rc = ERR_COMP_SYNTAX;
    set_pipeline_startup_diag(diag, rc, DIAG_PHASE_COMPILE,
                              "compile: invalid source or output");
    goto done;
  }

  if (compiler_context_prepare_bytecode_output(&ctx, 1024) != 0) {
    rc = ERR_COMP_SYNTAX;
    set_pipeline_startup_diag(diag, rc, DIAG_PHASE_COMPILE,
                              "compile: bytecode output allocation failed");
    goto done;
  }

  ctx.sem_ctx = sem_create_ctx();
  if (!ctx.sem_ctx) {
    rc = ERR_COMP_UNKNOWN;
    if (diag) {
      set_pipeline_startup_diag(diag, rc, DIAG_PHASE_COMPILE,
                                "compile: semantic context allocation failed");
    } else {
      compdiag_set_once(&rc, pipeline_errdetail, ERR_COMP_UNKNOWN, "compile",
                        "semantic context allocation failed");
    }
    goto done;
  }

  ParseInput parse_input = {ctx.source, ctx.source_len, source_name};
  rc = parse_source_compiler_diag(&parse_input, &ctx.ast_root,
                                  pipeline_errdetail,
                                  diag, &parse_state);
  if (rc != ERR_NOERROR) goto done;

  sem_seed_params(ctx.sem_ctx, params, param_count);

  rc = sem_check_locals_diag(ctx.ast_root, pipeline_errdetail, diag,
                             ctx.sem_ctx);
  if (rc != ERR_NOERROR) goto done;

  rc = lower_ast_to_ir_diag(ctx.ast_root, ctx.sem_ctx, &ctx.ir_unit,
                            pipeline_errdetail, diag);
  if (rc != ERR_NOERROR) goto done;

  rc = ir_validate_diag(ctx.ir_unit, ctx.sem_ctx->count, pipeline_errdetail,
                        diag);
  if (rc != ERR_NOERROR) goto done;

  uint32_t emitted_param_count = 0;
  for (uint32_t i = 0; i < ctx.sem_ctx->count; i++) {
    if (ctx.sem_ctx->locals[i].param) emitted_param_count++;
  }

  if (ctx.sem_ctx->count > UINT8_MAX) {
    compdiag_setf_once_diag(&rc, pipeline_errdetail, diag,
                            ERR_COMP_TOOMANYLOCALS, DIAG_PHASE_SEMANT,
                            "compile", "locals+params exceeds u8 max: %u",
                            ctx.sem_ctx->count);
    goto done;
  }

  if (emitted_param_count > UINT8_MAX) {
    compdiag_setf_once_diag(&rc, pipeline_errdetail, diag,
                            ERR_COMP_TOOMANYPARAMS, DIAG_PHASE_SEMANT,
                            "compile", "param count exceeds u8 max: %u",
                            emitted_param_count);
    goto done;
  }

  rc = emit_bytecode_diag(ctx.ir_unit, (uint8_t)ctx.sem_ctx->count,
                          (uint8_t)emitted_param_count, ctx.bytecode_out,
                          pipeline_errdetail, diag);
  if (rc != ERR_NOERROR) goto done;

  *out = ctx.bytecode_out;
  ctx.bytecode_out = NULL;

done:
  if (diag && rc != ERR_NOERROR) {
    compiler_diag_set_source_name(diag, source_name);
    if (diag->phase == DIAG_PHASE_PARSE && parse_state.line > 0) {
      compiler_diag_set_location(diag, parse_state.line, parse_state.column,
                                 parse_state.span);
    } else {
      compiler_diag_set_location(diag, 1, 1, 1);
    }

    size_t source_len = input ? input->len : 0;
    const char *source = input ? input->data : NULL;
    char *excerpt = source_line(source, source_len, diag->line);
    compiler_diag_set_excerpt(diag, excerpt ? excerpt : "");
    free(excerpt);
  }

  free(parse_state.offending_token);
  compdiag_reset_detail(&local_errdetail);
  compiler_context_destroy(&ctx);
  return rc;
}

int8_t compile_source_to_bytecode_with_params(const char *source, size_t len,
                                              const char **params,
                                              size_t param_count,
                                              OUTPUT_t **out,
                                              char **errdetail) {
  ParseInput input = make_default_parse_input(source, len);
  return compile_pipeline_run(&input, params, param_count, out, errdetail,
                              NULL);
}

int8_t compile_source_to_bytecode(const char *source, size_t len,
                                  OUTPUT_t **out, char **errdetail) {
  return compile_source_to_bytecode_with_params(source, len, NULL, 0, out,
                                                 errdetail);
}

int8_t compile_parse_input_to_bytecode(const ParseInput *input, OUTPUT_t **out,
                                       char **errdetail) {
  return compile_pipeline_run(input, NULL, 0, out, errdetail, NULL);
}

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

int8_t compile_parse_input_to_bytecode_diag(const ParseInput *input,
                                            OUTPUT_t **out,
                                            CompilerDiagnostic *out_diag) {
  return compile_pipeline_run(input, NULL, 0, out, NULL, out_diag);
}

int8_t compile_source_to_bytecode_diag(const char *source, size_t len,
                                       OUTPUT_t **out,
                                       CompilerDiagnostic *out_diag) {
  ParseInput input = make_default_parse_input(source, len);
  return compile_parse_input_to_bytecode_diag(&input, out, out_diag);
}
