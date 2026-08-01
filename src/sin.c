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
#include <errno.h>
#include <ctype.h>
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
#include "runtime_value.h"
#include "bytecode_format.h"

static volatile sig_atomic_t recovery_pending;
static bool runtime_signal_shutdown;

// The configuration object - for passing interesting data around globally.
CONFIG_t config;

static bool runtime_context_from_config(RuntimeContext *ctx, VM_t *vm) {
  runtime_context_init(ctx, vm);
  ctx->itemstore = config.itemstore_ctx;
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
  ctx->interrupt_pending = &recovery_pending;
  ctx->signal_shutdown_requested = &runtime_signal_shutdown;
  if (runtime_init(ctx, vm)) return true;
  runtime_destroy(ctx);
  return false;
}

void close_all_tasks(uv_handle_t* handle, void* arg) {
  (void)arg;
  if (!uv_is_closing(handle)) { //FALSE, handle is closing
    uv_close(handle, NULL);
  }
}

void handle_sigusr1(int sig) {
  (void)sig;
  recovery_pending = 1;
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
  printf(" -q, --quiet\t\tSuppress progress/status messages.\n");
  printf(" -v, --verbose\t\tPrint verbose diagnostic traces.\n");
}

static void usage_error(const char *message) {
  logerr("sin: %s\n", message);
  logerr("Try 'sin --help' for more information.\n");
}

static ITEMSTORE_t *load_or_create_itemstore_with_options(const char *filename,
                                                 bool strict_validation) {
  struct stat buffer;
  if (stat(filename, &buffer) == 0) {
    logmsg("Loading itemstore from %s.\n", filename);
    ITEMSTORE_t *store = itemstore_load_with_options(filename, strict_validation);
    if (!store) {
      logerr("Existing itemstore '%s' could not be loaded; refusing to "
             "replace it.\n", filename);
    }
    return store;
  }

  if (errno != ENOENT) {
    logerr("Unable to inspect itemstore '%s': %s\n", filename,
           strerror(errno));
    return NULL;
  }

  logmsg("Creating a new itemstore, which will be saved as %s.\n", filename);
  return itemstore_create("root");
}

static bool flag_requested(int argc, char **argv, const char *flag) {
  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], flag) == 0) return true;
  }
  return false;
}

typedef struct SinStartupOptions {
  size_t filesize;
  uint16_t listener_port;
  uint8_t *bytecode;
  bool loadonly;
} SinStartupOptions;

// One net.input event is processed per callback. This interval keeps queued
// events responsive without keeping the loop in an always-active idle phase.
#define INPUT_SCHEDULER_INTERVAL_MS 10u

typedef struct SinStartupState {
  bool loop_initialized;
  bool tasks_initialized;
  bool input_context_initialized;
  bool input_vm_initialized;
  bool input_task_initialized;
  bool input_task_started;
  bool networking_initialized;
  bool log_redirected;
  bool loop_storage_retained;
  NetworkRuntimeDeps network_deps;
  uv_tcp_t listener;
  uv_tcp_t listener_ipv4;
  uv_timer_t input_task;
  RuntimeContext input_ctx;
  bool boot_context_initialized;
  bool boot_vm_initialized;
  bool boot_completed;
  RuntimeContext boot_ctx;
  ITEMSTORE_t *boot_store;
  ITEM_t *boot_item;
} SinStartupState;

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
  memset(&config, 0, sizeof(config));
  startup->filesize = 0;
  startup->listener_port = LISTENER_PORT;
  startup->bytecode = NULL;
  startup->loadonly = false;

  config.itemstore_ctx = NULL;
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

static bool init_signal_handler(void) {
  init_errmsg();
  struct sigaction act;
  act.sa_handler = handle_sigusr1;
  sigemptyset(&act.sa_mask);
  act.sa_flags = 0;
  if (sigaction(SIGUSR1, &act, NULL) < 0) {
    logerr("Unable to install signal handler.\n");
    return false;
  }
  return true;
}

static bool parse_listener_port(const char *text, uint16_t *port) {
  if (!text || !*text || !port || text[0] == '+' || text[0] == '-') {
    return false;
  }
  for (const unsigned char *p = (const unsigned char *)text; *p; p++) {
    if (!isdigit(*p)) return false;
  }

  errno = 0;
  char *end = NULL;
  unsigned long parsed = strtoul(text, &end, 10);
  if (errno == ERANGE || end == text || *end != '\0' ||
      parsed > UINT16_MAX) {
    return false;
  }
  *port = (uint16_t)parsed;
  return true;
}

typedef enum SinParseResult {
  SIN_PARSE_OK = 0,
  SIN_PARSE_FAILURE = 1,
  SIN_PARSE_EXIT_SUCCESS = 2
} SinParseResult;

