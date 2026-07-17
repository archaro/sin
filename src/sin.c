// sin - a bytecode interpreter

// Licensed under the MIT License - see LICENSE file for details.

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <getopt.h>
#include <sys/stat.h>
#include <unistd.h>
#include <signal.h>
#include <setjmp.h>
#include <errno.h>
#include <uv.h>

#include "version.h"
#include "cli_io.h"
#include "config.h"
#include "error.h"
#include "memory.h"
#include "log.h"
#include "floatconv.h"
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

static void runtime_context_from_config(RuntimeContext *ctx, VM_t *vm) {
  runtime_context_init(ctx, vm);
  ctx->itemroot = config.itemroot;
  ctx->loop = config.loop;
  ctx->itemstore_filename = config.itemstore;
  ctx->itemstore_durability = config.itemstore_durability;
  ctx->srcroot = config.srcroot;
  ctx->input_name = config.input;
  ctx->inputline_name = config.inputline;
  ctx->inputtext_name = config.inputtext;
  ctx->maxconns = &config.maxconns;
  ctx->lastconn = &config.lastconn;
  ctx->safe_shutdown = &config.safe_shutdown;
  ctx->shutdown_requested = &config.shutdown_requested;
  ctx->network.lines = line;
  ctx->network.maxconns = &config.maxconns;
  ctx->network.lastconn = &config.lastconn;
  ctx->network.inputline_name = config.inputline;
  ctx->network.inputtext_name = config.inputtext;
  ctx->strict_validation = config.strict_validation;
  ctx->strict_runtime_contracts = config.strict_runtime_contracts;
  (void)runtime_init(ctx, vm);
}

void close_all_tasks(uv_handle_t* handle, void* arg) {
  (void)arg;
  if (!uv_is_closing(handle)) { //FALSE, handle is closing
    uv_close(handle, NULL);
  }
}

void handle_sigusr1(int sig) {
  // SIGUSR1 is raised in various places, and should cause the interpret()
  // function to terminate.
  (void)sig;
  logerr(errmsg[ERR_RUNTIME_SIGUSR1]);
  longjmp(recovery, ERR_RUNTIME_SIGUSR1);
}

static void usage(void) {
  printf("Syntax: sin <options>\n");
  printf("Options:\n");
  printf("     --loadonly\t\tLoad and execute the given object file.\n");
  printf("\t\t\t  This option is used to compile items without running\n");
  printf("\t\t\t  the game.  Useful for initialisation.\n");
  printf(" -h, --help\t\tThis message.\n");
  printf("     --version\t\tShow version information.\n");
  printf(" -i, --itemstore <file>\tItemstore file to load.\n");
  printf("\t\t\t  If this option is not supplied, the default filename\n");
  printf("\t\t\t  'items.dat' is used.  The file is created if it does\n");
  printf("\t\t\t  not exist.\n");
  printf(" -d, --itemstore-durability <full|fast>\n");
  printf("\t\t\t  Full durability synchronizes itemstore data before\n");
  printf("\t\t\t  replacement; fast mode skips that synchronization.\n");
  printf(" -l, --log [file]\tLog output to <file>.\n");
  printf("\t\t\t  If no filename is given, the default filename, 'sin'\n");
  printf("\t\t\t  is used.  The filename is suffixed with .log for\n");
  printf("\t\t\t  stdout and .err for stderr.\n");
  printf(" -n, --input <item>\tName of input-handler item.\n");
  printf("\t\t\t  If not supplied, this defaults to 'input'.\n");
  printf(" -o, --object <file>\tObject code to interpret.\n");
  printf(" -p, --port <port>\tPort to listen on.\n");
  printf(" -s, --srcroot <dir>\tRoot of source tree.\n");
  printf("\t\t\t  If this option is not supplied, the default directory\n");
  printf("\t\t\t  './srcroot' is used, which will be created if it does\n");
  printf("\t\t\t  not exist.  If this option is supplied the directory\n");
  printf("\t\t\t  given must exist or the interpreter will not run.\n");
  printf("     --strict-validation\n");
  printf("\t\t\t  Verify bytecode before runtime execution.\n");
  printf("     --strict-runtime-contracts\n");
  printf("\t\t\t  Report runtime argument contract violations.\n");
}

