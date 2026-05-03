// This is the wrapper for the standalone compiler (parser.y and lexer.l)

// Licensed under the MIT License - see LICENSE file for details.

#include <stdio.h>
#include <stdlib.h>

#include "config.h"
#include "error.h"
#include "parser.h"
#include "absyn.h"
#include "semant.h"
#include "memory.h"
#include "log.h"

// Things which need to be known
CONFIG_t config;

int main(int argc, char **argv) {
  char *source, *errdetail;
  int sourcelen;
  AS_NODE *absyn;
  SEM_CTX *ctx;
  OUTPUT_t *out;

  if (argc != 3) {
    printf("Syntax: maketest <input file> <output file>\n");
    exit(1);
  }
  FILE *in = fopen(argv[1], "r");
  if (!in) {
    printf("Unable to open input file.");
    exit(1);
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
  uint8_t result = parse_source(source, sourcelen, &absyn, &errdetail);
// DEBUG: Just delete the tree at this point.
//        Eventually we should do something with it first.
  if (result == ERR_NOERROR) {
    logmsg("Walking the abstract syntax tree...\n");
    as_walk(absyn);
    result = sem_check_locals(absyn, &errdetail, ctx);
    // Output the local table
    logmsg("Local table:\n");
    for (int i = 0; i < ctx->count; i++) {
      logmsg("Index %d: %s%s\n", ctx->locals[i].index, ctx->locals[i].name, ctx->locals[i].param?" (param)":"");
    }
    if (result != ERR_NOERROR) {
      logerr("Error: (#%d) %s\n", result, errmsg[result]);
      logerr("Detail: %s\n", errdetail);
      logerr("Compilation failed.\n");
    }
// FIXME: WE HAVEN'T COMPILED THE BYTECODE YET! ONLY THE ABSTRACT SYNTAX!
//    logmsg("Compilation completed: %ld bytes.\n",
//                                          out->nextbyte - out->bytecode);
//    FILE *output;
//    output = fopen(argv[2], "w");
//    if (!output) {
//      printf("Unable to open output file.");
//      exit(1);
//    } else {
//      fwrite(out->bytecode, out->nextbyte - out->bytecode, 1, output);
//      fclose(output);
//    }
  } else {
    logerr("Error: (#%d) %s\n", result, errmsg[result]);
    logerr("Detail: %s\n", errdetail);
    logerr("Compilation failed.\n");
  }

  if (errdetail) {
    FREE_ARRAY(char, errdetail, 1);
  }
  as_delete(absyn);
  sem_delete_ctx(ctx);
  FREE_ARRAY(unsigned char, out->bytecode, out->maxsize);
  FREE_ARRAY(OUTPUT_t, out, 1);
  FREE_ARRAY(char, source, sourcelen);
}
