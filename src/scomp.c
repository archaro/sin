// This is the wrapper for the standalone compiler (parser.y and lexer.l)

// Licensed under the MIT License - see LICENSE file for details.

#include <stdio.h>
#include <stdlib.h>

#include "config.h"
#include "error.h"
#include "parser.h"
#include "absyn.h"
#include "semant.h"
#include "lower.h"
#include "ir.h"
#include "memory.h"
#include "log.h"
#include "emitbc.h"

// Things which need to be known
CONFIG_t config;

int main(int argc, char **argv) {
  char *source = NULL;
  char *errdetail = NULL;
  int sourcelen;
  uint8_t result = ERR_NOERROR;
  AS_NODE *absyn = NULL;
  SEM_CTX *ctx = NULL;
  IR_Unit *ir = NULL;
  OUTPUT_t *out = NULL;

  if (argc != 3) {
    printf("Syntax: maketest <input file> <output file>\n");
    return 1;
  }

  FILE *in = fopen(argv[1], "r");
  if (!in) {
    printf("Unable to open input file.");
    return 1;
  }

  fseek(in, 0, SEEK_END);
  sourcelen = ftell(in);
  fseek(in, 0, SEEK_SET);
  source = GROW_ARRAY(char, NULL, 0, sourcelen);
  fread(source, sourcelen, sizeof(char), in);
  fclose(in);
  logmsg("Source loaded: %d bytes.\n", sourcelen);

  out = GROW_ARRAY(OUTPUT_t, NULL, 0, 1);
  out->maxsize = 1024;
  out->bytecode = GROW_ARRAY(unsigned char, NULL, 0, out->maxsize);
  out->nextbyte = out->bytecode;

  logmsg("Parsing...\n");
  init_errmsg();
  ctx = sem_create_ctx();

  result = parse_source(source, sourcelen, &absyn, &errdetail);
  if (result != ERR_NOERROR) {
    goto compile_error;
  }

#ifdef DEBUG
  logmsg("Walking the abstract syntax tree...\n");
  as_walk(absyn);
#endif

  result = sem_check_locals(absyn, &errdetail, ctx);
  if (result != ERR_NOERROR) {
    goto compile_error;
  }

#ifdef DEBUG
  logmsg("Local table:\n");
  for (int i = 0; i < ctx->count; i++) {
    logmsg("Index %d: %s%s\n", ctx->locals[i].index, ctx->locals[i].name,
           ctx->locals[i].param ? " (param)" : "");
  }
#endif

  result = lower_ast_to_ir(absyn, ctx, &ir, &errdetail);
  if (result != ERR_NOERROR) {
    goto compile_error;
  }

  result = ir_validate(ir, ctx->count, &errdetail);
  if (result != ERR_NOERROR) {
    goto compile_error;
  }

  uint8_t param_count = 0;
  for (uint32_t i = 0; i < ctx->count; i++) {
    if (ctx->locals[i].param) {
      param_count++;
    }
  }

  result = emit_bytecode(ir, (uint8_t)ctx->count, param_count, out, &errdetail);
  if (result != ERR_NOERROR) {
    goto compile_error;
  }

  logmsg("Compilation completed: %ld bytes.\n", out->nextbyte - out->bytecode);
  FILE *output = fopen(argv[2], "w");
  if (!output) {
    printf("Unable to open output file.");
    goto cleanup;
  }
  fwrite(out->bytecode, out->nextbyte - out->bytecode, 1, output);
  fclose(output);
  goto cleanup;

compile_error:
  logerr("Error: (#%d) %s\n", result, errmsg[result]);
  logerr("Detail: %s\n", errdetail);
  logerr("Compilation failed.\n");

cleanup:
  if (errdetail) {
    FREE_ARRAY(char, errdetail, 1);
  }
  if (absyn) {
    as_delete(absyn);
  }
  if (ir) {
    ir_destroy_unit(ir);
  }
  if (ctx) {
    sem_delete_ctx(ctx);
  }
  if (out) {
    if (out->bytecode) {
      FREE_ARRAY(unsigned char, out->bytecode, out->maxsize);
    }
    FREE_ARRAY(OUTPUT_t, out, 1);
  }
  if (source) {
    FREE_ARRAY(char, source, sourcelen);
  }

  return result == ERR_NOERROR ? 0 : 1;
}
