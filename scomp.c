// This is the wrapper for the standalone compiler (parser.y and lexer.l)

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/stat.h>

#include "parser.h"
#include "memory.h"
#include "log.h"
#include "Intern.h"

int main(int argc, char **argv) {
  char *source;
  int sourcelen;
  OUTPUT_t *out;

  if (argc != 4) {
    printf("Syntax: maketest <strings file> <input file> <output file>\n");
    exit(1);
  }
  // Here, check that the strings file exists.
  // If it does, deserialise it into the interning database.
  // If it doesn't, no matter.
  Intern intern;
  struct stat buffer;
  if (stat(argv[1], &buffer) == -1) {
    if (errno != 2) {
      printf("Unable to open strings file, error %d\n", errno);
      exit(1);
    } else {
       printf("Strings file does not exist.  Creating new interning database.\n");
    }
  } else {
    // We have a strings database, so load it up.
    printf("Loading interned strings database...\n");
    intern.unserialise(argv[1]);
  }
  // Input source file
  FILE *in = fopen(argv[2], "r");
  if (!in) {
    printf("Unable to open input file.\n");
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
  bool result =  parse_source(source, sourcelen, out);

  if (result) {
    // Output object code
    FILE *output;
    output = fopen(argv[3], "w");
    if (!output) {
      printf("Unable to open output file.");
      exit(1);
    } else {
      fwrite(out->bytecode, out->nextbyte - out->bytecode, 1, output);
      fclose(output);
      intern.serialise(argv[1]);
    }
  }

  FREE_ARRAY(unsigned char, out->bytecode, out->maxsize);
  FREE_ARRAY(OUTPUT_t, out, sizeof(OUTPUT_t));
  FREE_ARRAY(char, source, sourcelen);
}
