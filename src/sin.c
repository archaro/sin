// sin - a bytecode interpreter
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <getopt.h>
#include <sys/stat.h>

#include "memory.h"
#include "log.h"
#include "value.h"
#include "item.h"
#include "stack.h"
#include "vm.h"
#include "interpret.h"

// The Virtual Machine
VM_t vm;

// The slab allocator
Allocator allocator;

// The Itemstore
ITEM_t *itemroot = NULL;

void usage() {
  logmsg("Sin interpreter.\nSyntax: sin <options> <input file>\n");
  logmsg("Options:\n");
  logmsg(" -h, --help\t\tThis message.\n");
  logmsg(" -i, --itemstore <file>\tItemstore file to load.\n");
  logmsg("\t\t\tIf this option is not supplied, the default filename\n");
  logmsg("\t\t\t'items.dat' is used.  The file is created if it does not\n");
  logmsg("\t\t\texist.\n");
  logmsg(" -l, --log [file]\tLog output to <filename>.\n");
  logmsg("\t\t\tIf no filename is given, the default filename, 'sin' is\n");
  logmsg("\t\t\tused.  The filename is suffixed with .log for stdout,\n");
  logmsg("\t\t\tand .err for stderr.\n");
  logmsg(" -o, --object <file>\tObject code to interpret.\n");
}

int main(int argc, char **argv) {
  FILE *in;
  int filesize = 0;
  struct stat buffer;
  char *itemstore;
  uint8_t *bytecode = NULL;

  if (argc < 2) {
    usage();
    exit(EXIT_FAILURE);
  }

  // Do the very early preparations, for things which are needed
  // before even the options are processed.
  init_allocator(&allocator);

  // Are there any interesting options?
  int opt;
  const struct option options[] =
  {
    {"help", no_argument, 0, 'h'},
    {"itemstore", required_argument, 0, 'i'},
    {"log", optional_argument, 0, 'l'},
    {"object", required_argument, 0, 'o'},
    {NULL, 0, 0, '\0'}
  };
  while ((opt = getopt_long(argc, argv, "hi:l::o:", options, NULL)) != -1) {
    switch(opt) {
      case 'h':
        usage();
        exit(EXIT_SUCCESS);
        break;
      case 'i':
        // Optional: if given use this filename for the itemstore.
        itemstore = strdup(optarg);
        if (stat(itemstore, &buffer) == 0) {
          // The file exists, so load it.
          logmsg("Loading itemstore from %s.\n", itemstore);
          itemroot = load_itemstore(itemstore);
        } else {
          // The file does not exist, so create a blank itemstore
          // and save it to the file at the end.
          logmsg("Creating a new itemstore, which will be saved as %s.\n",
                                                                 itemstore);
          itemroot = make_root_item("root");
        }
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
        in = fopen(optarg, "r");
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
  DEBUG_LOG("DEBUG IS DEFINED\n");
  vm.stack = make_stack();
  vm.callstack = make_callstack();

  init_interpreter();
  // If the itemstore hasn't been loaded, do so now.
  if (!itemroot) {
    itemstore = strdup("items.dat");
    if (stat(itemstore, &buffer) == 0) {
      logmsg("Loading itemstore from %s\n", itemstore);
      itemroot = load_itemstore(itemstore);
    } else {
      logmsg("Creating a new itemstore, which will be saved as %s.\n",
                                                                 itemstore);
      itemroot = make_root_item("root");
    }
  }
  // Boot is a special item, which sits outside of the itemstore.
  // We have to abuse the API slightly here. :(
  ITEM_t *boot = make_root_item("boot");
  boot->type = ITEM_code;
  boot->bytecode = bytecode;
  boot->bytecode_len = filesize;

  // Now we have some bytecode, let's see what it does.
  VALUE_t ret = interpret(boot);
  if (ret.type == VALUE_int) {
    logmsg("Bytecode interpreter returned: %ld\n", ret.i);
  } else if (ret.type == VALUE_str) {
    logmsg("Bytecode interpreter returned: %s\n", ret.s);
  } else if (ret.type == VALUE_bool) {
    logmsg("Bytecode interpreter returned: %s\n", ret.i?"true":"false");
  } else if (ret.type == VALUE_nil) {
    logmsg("Bytecode interpreter returned nil.\n");
  } else {
    logerr("Interpreter returned unknown value type: '%c'.\n", ret.type);
  }

  // Clean up before shutdown.
  logmsg("Shutting down.\n");
  DEBUG_LOG("DEBUG IS DEFINED\n");
  save_itemstore(itemstore, itemroot);
  destroy_stack(vm.stack);
  destroy_callstack(vm.callstack);
  free(itemstore);
  destroy_item(itemroot);
  destroy_item(boot);
  destroy_allocator(&allocator);
  close_log();
  exit(EXIT_SUCCESS);
}

