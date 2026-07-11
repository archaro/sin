// sdiss - a bytecode disassembler

// Licensed under the MIT License - see LICENSE file for details.

#include <getopt.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "config.h"
#include "log.h"
#include "memory.h"
#include "sdiss_core.h"
#include "version.h"

CONFIG_t config;

static uint8_t *bytecode;
static int opt_raw = 0, opt_no_header = 0;

void usage() {
  logmsg("Syntax: sdiss <options>\n");
  logmsg("Options:\n");
  logmsg(" -h, --help\t\tThis message.\n");
  logmsg("     --version\t\tShow version information.\n");
  logmsg(" -o, --object <file>\tObject code to disassemble.\n");
  logmsg("     --raw\t\tShow raw bytes per instruction.\n");
  logmsg("     --no-header\tSkip locals/params header output.\n");
}

static void usage_error(const char *message) {
  logerr("sdiss: %s\n", message);
  logerr("Try 'sdiss --help' for more information.\n");
}

static void stdout_write(void *ctx, const char *data, size_t len) {
  (void)ctx;
  fwrite(data, 1, len, stdout);
}

int main(int argc, char **argv) {
  FILE *in = NULL;
  size_t filesize = 0;
  bytecode = NULL;
  if (argc < 2) {
    usage_error("missing object file");
    exit(EXIT_FAILURE);
  }
  int opt;
  enum { OPT_VERSION = 1002 };
  const struct option options[] = {
    {"help", no_argument, 0, 'h'},
    {"version", no_argument, 0, OPT_VERSION},
    {"object", required_argument, 0, 'o'},
    {"raw", no_argument, 0, 1000},
    {"no-header", no_argument, 0, 1001},
    {NULL, 0, 0, '\0'}
  };
  opterr = 0;
  optind = 1;
  while ((opt = getopt_long(argc, argv, "ho:", options, NULL)) != -1) {
    switch (opt) {
      case 'h':
        usage();
        exit(EXIT_SUCCESS);
      case OPT_VERSION:
        printf("sdiss %s\n", SINVERSION);
        exit(EXIT_SUCCESS);
      case 'o':
        in = fopen(optarg, "rb");
        if (!in) {
          logerr("Unable to open input file: %s\n", optarg);
          exit(EXIT_FAILURE);
        }
        if (fseek(in, 0, SEEK_END) != 0) {
          logerr("Unable to seek input file: %s\n", optarg);
          fclose(in);
          exit(EXIT_FAILURE);
        }
        long file_len = ftell(in);
        if (file_len < 0 || (uint64_t)file_len > UINT32_MAX) {
          logerr("Input file is too large to disassemble: %s\n", optarg);
          fclose(in);
          exit(EXIT_FAILURE);
        }
        filesize = (size_t)file_len;
        if (fseek(in, 0, SEEK_SET) != 0) {
          logerr("Unable to rewind input file: %s\n", optarg);
          fclose(in);
          exit(EXIT_FAILURE);
        }
        uint8_t *new_bytecode = realloc(bytecode, filesize);
        if (filesize > 0 && !new_bytecode) {
          logerr("Unable to allocate %zu bytes for input file: %s\n",
                 filesize, optarg);
          fclose(in);
          exit(EXIT_FAILURE);
        }
        bytecode = new_bytecode;
        if (filesize > 0 && fread(bytecode, 1, filesize, in) != filesize) {
          logerr("Unable to read complete input file: %s\n", optarg);
          fclose(in);
          exit(EXIT_FAILURE);
        }
        fclose(in);
        logmsg("Bytecode loaded: %zu bytes.\n", filesize);
        break;
      case 1000:
        opt_raw = 1;
        break;
      case 1001:
        opt_no_header = 1;
        break;
      default:
        usage_error("invalid option");
        return EXIT_FAILURE;
    }
  }
  if (optind < argc) {
    usage_error("unexpected positional arguments");
    return EXIT_FAILURE;
  }
  if (!bytecode) {
    usage_error("missing object file");
    exit(EXIT_FAILURE);
  }
  logmsg("Beginning disassembly...\n");

  SDissOptions dis_options = {.raw = opt_raw, .no_header = opt_no_header};
  SDissResult result = sdiss_disassemble_bytes(bytecode, (uint32_t)filesize,
                                               &dis_options, stdout_write, NULL);

  if (result.status == BC_VERIFY_ERROR) {
    logerr("%s\n", result.diagnostic.message);
    logerr("Disassembly aborted due to malformed bytecode.\n");
  } else if (result.status == BC_VERIFY_WARNING) {
    logerr("%s\n", result.diagnostic.message);
  }

  logmsg("Shutting down.\n");
  free(bytecode);
  return result.status == BC_VERIFY_ERROR ? EXIT_FAILURE : EXIT_SUCCESS;
}
