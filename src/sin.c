// sin - a bytecode interpreter

// Licensed under the MIT License - see LICENSE file for details.

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <getopt.h>
#include <sys/stat.h>
#include <unistd.h>
#include <signal.h>
#include <setjmp.h>
#include <uv.h>

#include "config.h"
#include "error.h"
#include "memory.h"
#include "log.h"
#include "network.h"
#include "task.h"
#include "value.h"
#include "item.h"
#include "stack.h"
#include "interpret.h"

// Error handling
jmp_buf recovery;

// The configuration object - for passing interesting data around globally.
CONFIG_t config;

void close_all_tasks(uv_handle_t* handle, void* arg) {
  if (!uv_is_closing(handle)) { //FALSE, handle is closing
    uv_close(handle, NULL);
  }
}

void handle_sigusr1(int sig) {
  // SIGUSR1 is raised in various places, and should cause the interpret()
  // function to terminate.
  logerr(errmsg[ERR_RUNTIME_SIGUSR1]);
  longjmp(recovery, ERR_RUNTIME_SIGUSR1);
}

void usage() {
  logmsg("Sin interpreter.\nSyntax: sin <options>\n");
  logmsg("Options:\n");
  logmsg(" -b, --bootonly\t\tOnly execute the bootstrap code.\n");
  logmsg("\t\t\t  This option is used to compile items without running\n");
  logmsg("\t\t\t  the game.  Useful for initialisation.\n");
  logmsg(" -h, --help\t\tThis message.\n");
  logmsg(" -i, --itemstore <file>\tItemstore file to load.\n");
  logmsg("\t\t\t  If this option is not supplied, the default filename\n");
  logmsg("\t\t\t  'items.dat' is used.  The file is created if it does\n");
  logmsg("\t\t\t  not exist.\n");
  logmsg(" -l, --log [file]\tLog output to <file>.\n");
  logmsg("\t\t\t  If no filename is given, the default filename, 'sin'\n");
  logmsg("\t\t\t  is used.  The filename is suffixed with .log for\n");
  logmsg("\t\t\t  stdout and .err for stderr.\n");
  logmsg(" -n, --input <item>\tName of input-handler item.\n");
  logmsg("\t\t\t  If not supplied, this defaults to 'input'.\n");
  logmsg(" -o, --object <file>\tObject code to interpret.\n");
  logmsg(" -p, --port <port>\tPort to listen on.\n");
  logmsg(" -s, --srcroot <dir>\tRoot of source tree.\n");
  logmsg("\t\t\t  If this option is not supplied, the default directory\n");
  logmsg("\t\t\t  './srcroot' is used, which will be created if it does\n");
  logmsg("\t\t\t  not exist.  If this option is supplied the directory\n");
  logmsg("\t\t\t  given must exist or the interpreter will not run.\n");
}

