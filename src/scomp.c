// This is the wrapper for the standalone compiler (parser.y and lexer.l)

// Licensed under the MIT License - see LICENSE file for details.

#include <stdio.h>
#include <stdlib.h>

#include "version.h"
#include "config.h"
#include "error.h"
#include "compiler_pipeline.h"
#include "compdiag.h"
#include "memory.h"
#include "log.h"
#include "emitbc.h"

// Things which need to be known
CONFIG_t config;

static const char *diag_message(const CompilerDiagnostic *diag) {
  return diag && diag->message ? diag->message : "";
}

static void print_source_line(FILE *stream, const char *source, size_t source_len,
                              int line) {
  const char *line_start = source;
  const char *line_end = source;
  int current_line = 1;

  if (!stream || !source || source_len == 0 || line < 1) {
    fprintf(stream, "\n");
    return;
  }

  while (line_start < source + source_len && current_line < line) {
    if (*line_start == '\n') current_line++;
    line_start++;
  }

  line_end = line_start;
  while (line_end < source + source_len && *line_end != '\n' &&
         *line_end != '\r') {
    line_end++;
  }

  fprintf(stream, "%.*s\n", (int)(line_end - line_start), line_start);
}

static void print_caret_marker(FILE *stream, int column, int span) {
  int caret_column = column > 0 ? column : 1;
  int caret_span = span > 0 ? span : 1;

  if (!stream) return;
  for (int i = 1; i < caret_column; i++) fputc(' ', stream);
  fputc('^', stream);
  for (int i = 1; i < caret_span; i++) fputc('~', stream);
  fputc('\n', stream);
}

static void print_compiler_diagnostic(const CompilerDiagnostic *diag,
                                      const char *source, size_t source_len) {
  const char *stable_code = diag && diag->stable_code
                                ? diag->stable_code
                                : compiler_diag_stable_code(diag ? diag->code : 0,
                                                            diag ? diag->phase : DIAG_PHASE_NONE);
  const char *stage = diag ? compiler_diag_phase_name(diag->phase)
                           : compiler_diag_phase_name(DIAG_PHASE_NONE);
  const char *file = diag && diag->source_name ? diag->source_name : "<unknown>";
  int line = diag && diag->has_loc ? diag->line : 1;
  int column = diag && diag->has_loc ? diag->column : 1;
  int span = diag && diag->span > 0 ? diag->span : 1;

  logerr("Diagnostic %s\n", stable_code);
  logerr("  stage: %s\n", stage);
  logerr("  file: %s\n", file);
  logerr("  line: %d\n", line);
  logerr("  column: %d\n", column);
  logerr("  message: %s\n", diag_message(diag));
  logerr("  legacy: ERR_%d\n", diag ? diag->code : 0);
  logerr("  source:\n");
  logerr("    ");
  print_source_line(stderr, source, source_len, line);
  logerr("    ");
  print_caret_marker(stderr, column, span);
}

int load_file_buffer(const char *path, char **out_data, size_t *out_len) {
  FILE *in = NULL;
  long file_len = 0;
  char *buf = NULL;
  size_t bytes_read = 0;

  if (!path || !out_data || !out_len) return -1;

  *out_data = NULL;
  *out_len = 0;

  in = fopen(path, "r");
  if (!in) return -1;
  if (fseek(in, 0, SEEK_END) != 0) goto fail;
  file_len = ftell(in);
  if (file_len < 0 || file_len > INT_MAX) goto fail;
  if (fseek(in, 0, SEEK_SET) != 0) goto fail;

  buf = GROW_ARRAY(char, NULL, 0, (int)file_len);
  bytes_read = fread(buf, sizeof(char), (size_t)file_len, in);
  if (bytes_read != (size_t)file_len) goto fail;

  fclose(in);
  *out_data = buf;
  *out_len = (size_t)file_len;
  return 0;

fail:
  if (in) fclose(in);
  if (buf) FREE_ARRAY(char, buf, (int)file_len);
  return -1;
}

int main(int argc, char **argv) {
  char *source = NULL;
  size_t source_len = 0;
  CompilerDiagnostic diag;
  compiler_diag_init(&diag);
  uint8_t result = ERR_NOERROR;
  OUTPUT_t *out = NULL;
  init_errmsg();

  logmsg("Sinistra compiler version %s\n", SINVERSION);

  if (argc != 3) {
    logmsg("Syntax: scomp <input file> <output file>\n");
    return 1;
  }

  if (load_file_buffer(argv[1], &source, &source_len) != 0) {
    result = ERR_COMP_SYNTAX;
    compiler_diag_set(&diag, result, DIAG_PHASE_IO, "input file IO failure");
    compiler_diag_set_source_name(&diag, argv[1]);
    compiler_diag_set_location(&diag, 1, 1, 1);
    goto compile_error;
  }
  logmsg("Source loaded: %zu bytes.\n", source_len);

  logmsg("Compiling...\n");
  ParseInput input = {source, source_len, argv[1]};
  result = compile_parse_input_to_bytecode_diag(&input, &out, &diag);
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
  print_compiler_diagnostic(&diag, source, source_len);
  logerr("Compilation failed.\n");

cleanup:
  compiler_diag_reset(&diag);
  if (out) {
    if (out->bytecode) {
      FREE_ARRAY(unsigned char, out->bytecode, out->maxsize);
    }
    FREE_ARRAY(OUTPUT_t, out, 1);
  }
  if (source) {
    FREE_ARRAY(char, source, (int)source_len);
  }

  return result == ERR_NOERROR ? 0 : 1;
}
