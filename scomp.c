// This is the wrapper for the standalone compiler (parser.y and lexer.l)

#include <stdio.h>
#include <stdlib.h>

#include "parser.h"
#include "memory.h"
#include "log.h"

int main(int argc, char **argv) {
  FILE *output;
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
  output = fopen(argv[2], "w");
  if (!output) {
    printf("Unable to open output file.");
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
  parse_source(source, sourcelen, out);

  fwrite(out->bytecode, out->nextbyte - out->bytecode, 1, output);
  fclose(output);
  FREE_ARRAY(unsigned char, out->bytecode, out->maxsize);
  FREE_ARRAY(OUTPUT_t, out, sizeof(OUTPUT_t));
  FREE_ARRAY(char, source, sourcelen);
}
