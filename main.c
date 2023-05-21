// sin - a bytecode interpreter
#include <stdio.h>
#include <getopt.h>

#include "memory.h"
#include "log.h"
#include "value.h"
#include "item.h"
#include "stack.h"
#include "interpret.h"

void usage() {
  logmsg("Sin interpreter.\nSyntax: sin <options> <input file>\n");
  logmsg("Options:\n");
  logmsg(" -h, --help\t\tThis message.\n");
  logmsg(" -l, --log [filename]\tLog output to <filename>.\n");
  logmsg("\t\t\tIf no filename is given, the default filename, 'sin'\n");
  logmsg("\t\t\tis used.  The filename is suffixed with .log for stdout,\n");
  logmsg("\t\t\tand .err for stderr.\n");
  logmsg(" -o, --object <filename> \tObject code to interpret.\n");
}

int main(int argc, char **argv) {
  int filesize = 0;
  unsigned char *bytecode = NULL;

  if (argc < 2) {
    usage();
    exit(EXIT_FAILURE);
  }

  // Are there any interesting options?
  int opt;
  const struct option options[] =
  {
    {"help", no_argument, 0, 'h'},
    {"log", optional_argument, 0, 'l'},
    {"object", required_argument, 0, 'o'},
    {NULL, 0, 0, '\0'}
  };
  while ((opt = getopt_long(argc, argv, "hl::o:", options, NULL)) != -1) {
    switch(opt) {
      case 'h':
        usage();
        exit(EXIT_SUCCESS);
        break;
      case 'l':
        // Optional: if given, log all output to file.
        if (optarg == NULL && optind < argc && argv[optind][0] != '-') {
          optarg = argv[optind++];
        }
        if (optarg != NULL) {
          // Filename is present
          log_to_file(optarg);
        } else {
          // No filename, use default.
          log_to_file("sin");
        }
        break;
      case 'o':
        // Mandatory: Name of the object code file.
        // Load a file to interpret, otherwise what's the point?
        // The file is always the last argument.
        FILE *in = fopen(optarg, "r");
        if (!in) {
          logerr("Unable to open input file: %s\n", optarg);
          exit(EXIT_FAILURE);
        }
        fseek(in, 0, SEEK_END);
        filesize = ftell(in);
        fseek(in, 0, SEEK_SET);
        bytecode = GROW_ARRAY(unsigned char, bytecode, 0, filesize);
        fread(bytecode, filesize, sizeof(char), in);
        fclose(in);
        logmsg("Bytecode loaded: %d bytes.\n", filesize);
        break;
      default:
        usage();
        return EXIT_FAILURE;
    }
  }

  // Just check to see if we have been given some bytecode.
  if (!bytecode) {
    logerr("No bytecode to process!\n");
    exit(EXIT_FAILURE);
  }

  // Do some preparations
  init_interpreter();
  ITEM_t *start = make_item("start", ITEM_code, VALUE_NIL, bytecode, filesize);

  // Now we have some bytecode, let's see what it does.
  int ret = interpret(start);
  logmsg("Bytecode interpreter returned: %d\n", ret);

  // Clean up
  logmsg("Shutting down.\n");
  free_item(start);
  close_log();
  exit(EXIT_SUCCESS);
}

