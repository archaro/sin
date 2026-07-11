// This is the wrapper for the standalone compiler (parser.y and lexer.l)

// Licensed under the MIT License - see LICENSE file for details.

#include <stdio.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <limits.h>

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

typedef struct {
  const char *input_path;
  const char *output_path;
  int quiet;
  int verbose;
} ScompOptions;

static void scomp_log(const ScompOptions *opts, const char *fmt, ...) {
  if (opts && opts->quiet) return;
  va_list args;
  va_start(args, fmt);
  vfprintf(stderr, fmt, args);
  fflush(stderr);
  va_end(args);
}

static void print_usage(FILE *stream) {
  fprintf(stream,
          "Usage:\n"
          "  scomp <input file> <output file>\n"
          "  scomp -i <input file> -o <output file> [options]\n"
          "\n"
          "Options:\n"
          "  -h, --help                          Show this help text\n"
          "      --version                       Show version information\n"
          "  -i, --input <file>                  Read source from <file> ('-' for stdin)\n"
          "  -o, --output <file>                 Write bytecode to <file> ('-' for stdout)\n"
          "  -q, --quiet                         Suppress progress messages\n"
          "  -v, --verbose                       Print verbose progress messages\n");
}

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

  if (strcmp(path, "-") == 0) {
    size_t cap = 4096;
    size_t len = 0;
    buf = malloc(cap);
    if (!buf) return -1;
    for (;;) {
      if (len == cap) {
        if (cap > SIZE_MAX / 2) goto fail;
        size_t next_cap = cap * 2;
        char *next = realloc(buf, next_cap);
        if (!next) goto fail;
        buf = next;
        cap = next_cap;
      }
      bytes_read = fread(buf + len, 1, cap - len, stdin);
      len += bytes_read;
      if (bytes_read == 0) {
        if (ferror(stdin)) goto fail;
        break;
      }
    }
    *out_data = buf;
    *out_len = len;
    return 0;
  }

  in = fopen(path, "rb");
  if (!in) return -1;
  if (fseek(in, 0, SEEK_END) != 0) goto fail;
  file_len = ftell(in);
  if (file_len < 0 || file_len > INT_MAX) goto fail;
  if (fseek(in, 0, SEEK_SET) != 0) goto fail;

  buf = malloc((size_t)file_len);
  if (file_len > 0 && !buf) goto fail;
  bytes_read = fread(buf, sizeof(char), (size_t)file_len, in);
  if (bytes_read != (size_t)file_len) goto fail;

  fclose(in);
  *out_data = buf;
  *out_len = (size_t)file_len;
  return 0;

fail:
  if (in) fclose(in);
  if (buf) free(buf);
  return -1;
}

static int parse_options(int argc, char **argv, ScompOptions *opts) {
  enum { OPT_VERSION = 1000 };
  static const struct option long_options[] = {
      {"help", no_argument, NULL, 'h'},
      {"version", no_argument, NULL, OPT_VERSION},
      {"input", required_argument, NULL, 'i'},
      {"output", required_argument, NULL, 'o'},
      {"quiet", no_argument, NULL, 'q'},
      {"verbose", no_argument, NULL, 'v'},
      {NULL, 0, NULL, 0},
  };

  int opt;
  memset(opts, 0, sizeof(*opts));
  opterr = 0;
  optind = 1;
  while ((opt = getopt_long(argc, argv, "hi:o:qv", long_options, NULL)) != -1) {
    switch (opt) {
      case 'h': print_usage(stdout); return 1;
      case OPT_VERSION: printf("scomp %s\n", SINVERSION); return 1;
      case 'i': opts->input_path = optarg; break;
      case 'o': opts->output_path = optarg; break;
      case 'q': opts->quiet = 1; break;
      case 'v': opts->verbose++; break;
      default:
        fprintf(stderr, "Unknown option. Use --help for usage.\n");
        return -1;
    }
  }

  int positional_count = argc - optind;
  if (!opts->input_path && !opts->output_path && positional_count == 2) {
    opts->input_path = argv[optind];
    opts->output_path = argv[optind + 1];
  } else if (positional_count != 0) {
    fprintf(stderr, "Unexpected positional arguments. Use --help for usage.\n");
    return -1;
  }

  if (!opts->input_path || !opts->output_path) {
    print_usage(stderr);
    return -1;
  }
  return 0;
}

int main(int argc, char **argv) {
  char *source = NULL;
  size_t source_len = 0;
  CompilerDiagnostic diag;
  compiler_diag_init(&diag);
  int8_t result = ERR_NOERROR;
  OUTPUT_t *out = NULL;
  ScompOptions opts;
  init_errmsg();

  int parse_rc = parse_options(argc, argv, &opts);
  if (parse_rc > 0) return 0;
  if (parse_rc < 0) return 1;

  if (strcmp(opts.output_path, "-") == 0) opts.quiet = 1;
  scomp_log(&opts, "Sinistra compiler version %s\n", SINVERSION);

  if (load_file_buffer(opts.input_path, &source, &source_len) != 0) {
    result = ERR_COMP_SYNTAX;
    compiler_diag_set(&diag, result, DIAG_PHASE_IO, "input file IO failure");
    compiler_diag_set_source_name(&diag, opts.input_path);
    compiler_diag_set_location(&diag, 1, 1, 1);
    goto compile_error;
  }
  scomp_log(&opts, "Source loaded: %zu bytes.\n", source_len);

  scomp_log(&opts, "Compiling...\n");
  ParseInput input = {source, source_len, strcmp(opts.input_path, "-") == 0 ? "<stdin>" : opts.input_path};
  result = compile_parse_input_to_bytecode_diag(&input, &out, &diag);
  if (result != ERR_NOERROR) {
    goto compile_error;
  }

  size_t bytecode_len = (size_t)(out->nextbyte - out->bytecode);
  scomp_log(&opts, "Compilation completed: %zu bytes.\n", bytecode_len);
  FILE *output = strcmp(opts.output_path, "-") == 0 ? stdout : fopen(opts.output_path, "wb");
  if (!output) {
    logerr("Unable to open output file: %s\n", opts.output_path);
    result = ERR_COMP_UNKNOWN;
    goto cleanup;
  }
  if (fwrite(out->bytecode, 1, bytecode_len, output) != bytecode_len) {
    logerr("Unable to write output file: %s\n", opts.output_path);
    result = ERR_COMP_UNKNOWN;
  }
  if (output != stdout) fclose(output);
  goto cleanup;

compile_error:
  logerr("Error: (#%d) %s\n", result, errmsg[result]);
  print_compiler_diagnostic(&diag, source, source_len);
  logerr("Compilation failed.\n");

cleanup:
  compiler_diag_reset(&diag);
  if (out) {
    if (out->bytecode) {
      free(out->bytecode);
    }
    free(out);
  }
  if (source) {
    free(source);
  }

  return result == ERR_NOERROR ? 0 : 1;
}