static SinParseResult parse_sin_options(int argc, char **argv,
                                        SinStartupOptions *startup,
                                        SinStartupState *state) {
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
    {"port", required_argument, 0, 'p'},
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
      case 'h':
        usage();
        return SIN_PARSE_EXIT_SUCCESS;
      case OPT_VERSION:
        printf("sin %s\n", SINVERSION);
        return SIN_PARSE_EXIT_SUCCESS;
      case 'i':
        itemstore_destroy(config.itemstore_ctx);
        config.itemstore_ctx = NULL;
        free(config.itemstore);
        config.itemstore = strdup(optarg);
        if (!config.itemstore) {
          logerr("Unable to allocate itemstore filename.\n");
          return EXIT_FAILURE;
        }
        config.itemstore_ctx = load_or_create_itemstore_with_options(config.itemstore,
            config.strict_validation);
        if (!config.itemstore_ctx) return EXIT_FAILURE;
        break;
      case 'l':
        if (optarg == NULL && optind < argc && argv[optind][0] != '-') optarg = argv[optind++];
        state->log_redirected = true;
        log_to_file(optarg != NULL ? optarg : "sin");
        break;
      case 'n': {
        if (!config.itemstore_ctx) {
          logerr("If -n option is given, -i option must be given first.\n");
          return EXIT_FAILURE;
        }
        ITEM_t *input_item = find_item(itemstore_root(config.itemstore_ctx), optarg);
        if (!input_item || item_kind(input_item) != ITEM_code) {
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
        BC_FormatHeader header;
        bc_decode_header(new_bytecode, (uint32_t)new_filesize, &header);
        if (header.legacy) {
          logerr("Bootstrap object '%s' is unversioned; recompile with scomp.\n",
                 optarg);
          free(new_bytecode);
          return EXIT_FAILURE;
        }
        free(startup->bytecode);
        startup->bytecode = new_bytecode;
        startup->filesize = new_filesize;
        logmsg("Bytecode loaded: %zu bytes from %s.\n", startup->filesize, optarg);
        break;
      }
      case 'p':
        if (!parse_listener_port(optarg, &startup->listener_port)) {
          logerr("Invalid listener port '%s'; expected an integer from 0 to 65535.\n",
                 optarg ? optarg : "");
          return EXIT_FAILURE;
        }
        break;
      case 's':
        free(config.srcroot);
        config.srcroot = strdup(optarg);
        if (!config.srcroot) {
          logerr("Unable to allocate source root name.\n");
          return EXIT_FAILURE;
        }
        break;
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
  return SIN_PARSE_OK;
}

static int ensure_source_root(void) {
  if (!config.srcroot) {
    config.srcroot = strdup("srcroot");
    if (!config.srcroot) {
      logerr("Unable to allocate source root name.\n");
      return EXIT_FAILURE;
    }
    struct stat s;
    int err = stat(config.srcroot, &s);
    if (err == -1) {
      mkdir(config.srcroot, 0777);
      logmsg("Creating new source root in current directory.\n");
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
  logmsg("Using '%s' as the source root.\n", config.srcroot);
  return EXIT_SUCCESS;
}

static int ensure_itemstore(void) {
  if (!config.itemstore_ctx) {
    config.itemstore = strdup("items.dat");
    if (!config.itemstore) {
      logerr("Unable to allocate itemstore filename.\n");
      return EXIT_FAILURE;
    }
    config.itemstore_ctx = load_or_create_itemstore_with_options(config.itemstore,
            config.strict_validation);
    if (!config.itemstore_ctx) return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}

static void log_interpreter_return(VALUE_t ret) {
  if (ret.type == VALUE_int) {
    logverbose("Bytecode interpreter returned: %ld\n", ret.i);
  } else if (ret.type == VALUE_str) {
    logverbose("Bytecode interpreter returned: %s\n", ret.s);
    free_runtime_string(ret.s);
  } else if (ret.type == VALUE_float) {
    char fbuffer[64];
    if (sin_format_binary64_buf(ret.f, fbuffer, sizeof(fbuffer))) {
      logverbose("Bytecode interpreter returned: %s\n", fbuffer);
    } else {
      logverbose("Bytecode interpreter returned: <float-format-error>\n");
    }
  } else if (ret.type == VALUE_bool) {
    logverbose("Bytecode interpreter returned: %s\n", ret.i?"true":"false");
  } else if (ret.type == VALUE_nil) {
    logverbose("Bytecode interpreter returned nil.\n");
  } else {
    logerr("Interpreter returned unknown value type: '%c'.\n", ret.type);
  }
}

static void destroy_boot_runtime(SinStartupState *state, bool destroy_boot_item) {
  if (state->boot_context_initialized) {
    runtime_destroy(&state->boot_ctx);
    state->boot_context_initialized = false;
  }
  if (state->boot_vm_initialized) {
    VM_t *vm = config.vm;
    config.vm = NULL;
    state->boot_vm_initialized = false;
    destroy_vm(vm);
  }
  if (destroy_boot_item && state->boot_item) {
    ITEMSTORE_t *boot_store = state->boot_store;
    state->boot_store = NULL;
    state->boot_item = NULL;
    itemstore_destroy(boot_store);
  }
}

static int run_boot_item(SinStartupOptions *startup, SinStartupState *state) {
restart_boot:
  if (recovery_pending) {
    recovery_pending = 0;
    logerr("SIGUSR1 received.  Restarting boot item.\n");
    logerr("Destroying and recreating all stacks.\n");
    destroy_boot_runtime(state, false);
  }
  if (ensure_itemstore() != EXIT_SUCCESS) goto boot_failure;
  if (!state->boot_item) {
    state->boot_store = itemstore_create_boot("boot", startup->bytecode,
                                              (uint32_t)startup->filesize);
    if (!state->boot_store) {
      logerr("Unable to allocate boot item.\n");
      goto boot_failure;
    }
    state->boot_item = itemstore_root(state->boot_store);
    startup->bytecode = NULL;
  }
  if (!state->boot_vm_initialized) {
    config.vm = make_vm();
    if (!config.vm) {
      logerr("Unable to allocate boot VM.\n");
      goto boot_failure;
    }
    state->boot_vm_initialized = true;
  }

  if (!state->loop_initialized) {
    config.loop = malloc(sizeof *config.loop);
    if (!config.loop || uv_loop_init(config.loop) != 0) {
      logerr("Unable to initialize the runtime event loop.\n");
      free(config.loop);
      config.loop = NULL;
      goto boot_failure;
    }
    state->loop_initialized = true;
  }
  if (!state->boot_context_initialized) {
    runtime_context_init(&state->boot_ctx, config.vm);
    state->boot_context_initialized = true;
    if (!runtime_context_from_config(&state->boot_ctx, config.vm)) {
      logerr("Unable to initialize the boot runtime context.\n");
      goto boot_failure;
    }
  }

  logverbose("Setting up error handler.\n");
  log_interpreter_return(interpret(&state->boot_ctx, state->boot_item));
  if (state->boot_ctx.interrupted) {
    logerr("SIGUSR1 received.  Restarting boot item.\n");
    logerr("Destroying and recreating all stacks.\n");
    destroy_boot_runtime(state, false);
    goto restart_boot;
  }
  state->boot_completed = true;
  destroy_boot_runtime(state, true);
  return EXIT_SUCCESS;

boot_failure:
  destroy_boot_runtime(state, true);
  return EXIT_FAILURE;
}

static int run_network_loop(uint16_t listener_port, SinStartupState *state) {

  logmsg("Using `%s` as the input item.\n", config.input);
  config.input_vm = make_vm();
  if (!config.input_vm) {
    logerr("Unable to allocate input VM.\n");
    return EXIT_FAILURE;
  }
  state->input_vm_initialized = true;
  config.maxconns = MAXCONNS;
  config.lastconn = config.maxconns;
  state->network_deps = (NetworkRuntimeDeps){
    .loop = config.loop,
    .listener = &state->listener,
    .listener_ipv4 = &state->listener_ipv4,
    .lines = &line,
    .maxconns = config.maxconns
  };
  runtime_context_init(&state->input_ctx, config.input_vm);
  state->input_context_initialized = true;
  if (!runtime_context_from_config(&state->input_ctx, config.input_vm)) {
    logerr("Unable to initialize the input runtime context.\n");
    return EXIT_FAILURE;
  }

  state->networking_initialized = true;
  if (!init_networking_with_deps(&state->network_deps)) {
    return EXIT_FAILURE;
  }
  if (!init_listener_with_deps(&state->network_deps, listener_port)) {
    return EXIT_FAILURE;
  }
  if (uv_timer_init(config.loop, &state->input_task) != 0) {
    logerr("Unable to initialize the input scheduler timer.\n");
    return EXIT_FAILURE;
  }
  state->input_task_initialized = true;
  state->input_task.data = &state->input_ctx;
  if (uv_timer_start(&state->input_task, input_processor, 0,
                    INPUT_SCHEDULER_INTERVAL_MS) != 0) {
    logerr("Unable to start the input scheduler timer.\n");
    return EXIT_FAILURE;
  }
  state->input_task_started = true;

  logmsg("Running...\n");
  int runloop_retval = uv_run(config.loop, UV_RUN_DEFAULT);
  if (runtime_signal_shutdown) return EXIT_FAILURE;
  if (config.shutdown_requested) runloop_retval = 0;
  return runloop_retval;
}

static int run_startup_with_recovery(SinStartupOptions *startup,
                                     SinStartupState *state) {
  init_tasks();
  state->tasks_initialized = true;
  if (run_boot_item(startup, state) != EXIT_SUCCESS) {
    return EXIT_FAILURE;
  }
  if (recovery_pending) {
    recovery_pending = 0;
    config.safe_shutdown = false;
    logerr("SIGUSR1 received during runtime; shutting down.\n");
    return EXIT_FAILURE;
  }
  if (startup->loadonly) {
    return EXIT_SUCCESS;
  }

  int runloop_retval = run_network_loop(startup->listener_port, state);
  return runloop_retval;
}

static int shutdown_startup(SinStartupState *state, SinStartupOptions *startup,
                            bool persist_itemstore, int runloop_retval) {
  if (persist_itemstore) logmsg("Shutting down.\n");
  if (state->input_task_started) {
    uv_timer_stop(&state->input_task);
    state->input_task_started = false;
  }
  if (state->networking_initialized) {
    shutdown_listener_with_deps(&state->network_deps);
  }
  if (state->tasks_initialized) {
    finalise_tasks(state->loop_initialized ? config.loop : NULL);
    state->tasks_initialized = false;
  }
  if (state->input_task_initialized && !uv_is_closing(
          (uv_handle_t *)&state->input_task)) {
    uv_close((uv_handle_t *)&state->input_task, NULL);
    state->input_task_initialized = false;
  }
  if (state->loop_initialized) {
    uv_walk(config.loop, close_all_tasks, NULL);
    while (uv_run(config.loop, UV_RUN_DEFAULT) != 0) {
      // Drain close callbacks before freeing task and network state.
    }
  }
  if (state->networking_initialized) {
    shutdown_networking();
    state->networking_initialized = false;
  }
  if (state->input_context_initialized) {
    runtime_destroy(&state->input_ctx);
    state->input_context_initialized = false;
  }
  if (state->input_vm_initialized) {
    destroy_vm(config.input_vm);
    config.input_vm = NULL;
    state->input_vm_initialized = false;
  }
  if (state->loop_initialized) {
    int loop_result = uv_loop_close(config.loop);
    if (loop_result != 0) {
      logerr("Unable to close the runtime event loop: %s\n",
             uv_strerror(loop_result));
      if (runloop_retval == 0) runloop_retval = EXIT_FAILURE;
      /* Keep the loop allocation alive until process exit. A failed close
       * means libuv still owns state reachable through this storage. */
      state->loop_storage_retained = true;
      state->loop_initialized = false;
    } else {
      free(config.loop);
      config.loop = NULL;
      state->loop_initialized = false;
    }
  }
  if (persist_itemstore && config.safe_shutdown) {
    if (!itemstore_save_with_options(config.itemstore, config.itemstore_ctx,
                                     config.itemstore_durability)) {
      logerr("Shutdown could not persist itemstore '%s'.\n", config.itemstore);
      if (runloop_retval == 0) runloop_retval = EXIT_FAILURE;
    }
  }
  destroy_boot_runtime(state, true);
  destroy_vm(config.vm);
  config.vm = NULL;
  free(startup->bytecode);
  startup->bytecode = NULL;
  free(config.itemstore);
  free(config.srcroot);
  free(config.input);
  free(config.inputline);
  free(config.inputtext);
  itemstore_destroy(config.itemstore_ctx);
  config.itemstore_ctx = NULL;
  if (persist_itemstore || state->log_redirected) close_log();
  return runloop_retval;
}

int main(int argc, char **argv) {
  SinStartupOptions startup;
  SinStartupState state = {0};
  int result = EXIT_FAILURE;
  bool persist_itemstore = false;

  if (argc < 2) {
    usage_error("missing object file");
    exit(EXIT_FAILURE);
  }

  if (init_default_config(argc, argv, &startup) != EXIT_SUCCESS) goto cleanup;
  if (!init_signal_handler()) goto cleanup;

  SinParseResult parse_result = parse_sin_options(argc, argv, &startup, &state);
  if (parse_result == SIN_PARSE_EXIT_SUCCESS) {
    result = EXIT_SUCCESS;
    goto cleanup;
  }
  if (parse_result != SIN_PARSE_OK) goto cleanup;
  if (ensure_source_root() != EXIT_SUCCESS) goto cleanup;

  logmsg("Runtime options: loadonly=%d strict_validation=%d strict_runtime_contracts=%d.\n",
             startup.loadonly, config.strict_validation, config.strict_runtime_contracts);

  if (!startup.bytecode) {
    usage_error("missing object file");
    goto cleanup;
  }

  int runloop_retval = run_startup_with_recovery(&startup, &state);
  persist_itemstore = state.boot_completed;

  result = shutdown_startup(&state, &startup, persist_itemstore,
                            runloop_retval);
  return result;

cleanup:
  return shutdown_startup(&state, &startup, persist_itemstore, result);
}
