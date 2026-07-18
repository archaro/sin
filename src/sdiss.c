// sdiss - a bytecode disassembler

// Licensed under the MIT License - see LICENSE file for details.

#include <getopt.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "config.h"
#include "cli_io.h"
#include "log.h"
#include "memory.h"
#include "sdiss_core.h"
#include "version.h"

CONFIG_t config;

static uint8_t *bytecode;
static int opt_raw = 0, opt_no_header = 0;

static void usage(void) {
  printf("Usage: sdiss -o <object file> [options]\n");
  printf("Options:\n");
  printf(" -h, --help\t\tThis message.\n");
  printf("     --version\t\tShow version information.\n");
  printf(" -o, --object <file>\tObject code to disassemble.\n");
  printf("     --raw\t\tShow raw bytes per instruction.\n");
  printf("     --no-header\tSkip locals/params header output.\n");
  printf(" -q, --quiet\t\tSuppress progress messages.\n");
  printf(" -v, --verbose\t\tPrint progress messages.\n");
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
  size_t filesize = 0;
  bytecode = NULL;
  int opt;
  enum {
    OPT_RAW = 1000,
    OPT_NO_HEADER = 1001,
    OPT_VERSION = 1002
  };
  const struct option options[] = {
    {"help", no_argument, 0, 'h'},
    {"version", no_argument, 0, OPT_VERSION},
    {"object", required_argument, 0, 'o'},
    {"raw", no_argument, 0, OPT_RAW},
    {"no-header", no_argument, 0, OPT_NO_HEADER},
    {"quiet", no_argument, 0, 'q'},
    {"verbose", no_argument, 0, 'v'},
    {NULL, 0, 0, '\0'}
  };
  opterr = 0;
  optind = 1;
  while ((opt = getopt_long(argc, argv, "ho:qv", options, NULL)) != -1) {
    switch (opt) {
      case 'h':
        usage();
        exit(EXIT_SUCCESS);
      case OPT_VERSION:
        printf("sdiss %s\n", SINVERSION);
        exit(EXIT_SUCCESS);
      case 'q':
        log_set_level(LOG_LEVEL_QUIET);
        break;
      case 'v':
        log_set_level(LOG_LEVEL_VERBOSE);
        break;
      case 'o': {
        uint8_t *new_bytecode = NULL;
        size_t new_filesize = 0;
        CliIoStatus io_status = cli_io_read_file_bytes(optarg, &new_bytecode,
                                                       &new_filesize);
        if (io_status.code != CLI_IO_OK) {
          logerr("Unable to read object file '%s': %s\n", optarg,
                 cli_io_status_detail(io_status));
          free(new_bytecode);
          exit(EXIT_FAILURE);
        }
        if (new_filesize > UINT32_MAX) {
          logerr("Input file is too large to disassemble: %s\n", optarg);
          free(new_bytecode);
          exit(EXIT_FAILURE);
        }
        free(bytecode);
        bytecode = new_bytecode;
        filesize = new_filesize;
        logmsg("Bytecode loaded: %zu bytes from %s.\n", filesize, optarg);
        break;
      }
      case OPT_RAW:
        opt_raw = 1;
        break;
      case OPT_NO_HEADER:
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
  logmsg("Disassembly options: raw=%d no_header=%d.\n", opt_raw, opt_no_header);

  SDissOptions dis_options = {.raw = opt_raw, .no_header = opt_no_header};
  SDissResult result = sdiss_disassemble_bytes(bytecode, (uint32_t)filesize,
                                               &dis_options, stdout_write, NULL);

  if (result.status == BC_VERIFY_ERROR) {
    logerr("%s\n", result.diagnostic.message);
    logerr("Disassembly aborted due to malformed bytecode.\n");
  } else if (result.status == BC_VERIFY_WARNING) {
    logerr("%s\n", result.diagnostic.message);
  }

  logmsg("Finishing up.\n");
  free(bytecode);
  return result.status == BC_VERIFY_ERROR ? EXIT_FAILURE : EXIT_SUCCESS;
}
