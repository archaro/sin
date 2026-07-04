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
  logmsg("Sinistra disassembler version %s.\nSyntax: sdiss <options>\n", SINVERSION);
  logmsg("Options:\n");
  logmsg(" -h, --help\t\tThis message.\n");
  logmsg(" -o, --object <file>\tObject code to disassemble.\n");
  logmsg("     --raw\t\tShow raw bytes per instruction.\n");
  logmsg("     --no-header\tSkip locals/params header output.\n");
}

static void stdout_write(void *ctx, const char *data, size_t len) {
  (void)ctx;
  fwrite(data, 1, len, stdout);
}

int main(int argc, char **argv) {
  FILE *in = NULL;
  int filesize = 0;
  bytecode = NULL;
  if (argc < 2) {
    usage();
    exit(EXIT_FAILURE);
  }
  int opt;
  const struct option options[] = {
    {"help", no_argument, 0, 'h'},
    {"object", required_argument, 0, 'o'},
    {"raw", no_argument, 0, 1000},
    {"no-header", no_argument, 0, 1001},
    {NULL, 0, 0, '\0'}
  };
  while ((opt = getopt_long(argc, argv, "ho:", options, NULL)) != -1) {
    switch (opt) {
      case 'h':
        usage();
        exit(EXIT_SUCCESS);
      case 'o':
        in = fopen(optarg, "rb");
        if (!in) {
          logerr("Unable to open input file: %s\n", optarg);
          exit(EXIT_FAILURE);
        }
        fseek(in, 0, SEEK_END);
        filesize = ftell(in);
        fseek(in, 0, SEEK_SET);
        bytecode = realloc(bytecode, (size_t)filesize);
        fread(bytecode, filesize, sizeof(char), in);
        fclose(in);
        logmsg("Bytecode loaded: %d bytes.\n", filesize);
        break;
      case 1000:
        opt_raw = 1;
        break;
      case 1001:
        opt_no_header = 1;
        break;
      default:
        usage();
        return EXIT_FAILURE;
    }
  }
  if (!bytecode) {
    logerr("No bytecode to process!\n");
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