static void usage_error(const char *message) {
  logerr("sin: %s\n", message);
  logerr("Try 'sin --help' for more information.\n");
}

static ITEM_t *load_or_create_itemstore_with_options(const char *filename,
                                                 bool strict_validation) {
  struct stat buffer;
  if (stat(filename, &buffer) == 0) {
    logstatus("Loading itemstore from %s.\n", filename);
    ITEM_t *root = load_itemstore_with_options(filename, strict_validation);
    if (!root) {
      logerr("Existing itemstore '%s' could not be loaded; refusing to "
             "replace it.\n", filename);
    }
    return root;
  }

  if (errno != ENOENT) {
    logerr("Unable to inspect itemstore '%s': %s\n", filename,
           strerror(errno));
    return NULL;
  }

  logstatus("Creating a new itemstore, which will be saved as %s.\n", filename);
  return make_root_item("root");
}

static bool flag_requested(int argc, char **argv, const char *flag) {
  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], flag) == 0) return true;
  }
  return false;
}

typedef struct SinStartupOptions {
  size_t filesize;
  int listener_port;
  uint8_t *bytecode;
  bool loadonly;
} SinStartupOptions;

static char *make_input_alias(const char *input, const char *suffix) {
  size_t name_len = 0;
  size_t allocation_size = 0;
  if (!input || !suffix ||
      alloc_add_overflow(strlen(input), strlen(suffix), &name_len) ||
      alloc_add_overflow(name_len, 1u, &allocation_size)) {
    return NULL;
  }
  char *name = malloc(allocation_size);
  if (!name) return NULL;
  snprintf(name, allocation_size, "%s%s", input, suffix);
  return name;
}

static bool set_config_input_name(const char *input_name) {
  char *input = input_name ? strdup(input_name) : NULL;
  char *inputline = make_input_alias(input, ".line");
  char *inputtext = make_input_alias(input, ".text");
  if (!input || !inputline || !inputtext) {
    free(input);
    free(inputline);
    free(inputtext);
    return false;
  }
  free(config.input);
  free(config.inputline);
  free(config.inputtext);
  config.input = input;
  config.inputline = inputline;
  config.inputtext = inputtext;
  return true;
}

static int init_default_config(int argc, char **argv, SinStartupOptions *startup) {
  startup->filesize = 0;
  startup->listener_port = LISTENER_PORT;
  startup->bytecode = NULL;
  startup->loadonly = false;

  config.itemroot = NULL;
  config.srcroot = NULL;
  config.itemstore = NULL;
  config.itemstore_durability = ITEMSTORE_DURABLE_FULL;
  config.input = NULL;
  config.inputline = NULL;
  config.inputtext = NULL;
  if (!set_config_input_name("input")) {
    logerr("Unable to allocate default input item names.\n");
    return EXIT_FAILURE;
  }
  config.safe_shutdown = true;
  config.shutdown_requested = false;
  /* Itemstores named with -i are loaded while options are processed. Detect
   * this global validation policy first so its effect is independent of
   * command-line option order. */
  config.strict_validation = flag_requested(argc, argv, "--strict-validation");
  config.strict_runtime_contracts = flag_requested(argc, argv, "--strict-runtime-contracts");
  /* Runtime contract diagnostics are intentionally independent from bytecode
   * validation and remain disabled unless explicitly requested. */
  return EXIT_SUCCESS;
}

static void init_signal_handler(void) {
  init_errmsg();
  struct sigaction act;
  act.sa_handler = handle_sigusr1;
  sigemptyset(&act.sa_mask);
  act.sa_flags = 0;
  if (sigaction(SIGUSR1, &act, NULL) < 0) {
    logerr("Unable to install signal handler.\n");
    exit(EXIT_FAILURE);
  }
}

