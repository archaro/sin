// This is the wrapper for the standalone compiler (parser.y and lexer.l)

// Licensed under the MIT License - see LICENSE file for details.

#include <stdio.h>
#include <stdlib.h>

#include "config.h"
#include "error.h"
#include "parser.h"
#include "absyn.h"
#include "memory.h"
#include "log.h"

// Things which need to be known
CONFIG_t config;

int main(int argc, char **argv) {
  char *source;
  int sourcelen;
  AS_NODE *absyn;
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
  uint8_t result = parse_source(source, sourcelen, &absyn);
// DEBUG: Just delete the tree at this point.
//        Eventually we should do something with it first.
  if (result == 0) {
    as_delete(absyn);
    FREE_ARRAY(AS_NODE, absyn, 1);
  }
// FIXME: WE HAVEN'T COMPILED THE BYTECODE YET! ONLY THE ABSTRACT SYNTAX!
//  if (result == 0) {
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
//  } else {
//    logerr("Error: (#%d) %s\n", result, errmsg[result]);
//    logerr("Compilation failed.\n");
//  }

  FREE_ARRAY(unsigned char, out->bytecode, out->maxsize);
  FREE_ARRAY(OUTPUT_t, out, 1);
  FREE_ARRAY(char, source, sourcelen);
// FIXME: No longer relevant.  Rework for abstract syntax
//  for (int l = 0; l < local.count; l++) {
//    free(local.id[l]);
//  }
}