int main(int argc, char **argv) {
  FILE *in;
  int filesize = 0, listener_port = LISTENER_PORT;
  struct stat buffer;
  uint8_t *bytecode = NULL;
  bool bootonly = false;

  if (argc < 2) {
    usage();
    exit(EXIT_FAILURE);
  }

  // Set up some defaults (may be overridden by config options)
  config.itemroot = NULL;
  config.srcroot = NULL;
  config.input = strdup("input");
  config.inputline = malloc(strlen(config.input) + 6);
  config.inputtext = malloc(strlen(config.input) + 6);
  sprintf(config.inputline, "%s.line", config.input);
  sprintf(config.inputtext, "%s.text", config.input);
  config.safe_shutdown = true;

  // Do the very early preparations, for things which are needed
  // before even the options are processed.
  init_errmsg();
  struct sigaction act;
  act.sa_handler = handle_sigusr1;
  sigemptyset(&act.sa_mask);
  act.sa_flags = 0;
  if (sigaction(SIGUSR1, &act, NULL) < 0) {
    logerr("Unable to install signal handler.\n");
    exit(EXIT_FAILURE);
  }

  // Are there any interesting options?
  int opt;
  const struct option options[] =
  {
    {"bootonly", no_argument, 0, 'b'},
    {"help", no_argument, 0, 'h'},
    {"itemstore", required_argument, 0, 'i'},
    {"log", optional_argument, 0, 'l'},
    {"input", required_argument, 0, 'n'},
    {"object", required_argument, 0, 'o'},
    {"port", optional_argument, 0, 'p'},
    {"srcroot", required_argument, 0, 's'},
    {NULL, 0, 0, '\0'}
  };
  while ((opt = getopt_long(argc, argv, "bhi:l::n:o:p:s:", options, NULL)) != -1) {
    switch(opt) {
      case 'b':
        bootonly = true;
        break;
      case 'h':
        usage();
        exit(EXIT_SUCCESS);
        break;
      case 'i':
        // Optional: if given use this filename for the itemstore.
        config.itemstore = strdup(optarg);
        if (stat(config.itemstore, &buffer) == 0) {
          // The file exists, so load it.
          logmsg("Loading itemstore from %s.\n", config.itemstore);
          config.itemroot = load_itemstore(config.itemstore);
        } else {
          // The file does not exist, so create a blank itemstore
          // and save it to the file at the end.
          logmsg("Creating a new itemstore, which will be saved as %s.\n",
                                                         config.itemstore);
          config.itemroot = make_root_item("root");
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
      case 'n':
        // Optional: name of item which handles input processing
        // Defaults to 'input' if not given.
        if (!config.itemroot) {
          logerr("If -n option is given, -i option must be given first.\n");
          exit(EXIT_FAILURE);
        }
        ITEM_t *input_item = find_item(config.itemroot, optarg);
        if (!input_item || input_item->type != ITEM_code) {
          logerr("Item `%s` does not exist, or is not a code item.\n",
                                                                  optarg);
          exit(EXIT_FAILURE);
        } else {
          free(config.input);
          config.input = strdup(optarg);
        }
        break;
      case 'o':
        // Mandatory: Name of the object code file.
        // Load a file to interpret, otherwise what's the point?
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
      case 'p':
        // Optional: port to listen on.
        listener_port = atoi(optarg);
        break;
      case 's':
        // Optional: root directory of the source tree.
        config.srcroot = strdup(optarg);
        break;
      default:
        usage();
        return EXIT_FAILURE;
    }
  }

  // Before we continue, has the source root been defined?
  // If not, use the default.
  if (!config.srcroot) {
    config.srcroot = strdup("srcroot");
    struct stat s;
    int err = stat(config.srcroot, &s);
    if (err == -1) {
      // Doesn't exist, so create it.
      mkdir(config.srcroot, 0777);
      logmsg("Creating new source root in current directory.\n");
    } else {
      if(!S_ISDIR(s.st_mode)) {
        // Exists, but not a directory.  Panic.
        logerr("./%s exists but it is not a directory.\n", config.srcroot);
        free(config.srcroot);
        exit(EXIT_FAILURE);
      }
    }
  } else {
    // We have been given a source root, so does it exist?
    struct stat s;
    int err = stat(config.srcroot, &s);
    if (err == -1) {
      logerr("Directory %s does not exist.\n", config.srcroot);
      free(config.srcroot);
      exit(EXIT_FAILURE);
    } else {
      if(!S_ISDIR(s.st_mode)) {
        // Exists, but not a directory
        logerr("./%s exists but it is not a directory.\n", config.srcroot);
        free(config.srcroot);
        exit(EXIT_FAILURE);
      } else {
        if (access(config.srcroot, W_OK) != 0) {
          logerr("./%s exists, but it is not writable.\n", config.srcroot);
          free(config.srcroot);
          exit(EXIT_FAILURE);
        }
      }
    }
  }
  logmsg("Using '%s' as the source root.\n", config.srcroot);

  // Just check to see if we have been given some bytecode.
  if (!bytecode) {
    logerr("No bytecode to process!\n");
    exit(EXIT_FAILURE);
  }

  // Do some preparations
  DEBUG_LOG("DEBUG IS DEFINED\n");
  ITEMDEBUG_LOG("ITEMDEBUG IS DEFINED\n");
  STRINGDEBUG_LOG("STRINGDEBUG IS DEFINED\n");
  DISASS_LOG("DISASS IS DEFINED\n");
  config.vm = make_vm();

  init_interpreter();
  // If the itemstore hasn't been loaded, do so now.
  if (!config.itemroot) {
    config.itemstore = strdup("items.dat");
    if (stat(config.itemstore, &buffer) == 0) {
      logmsg("Loading itemstore from %s\n", config.itemstore);
      config.itemroot = load_itemstore(config.itemstore);
    } else {
      logmsg("Creating a new itemstore, which will be saved as %s.\n",
                                                         config.itemstore);
      config.itemroot = make_root_item("root");
    }
  }
  // Boot is a special item, which sits outside of the itemstore.
  // We have to abuse the API slightly here. :(
  ITEM_t *boot = make_root_item("boot");
  boot->type = ITEM_code;
  boot->bytecode = bytecode;
  boot->bytecode_len = filesize;
  // Prepare the loop - the boot item should be setting up tasks,
  // so the loop needs to be read for 'em.
  config.loop = GROW_ARRAY(uv_loop_t, config.loop, 0, sizeof(uv_loop_t));
  uv_loop_init(config.loop);

  // This is a relatively safe restart point if things turn ugly.
  // This will need to be revisited once the eventloop is running.
  if (setjmp(recovery) == 0) {
    logmsg("Setting up error handler.\n");
  } else {
    logerr("SIGUSR1 received.  Restarting boot item.\n");
    logerr("Destroying and recreating all stacks.\n");
    destroy_vm(config.vm);
    config.vm = make_vm();
  }
  // Execute the boot item.  This should set up all the tasks for
  // the main game.  It must not be an infinite loop!
  VALUE_t ret = interpret(boot);
  if (ret.type == VALUE_int) {
    logmsg("Bytecode interpreter returned: %ld\n", ret.i);
  } else if (ret.type == VALUE_str) {
    logmsg("Bytecode interpreter returned: %s\n", ret.s);
    FREE_ARRAY(char, ret.s, strlen(ret.s));
  } else if (ret.type == VALUE_bool) {
    logmsg("Bytecode interpreter returned: %s\n", ret.i?"true":"false");
  } else if (ret.type == VALUE_nil) {
    logmsg("Bytecode interpreter returned nil.\n");
  } else {
    logerr("Interpreter returned unknown value type: '%c'.\n", ret.type);
  }
  // Finished with the boot item, and all its empty promises
  destroy_vm(config.vm);
  destroy_item(boot);

  int runloop_retval = 0;
  uv_idle_t input_task;
  if (!bootonly) {
    // Set up the item which handles input.
    logmsg("Using `%s` as the input item.\n", config.input);
    config.input_vm = make_vm();
    uv_idle_init(config.loop, &input_task);
    uv_idle_start(&input_task, input_processor);
    // Here we go...
    logmsg("Running...\n");
    config.maxconns = MAXCONNS;
    config.lastconn = 255;
    init_networking();
    init_listener(listener_port);
    runloop_retval = uv_run(config.loop, UV_RUN_DEFAULT);
  }

  // Clean up before shutdown.
  logmsg("Shutting down.\n");
  DEBUG_LOG("DEBUG IS DEFINED\n");
  ITEMDEBUG_LOG("ITEMDEBUG IS DEFINED\n");
  STRINGDEBUG_LOG("STRINGDEBUG IS DEFINED\n");
  DISASS_LOG("DISASS IS DEFINED\n");
  if (!bootonly) {
    shutdown_listener();
    uv_idle_stop(&input_task);
    // Send a close request to every registered callback
    uv_walk(config.loop, close_all_tasks, NULL);
    // Process pending handles - should all be closed or closing
    uv_run(config.loop, UV_RUN_ONCE);
    finalise_tasks();
    shutdown_networking();
    destroy_vm(config.input_vm);
  }
  uv_loop_close(config.loop);
  if (config.safe_shutdown) {
    save_itemstore(config.itemstore, config.itemroot);
  }
  FREE_ARRAY(uv_loop_t, config.loop, sizeof(uv_loop_t));
  free(config.itemstore);
  free(config.srcroot);
  free(config.input);
  free(config.inputline);
  free(config.inputtext);
  destroy_item(config.itemroot);
  close_log();
  return runloop_retval;
}