static int parse_sin_options(int argc, char **argv, SinStartupOptions *startup) {
  int opt;
  enum { OPT_STRICT_VALIDATION = 1000, OPT_STRICT_RUNTIME_CONTRACTS = 1001,
         OPT_VERSION = 1002, OPT_LOADONLY = 1003 };
  const struct option options[] =
  {
    {"loadonly", no_argument, 0, OPT_LOADONLY},
    {"itemstore-durability", required_argument, 0, 'd'},
    {"help", no_argument, 0, 'h'},
    {"version", no_argument, 0, OPT_VERSION},
    {"itemstore", required_argument, 0, 'i'},
    {"log", optional_argument, 0, 'l'},
    {"input", required_argument, 0, 'n'},
    {"object", required_argument, 0, 'o'},
    {"port", optional_argument, 0, 'p'},
    {"srcroot", required_argument, 0, 's'},
    {"strict-validation", no_argument, 0, OPT_STRICT_VALIDATION},
    {"strict-runtime-contracts", no_argument, 0, OPT_STRICT_RUNTIME_CONTRACTS},
    {"quiet", no_argument, 0, 'q'},
    {"verbose", no_argument, 0, 'v'},
    {NULL, 0, 0, '\0'}
  };
  opterr = 0;
  optind = 1;
  while ((opt = getopt_long(argc, argv, "d:hi:l::n:o:p:s:qv", options, NULL)) != -1) {
    switch(opt) {
      case 'q': log_set_level(LOG_LEVEL_QUIET); break;
      case 'v': log_set_level(LOG_LEVEL_VERBOSE); break;
      case OPT_LOADONLY: startup->loadonly = true; break;
      case 'd':
        if (strcmp(optarg, "full") == 0) {
          config.itemstore_durability = ITEMSTORE_DURABLE_FULL;
        } else if (strcmp(optarg, "fast") == 0) {
          config.itemstore_durability = ITEMSTORE_DURABLE_FAST;
        } else {
          logerr("Invalid itemstore durability '%s'; expected full or fast.\n", optarg);
          return EXIT_FAILURE;
        }
        break;
      case 'h': usage(); exit(EXIT_SUCCESS);
      case OPT_VERSION:
        printf("sin %s\n", SINVERSION);
        exit(EXIT_SUCCESS);
      case 'i':
        free(config.itemstore);
        config.itemstore = strdup(optarg);
        config.itemroot = load_or_create_itemstore_with_options(config.itemstore,
            config.strict_validation);
        if (!config.itemroot) return EXIT_FAILURE;
        break;
      case 'l':
        if (optarg == NULL && optind < argc && argv[optind][0] != '-') optarg = argv[optind++];
        log_to_file(optarg != NULL ? optarg : "sin");
        break;
      case 'n': {
        if (!config.itemroot) {
          logerr("If -n option is given, -i option must be given first.\n");
          return EXIT_FAILURE;
        }
        ITEM_t *input_item = find_item(config.itemroot, optarg);
        if (!input_item || input_item->type != ITEM_code) {
          logerr("Item `%s` does not exist, or is not a code item.\n", optarg);
          return EXIT_FAILURE;
        }
        if (!set_config_input_name(optarg)) {
          logerr("Unable to allocate input item names for `%s`.\n", optarg);
          return EXIT_FAILURE;
        }
        break;
      }
      case 'o': {
        uint8_t *new_bytecode = NULL;
        size_t new_filesize = 0;
        CliIoStatus io_status = cli_io_read_file_bytes(optarg, &new_bytecode, &new_filesize);
        if (io_status.code != CLI_IO_OK) {
          logerr("Unable to read object file '%s': %s\n", optarg, cli_io_status_detail(io_status));
          free(new_bytecode);
          return EXIT_FAILURE;
        }
        if (new_filesize > UINT32_MAX) {
          logerr("Input file is too large to interpret: %s\n", optarg);
          free(new_bytecode);
          return EXIT_FAILURE;
        }
        free(startup->bytecode);
        startup->bytecode = new_bytecode;
        startup->filesize = new_filesize;
        logverbose("Bytecode loaded: %zu bytes from %s.\n", startup->filesize, optarg);
        break;
      }
      case 'p': startup->listener_port = atoi(optarg); break;
      case 's': free(config.srcroot); config.srcroot = strdup(optarg); break;
      case OPT_STRICT_VALIDATION: config.strict_validation = true; break;
      case OPT_STRICT_RUNTIME_CONTRACTS: config.strict_runtime_contracts = true; break;
      default:
        usage_error("invalid option");
        return EXIT_FAILURE;
    }
  }
  if (optind < argc) {
    usage_error("unexpected positional arguments");
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}

static int ensure_source_root(void) {
  if (!config.srcroot) {
    config.srcroot = strdup("srcroot");
    struct stat s;
    int err = stat(config.srcroot, &s);
    if (err == -1) {
      mkdir(config.srcroot, 0777);
      logstatus("Creating new source root in current directory.\n");
    } else if(!S_ISDIR(s.st_mode)) {
      logerr("./%s exists but it is not a directory.\n", config.srcroot);
      return EXIT_FAILURE;
    }
  } else {
    struct stat s;
    int err = stat(config.srcroot, &s);
    if (err == -1) {
      logerr("Directory %s does not exist.\n", config.srcroot);
      return EXIT_FAILURE;
    } else if(!S_ISDIR(s.st_mode)) {
      logerr("./%s exists but it is not a directory.\n", config.srcroot);
      return EXIT_FAILURE;
    } else if (access(config.srcroot, W_OK) != 0) {
      logerr("./%s exists, but it is not writable.\n", config.srcroot);
      return EXIT_FAILURE;
    }
  }
  logstatus("Using '%s' as the source root.\n", config.srcroot);
  return EXIT_SUCCESS;
}

static int ensure_itemstore(void) {
  if (!config.itemroot) {
    config.itemstore = strdup("items.dat");
    config.itemroot = load_or_create_itemstore_with_options(config.itemstore,
            config.strict_validation);
    if (!config.itemroot) return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}

static void log_interpreter_return(VALUE_t ret) {
  if (ret.type == VALUE_int) {
    logstatus("Bytecode interpreter returned: %ld\n", ret.i);
  } else if (ret.type == VALUE_str) {
    logstatus("Bytecode interpreter returned: %s\n", ret.s);
    free(ret.s);
  } else if (ret.type == VALUE_float) {
    char fbuffer[64];
    if (sin_format_binary64_buf(ret.f, fbuffer, sizeof(fbuffer))) {
      logstatus("Bytecode interpreter returned: %s\n", fbuffer);
    } else {
      logstatus("Bytecode interpreter returned: <float-format-error>\n");
    }
  } else if (ret.type == VALUE_bool) {
    logstatus("Bytecode interpreter returned: %s\n", ret.i?"true":"false");
  } else if (ret.type == VALUE_nil) {
    logstatus("Bytecode interpreter returned nil.\n");
  } else {
    logerr("Interpreter returned unknown value type: '%c'.\n", ret.type);
  }
}

static int run_boot_item(const SinStartupOptions *startup) {
  config.vm = make_vm();
  RuntimeContext boot_ctx;
  if (ensure_itemstore() != EXIT_SUCCESS) return EXIT_FAILURE;

  ITEM_t *boot = make_root_item("boot");
  boot->type = ITEM_code;
  boot->bytecode = startup->bytecode;
  boot->bytecode_len = (uint32_t)startup->filesize;

  config.loop = malloc(sizeof *config.loop);
  uv_loop_init(config.loop);
  runtime_context_from_config(&boot_ctx, config.vm);
  if (!boot_ctx.initialized) return EXIT_FAILURE;

  if (setjmp(recovery) == 0) {
    logstatus("Setting up error handler.\n");
  } else {
    logerr("SIGUSR1 received.  Restarting boot item.\n");
    logerr("Destroying and recreating all stacks.\n");
    destroy_vm(config.vm);
    config.vm = make_vm();
    runtime_context_from_config(&boot_ctx, config.vm);
  }

  log_interpreter_return(interpret(&boot_ctx, boot));
  runtime_destroy(&boot_ctx);
  destroy_vm(config.vm);
  destroy_item(boot);
  return EXIT_SUCCESS;
}

static int run_network_loop(int listener_port) {
  uv_idle_t input_task;
  RuntimeContext input_ctx = {0};

  logstatus("Using `%s` as the input item.\n", config.input);
  config.input_vm = make_vm();
  config.maxconns = MAXCONNS;
  config.lastconn = config.maxconns;
  uv_tcp_t listener;
  NetworkRuntimeDeps network_deps = {
    .loop = config.loop,
    .listener = &listener,
    .lines = &line,
    .maxconns = config.maxconns
  };
  runtime_context_from_config(&input_ctx, config.input_vm);
  uv_idle_init(config.loop, &input_task);
  input_task.data = &input_ctx;
  uv_idle_start(&input_task, input_processor);

  logstatus("Running...\n");
  if (!validate_network_deps(&network_deps)) return EXIT_FAILURE;
  init_networking_with_deps(&network_deps);
  if (listener_port < 0) {
    logerr("Listener port must be non-negative.\n");
    return EXIT_FAILURE;
  }
  init_listener_with_deps(&network_deps, (uint32_t)listener_port);
  int runloop_retval = uv_run(config.loop, UV_RUN_DEFAULT);
  if (config.shutdown_requested) runloop_retval = 0;

  shutdown_listener_with_deps(&network_deps);
  uv_idle_stop(&input_task);
  uv_walk(config.loop, close_all_tasks, NULL);
  while (uv_run(config.loop, UV_RUN_DEFAULT) != 0) {
    // Drain close callbacks before freeing task and network state.
  }
  finalise_tasks();
  shutdown_networking();
  runtime_destroy(&input_ctx);
  destroy_vm(config.input_vm);
  return runloop_retval;
}

static int shutdown_runtime(bool loadonly, int runloop_retval) {
  logmsg("Shutting down.\n");
  (void)loadonly;
  uv_loop_close(config.loop);
  if (config.safe_shutdown) {
    if (!save_itemstore_with_options(config.itemstore, config.itemroot,
                                     config.itemstore_durability)) {
      logerr("Shutdown could not persist itemstore '%s'.\n", config.itemstore);
      if (runloop_retval == 0) runloop_retval = EXIT_FAILURE;
    }
  }
  free(config.loop);
  free(config.itemstore);
  free(config.srcroot);
  free(config.input);
  free(config.inputline);
  free(config.inputtext);
  destroy_item(config.itemroot);
  close_log();
  return runloop_retval;
}

int main(int argc, char **argv) {
  SinStartupOptions startup;

  if (argc < 2) {
    usage_error("missing object file");
    exit(EXIT_FAILURE);
  }

  if (init_default_config(argc, argv, &startup) != EXIT_SUCCESS) return EXIT_FAILURE;
  init_signal_handler();

  if (parse_sin_options(argc, argv, &startup) != EXIT_SUCCESS) return EXIT_FAILURE;
  if (ensure_source_root() != EXIT_SUCCESS) return EXIT_FAILURE;

  logverbose("Runtime options: loadonly=%d strict_validation=%d strict_runtime_contracts=%d.\n",
             startup.loadonly, config.strict_validation, config.strict_runtime_contracts);

  if (!startup.bytecode) {
    usage_error("missing object file");
    exit(EXIT_FAILURE);
  }

  if (run_boot_item(&startup) != EXIT_SUCCESS) return EXIT_FAILURE;

  int runloop_retval = 0;
  if (!startup.loadonly) runloop_retval = run_network_loop(startup.listener_port);

  return shutdown_runtime(startup.loadonly, runloop_retval);
}
