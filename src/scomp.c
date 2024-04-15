// This is the wrapper for the standalone compiler (parser.y and lexer.l)

#include <stdio.h>
#include <stdlib.h>

#include "error.h"
#include "parser.h"
#include "memory.h"
#include "log.h"
#include "vm.h"

VM_t vm; // Not needed by standalone compiler, but needed for linking

int main(int argc, char **argv) {
  char *source;
  int sourcelen;
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

  out = GROW_ARRAY(OUTPUT_t, NULL, 0, sizeof(OUTPUT_t));
  out->maxsize = 1024;
  out->bytecode = GROW_ARRAY(unsigned char, NULL, 0, out->maxsize);
  out->nextbyte = out->bytecode;

  LOCAL_t local;
  local.count = 0;
  local.param_count = 0;
  logmsg("Parsing...\n");
  init_errmsg();
  bool result =  parse_source(source, sourcelen, out, &local);

  if (result) {
    logmsg("Compilation completed: %ld bytes.\n",
                                          out->nextbyte - out->bytecode);
    FILE *output;
    output = fopen(argv[2], "w");
    if (!output) {
      printf("Unable to open output file.");
      exit(1);
    } else {
      fwrite(out->bytecode, out->nextbyte - out->bytecode, 1, output);
      fclose(output);
    }
  } else {
    logerr("Error: (#%d) %s\n", local.errnum, errmsg[local.errnum]);
    logerr("Compilation failed.\n");
  }

  FREE_ARRAY(unsigned char, out->bytecode, out->maxsize);
  FREE_ARRAY(OUTPUT_t, out, sizeof(OUTPUT_t));
  FREE_ARRAY(char, source, sourcelen);
  for (int l = 0; l < local.count; l++) {
    free(local.id[l]);
  }
}
