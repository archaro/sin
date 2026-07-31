// sconv - convert itemstore files to canonical v2 format
//
// Licensed under the MIT License - see LICENSE file for details.

#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "config.h"
#include "error.h"
#include "itemstore/item_internal.h"
#include "log.h"
#include "version.h"

CONFIG_t config;

typedef struct {
  const char *input_path;
  const char *output_path;
  ITEMSTORE_DURABILITY_e durability;
  bool replace;
} SconvOptions;

static void print_usage(FILE *stream) {
  fprintf(stream,
          "Usage:\n"
          "  sconv <input itemstore> <output itemstore>\n"
          "  sconv -i <input itemstore> -o <output itemstore> [options]\n\n"
          "Options:\n"
          "  -h, --help                          Show this help text\n"
          "      --version                       Show version information\n"
          "  -i, --input <file>                  Read itemstore from <file>\n"
          "  -o, --output <file>                 Write itemstore to <file>\n"
          "  -d, --itemstore-durability <mode>   Durability: full or fast\n"
          "  -q, --quiet                         Suppress progress messages\n"
          "  -v, --verbose                       Print verbose progress messages\n"
          "      --replace                       Replace an existing output\n");
}

static void usage_error(const char *message) {
  logerr("sconv: %s\n", message);
  logerr("Try 'sconv --help' for more information.\n");
}

static int parse_options(int argc, char **argv, SconvOptions *options) {
  enum { OPT_VERSION = 1000, OPT_REPLACE };
  static const struct option long_options[] = {
      {"help", no_argument, NULL, 'h'},
      {"version", no_argument, NULL, OPT_VERSION},
      {"input", required_argument, NULL, 'i'},
      {"output", required_argument, NULL, 'o'},
      {"itemstore-durability", required_argument, NULL, 'd'},
      {"quiet", no_argument, NULL, 'q'},
      {"verbose", no_argument, NULL, 'v'},
      {"replace", no_argument, NULL, OPT_REPLACE},
      {NULL, 0, NULL, 0},
  };
  memset(options, 0, sizeof(*options));
  options->durability = ITEMSTORE_DURABLE_FULL;
  opterr = 0;
  optind = 1;
  int opt;
  while ((opt = getopt_long(argc, argv, "hi:o:d:qv", long_options, NULL)) != -1) {
    switch (opt) {
      case 'h': print_usage(stdout); return 1;
      case OPT_VERSION: printf("sconv %s\n", SINVERSION); return 1;
      case 'i': options->input_path = optarg; break;
      case 'o': options->output_path = optarg; break;
      case 'd':
        if (strcmp(optarg, "full") == 0) options->durability = ITEMSTORE_DURABLE_FULL;
        else if (strcmp(optarg, "fast") == 0) options->durability = ITEMSTORE_DURABLE_FAST;
        else { usage_error("invalid itemstore durability"); return -1; }
        break;
      case 'q': log_set_level(LOG_LEVEL_QUIET); break;
      case 'v': log_set_level(LOG_LEVEL_VERBOSE); break;
      case OPT_REPLACE: options->replace = true; break;
      default: usage_error("invalid option"); return -1;
    }
  }
  int positional_count = argc - optind;
  if (!options->input_path && !options->output_path && positional_count == 2) {
    options->input_path = argv[optind];
    options->output_path = argv[optind + 1];
  } else if (positional_count != 0) {
    usage_error("unexpected positional arguments"); return -1;
  }
  if (!options->input_path || !options->output_path) {
    usage_error("missing input or output file"); return -1;
  }
  return 0;
}

int main(int argc, char **argv) {
  init_errmsg();
  SconvOptions options;
  int parse_result = parse_options(argc, argv, &options);
  if (parse_result != 0) return parse_result > 0 ? EXIT_SUCCESS : EXIT_FAILURE;
  logmsg("Converting itemstore %s to %s.\n", options.input_path,
         options.output_path);
  logverbose("Conversion options: durability=%s replace=%s.\n",
             options.durability == ITEMSTORE_DURABLE_FULL ? "full" : "fast",
             options.replace ? "yes" : "no");
  ITEMSTORE_CONVERT_RESULT_e result = itemstore_convert(
      options.input_path, options.output_path, options.durability,
      options.replace);
  if (result == ITEMSTORE_CONVERT_TARGET_EXISTS) {
    logerr("Output itemstore '%s' already exists; use --replace to replace it.\n",
           options.output_path);
  } else if (result == ITEMSTORE_CONVERT_SAME_FILE) {
    logerr("Input and output itemstores must be different files.\n");
  } else if (result != ITEMSTORE_CONVERT_SUCCESS) {
    logerr("Failed to convert itemstore '%s'.\n", options.input_path);
  }
  if (result == ITEMSTORE_CONVERT_SUCCESS) logmsg("Conversion completed.\n");
  return result == ITEMSTORE_CONVERT_SUCCESS ? EXIT_SUCCESS : EXIT_FAILURE;
}
