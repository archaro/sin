// This is the wrapper for the standalone compiler (parser.y and lexer.l)

// Licensed under the MIT License - see LICENSE file for details.

#include <stdio.h>
#include <stdlib.h>
#include <limits.h>

#include "config.h"
#include "error.h"
#include "compiler_pipeline.h"
#include "memory.h"
#include "log.h"
#include "emitbc.h"

// Things which need to be known
CONFIG_t config;

int main(int argc, char **argv) {
  char *source = NULL;
  char *errdetail = NULL;
  int sourcelen = 0;
  uint8_t result = ERR_NOERROR;
  OUTPUT_t *out = NULL;
  init_errmsg();

  if (argc != 3) {
    printf("Syntax: scomp <input file> <output file>\n");
    return 1;
  }

  FILE *in = fopen(argv[1], "r");
  if (!in) {
    printf("Unable to open input file.");
    return 1;
  }

  if (fseek(in, 0, SEEK_END) != 0) {
    errdetail = GROW_ARRAY(char, NULL, 0, 128);
    snprintf(errdetail, 128, "Unable to seek to end of input file '%s'.", argv[1]);
    result = ERR_COMP_SYNTAX;
    fclose(in);
    goto compile_error;
  }
  long sourcefilelen = ftell(in);
  if (sourcefilelen < 0 || sourcefilelen > INT_MAX) {
    errdetail = GROW_ARRAY(char, NULL, 0, 160);
    snprintf(errdetail, 160,
             "Invalid input file length %ld for '%s' (must be 0..%d bytes).",
             sourcefilelen, argv[1], INT_MAX);
    result = ERR_COMP_SYNTAX;
    fclose(in);
    goto compile_error;
  }
  sourcelen = (int)sourcefilelen;
  if (fseek(in, 0, SEEK_SET) != 0) {
    errdetail = GROW_ARRAY(char, NULL, 0, 128);
    snprintf(errdetail, 128, "Unable to rewind input file '%s'.", argv[1]);
    result = ERR_COMP_SYNTAX;
    fclose(in);
    goto compile_error;
  }
  source = GROW_ARRAY(char, NULL, 0, sourcelen);
  size_t bytesread = fread(source, sizeof(char), sourcelen, in);
  if (bytesread != (size_t)sourcelen) {
    errdetail = GROW_ARRAY(char, NULL, 0, 192);
    snprintf(errdetail, 192,
             "Input load failed for '%s': expected %d bytes but read %zu.",
             argv[1], sourcelen, bytesread);
    result = ERR_COMP_SYNTAX;
    fclose(in);
    goto compile_error;
  }
  fclose(in);
  logmsg("Source loaded: %d bytes.\n", sourcelen);

  logmsg("Compiling...\n");
  result = compile_source_to_bytecode(source, (size_t)sourcelen, &out, &errdetail);
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
