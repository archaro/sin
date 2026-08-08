// This is the wrapper for the standalone compiler grammar and lexer.

// Licensed under the MIT License - see LICENSE file for details.

#include <stdio.h>
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <limits.h>

#include "version.h"
#include "cli_io.h"
#include "config.h"
#include "error.h"
#include "compiler/compiler_pipeline.h"
#include "compiler/compdiag.h"
#include "memory.h"
#include "log.h"
#include "compiler/emitbc.h"
#include "compiler/absyn.h"

// Things which need to be known
CONFIG_t config;

typedef struct {
  const char *input_path;
  const char *output_path;
} ScompOptions;

typedef enum {
  SCOMP_PARSE_ERROR = -1,
  SCOMP_PARSE_OK = 0,
  SCOMP_PARSE_EXIT_SUCCESS = 1
} ScompParseResult;

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

static void usage_error(const char *message) {
  logerr("scomp: %s\n", message);
  logerr("Try 'scomp --help' for more information.\n");
}

static void print_source_line(FILE *stream, const char *source, size_t source_len,
                              int line) {
  const char *line_start = source;
  const char *line_end = source;
  int current_line = 1;

  /* A missing stream means that the caller requested no diagnostic output. */
  if (!stream) return;
  if (!source || source_len == 0 || line < 1) {
    fputc('\n', stream);
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

  fwrite(line_start, 1, (size_t)(line_end - line_start), stream);
  fputc('\n', stream);
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
  logerr("  errno: ERR_%d\n", diag ? diag->code : 0);
  logerr("  source:\n");
  logerr("    ");
  print_source_line(stderr, source, source_len, line);
  logerr("    ");
  print_caret_marker(stderr, column, span);
}

static ScompParseResult parse_options(int argc, char **argv, ScompOptions *opts) {
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
      case 'h': print_usage(stdout); return SCOMP_PARSE_EXIT_SUCCESS;
      case OPT_VERSION: printf("scomp %s\n", SINVERSION); return SCOMP_PARSE_EXIT_SUCCESS;
      case 'i': opts->input_path = optarg; break;
      case 'o': opts->output_path = optarg; break;
      case 'q': log_set_level(LOG_LEVEL_QUIET); break;
      case 'v': log_set_level(LOG_LEVEL_VERBOSE); break;
      default:
        usage_error("invalid option");
        return SCOMP_PARSE_ERROR;
    }
  }

  int positional_count = argc - optind;
  if (!opts->input_path && !opts->output_path && positional_count == 2) {
    opts->input_path = argv[optind];
    opts->output_path = argv[optind + 1];
  } else if (positional_count != 0) {
    usage_error("unexpected positional arguments");
    return SCOMP_PARSE_ERROR;
  }

  if (!opts->input_path || !opts->output_path) {
    usage_error("missing input or output file");
    return SCOMP_PARSE_ERROR;
  }
  return SCOMP_PARSE_OK;
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

  ScompParseResult parse_result = parse_options(argc, argv, &opts);
  if (parse_result == SCOMP_PARSE_EXIT_SUCCESS) return EXIT_SUCCESS;
  if (parse_result == SCOMP_PARSE_ERROR) return EXIT_FAILURE;

  if (strcmp(opts.output_path, "-") == 0 && log_get_level() == LOG_LEVEL_NORMAL) {
    log_set_level(LOG_LEVEL_QUIET);
  }
  logmsg("Sinistra compiler version %s\n", SINVERSION);

  CliIoStatus read_status = cli_io_read_source_text(opts.input_path, &source,
                                                     &source_len);
  if (read_status.code != CLI_IO_OK) {
    result = ERR_COMP_SYNTAX;
    compiler_diag_set(&diag, result, DIAG_PHASE_IO,
                      cli_io_status_detail(read_status));
    compiler_diag_set_source_name(&diag, opts.input_path);
    compiler_diag_set_location(&diag, 1, 1, 1);
    goto compile_error;
  }
  logmsg("Source loaded: %zu bytes from %s.\n", source_len, opts.input_path);

  logmsg("Compiling...\n");
  ParseInput input = {source, source_len,
                      strcmp(opts.input_path, "-") == 0 ? "<stdin>"
                                                        : opts.input_path};
  size_t ast_node_limit = 0;
  const char *test_node_limit = getenv("SINISTRA_TEST_AST_NODE_LIMIT");
  if (test_node_limit && test_node_limit[0]) {
    char *end = NULL;
    errno = 0;
    unsigned long long parsed = strtoull(test_node_limit, &end, 10);
    if (errno == 0 && end && *end == '\0' && parsed > 0 &&
        parsed <= AS_AST_NODE_LIMIT) {
      ast_node_limit = (size_t)parsed;
    }
  }
  result = compile_parse_input_to_bytecode_diag_with_node_limit(
      &input, ast_node_limit, &out, &diag);
  if (result != ERR_NOERROR) {
    goto compile_error;
  }

  size_t bytecode_len = (size_t)(out->nextbyte - out->bytecode);
  logmsg("Compilation completed: %zu bytes.\n", bytecode_len);
  logmsg("Writing bytecode to %s.\n", opts.output_path);
  CliIoStatus write_status = cli_io_write_bytes(opts.output_path, out->bytecode,
                                                bytecode_len);
  if (write_status.code != CLI_IO_OK) {
    logerr("Unable to write output file '%s': %s\n", opts.output_path,
           cli_io_status_detail(write_status));
    result = ERR_COMP_UNKNOWN;
  }
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
