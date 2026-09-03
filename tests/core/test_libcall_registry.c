#include "item.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <glob.h>
#include <unistd.h>
#include <stdint.h>

#include "libcall.h"
#include "config.h"
#include "compiler/compiler_pipeline.h"
#include "error.h"
#include "interpret.h"
#include "log.h"
#include "item.h"
#include "test_assert.h"
#include "test_helpers.h"
#include "task.h"
#include "vm.h"
#include "memory.h"
#include "runtime_value.h"
#include "string_limits.h"
#include "version.h"

#include "list.h"
#include "itemref.h"

uint8_t *lc_task_newgametask(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item);
uint8_t *lc_list_length(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item);
uint8_t *lc_list_get(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item);
uint8_t *lc_list_append(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item);
uint8_t *lc_list_set(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item);
uint8_t *lc_list_concat(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item);
uint8_t *lc_list_slice(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item);
uint8_t *lc_list_islist(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item);
uint8_t *lc_math_abs(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item);
uint8_t *lc_math_min(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item);
uint8_t *lc_math_max(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item);
uint8_t *lc_math_floor(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item);
uint8_t *lc_math_ceil(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item);
uint8_t *lc_math_round(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item);
uint8_t *lc_math_sqrt(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item);
uint8_t *lc_math_pow(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item);
uint8_t *lc_math_log(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item);
uint8_t *lc_math_log2(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item);
uint8_t *lc_math_log10(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item);
uint8_t *lc_math_exp(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item);
uint8_t *lc_task_killtask(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item);
uint8_t *lc_task_thisid(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item);
uint8_t *lc_task_exists(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item);
uint8_t *lc_task_count(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item);
void execute_task_cb(uv_timer_t *req);
uint8_t *lc_net_write(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item);
uint8_t *lc_net_input(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item);
uint8_t *lc_net_flush(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item);
uint8_t *lc_net_ditch(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item);
uint8_t *lc_net_echo(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item);
uint8_t *lc_net_maxlines(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item);
uint8_t *lc_net_connected(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item);
uint8_t *lc_net_address(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item);
uint8_t *lc_sys_log(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item);
uint8_t *lc_sys_backup(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item);
uint8_t *lc_sys_shutdown(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item);
uint8_t *lc_sys_abort(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item);
uint8_t *lc_sys_save(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item);
uint8_t *lc_sys_thisitem(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item);
uint8_t *lc_sys_parentitem(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item);
uint8_t *lc_sys_itemtype(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item);
uint8_t *lc_sys_childcount(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item);
uint8_t *lc_sys_rootcount(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item);
uint8_t *lc_sys_version(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item);
uint8_t *lc_sys_now(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item);
uint8_t *lc_sys_monotime(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item);
uint8_t *lc_sys_calleritem(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item);
uint8_t *lc_sys_paramcount(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item);
uint8_t *lc_sys_source(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item);
uint8_t *lc_sys_itemref(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item);
uint8_t *lc_sys_itemname(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item);
uint8_t *lc_sys_fetch(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item);
uint8_t *lc_sys_call(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item);
int64_t lc_sys_wall_milliseconds(int64_t seconds, int64_t microseconds);
uint8_t *lc_sys_compile(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item);
uint8_t *lc_sys_exists(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item);
uint8_t *lc_sys_delete(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item);
uint8_t *lc_sys_nthname(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item);
uint8_t *lc_sys_rootname(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item);
uint8_t *lc_str_capitalise(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item);
uint8_t *lc_str_upper(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item);
uint8_t *lc_str_lower(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item);
uint8_t *lc_str_len(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item);
uint8_t *lc_str_valtostr(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item);
uint8_t *lc_str_trim(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item);
uint8_t *lc_str_ltrim(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item);
uint8_t *lc_str_rtrim(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item);
uint8_t *lc_str_substr(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item);
uint8_t *lc_str_find(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item);
uint8_t *lc_str_contains(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item);
uint8_t *lc_str_startswith(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item);
uint8_t *lc_str_endswith(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item);
uint8_t *lc_str_eqcasei(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item);
uint8_t *lc_str_replace(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item);
uint8_t *lc_str_repeat(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item);
uint8_t *lc_str_padleft(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item);
uint8_t *lc_str_padright(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item);

extern CONFIG_t config;

#include "shared/test_libcall_support.h"

static void assert_logging_controls_and_redirection(void) {
  char base_path[96];
  char log_path[104];
  char err_path[104];
  ASSERT_EQ_INT(0, test_make_temp_path("logging-contract", base_path,
                                       sizeof(base_path)));
  ASSERT_TRUE(snprintf(log_path, sizeof(log_path), "%s.log", base_path) > 0);
  ASSERT_TRUE(snprintf(err_path, sizeof(err_path), "%s.err", base_path) > 0);

  int saved_stdout = dup(STDOUT_FILENO);
  int saved_stderr = dup(STDERR_FILENO);
  ASSERT_TRUE(saved_stdout >= 0);
  ASSERT_TRUE(saved_stderr >= 0);

  LogLevel saved_level = log_get_level();
  bool redirected = log_to_file(base_path);
  if (redirected) {
    log_set_level(LOG_LEVEL_QUIET);
    ASSERT_EQ_INT(LOG_LEVEL_QUIET, log_get_level());
    ASSERT_TRUE(!log_is_verbose());
    logmsg("quiet-marker\n");
    logverbose("quiet-verbose-marker\n");

    log_set_level(LOG_LEVEL_VERBOSE);
    ASSERT_EQ_INT(LOG_LEVEL_VERBOSE, log_get_level());
    ASSERT_TRUE(log_is_verbose());
    logmsg("normal-marker\n");
    logverbose("verbose-marker\n");
    logerr("error-marker\n");
  }

  fflush(stdout);
  fflush(stderr);
  ASSERT_TRUE(dup2(saved_stdout, STDOUT_FILENO) >= 0);
  ASSERT_TRUE(dup2(saved_stderr, STDERR_FILENO) >= 0);
  close(saved_stdout);
  close(saved_stderr);
  log_set_level(saved_level);

  ASSERT_TRUE(redirected);

  FILE *capture = fopen(log_path, "rb");
  ASSERT_NOT_NULL(capture);
  char buffer[256] = {0};
  size_t n = fread(buffer, 1, sizeof(buffer) - 1, capture);
  buffer[n] = '\0';
  ASSERT_EQ_INT(0, fclose(capture));
  ASSERT_TRUE(strstr(buffer, "normal-marker\n") != NULL);
  ASSERT_TRUE(strstr(buffer, "quiet-marker\n") == NULL);

  capture = fopen(err_path, "rb");
  ASSERT_NOT_NULL(capture);
  memset(buffer, 0, sizeof(buffer));
  n = fread(buffer, 1, sizeof(buffer) - 1, capture);
  buffer[n] = '\0';
  ASSERT_EQ_INT(0, fclose(capture));
  ASSERT_TRUE(strstr(buffer, "verbose-marker\n") != NULL);
  ASSERT_TRUE(strstr(buffer, "error-marker\n") != NULL);
  ASSERT_TRUE(strstr(buffer, "quiet-verbose-marker\n") == NULL);

  ASSERT_EQ_INT(0, unlink(log_path));
  ASSERT_EQ_INT(0, unlink(err_path));
}

static void assert_sys_log_output(VALUE_t out, const char *expected) {
  FILE *capture = tmpfile();
  ASSERT_NOT_NULL(capture);
  int saved_stdout = dup(STDOUT_FILENO);
  ASSERT_TRUE(saved_stdout >= 0);
  ASSERT_TRUE(dup2(fileno(capture), STDOUT_FILENO) >= 0);

  push_stack(config.vm->stack, out);
  (void)lc_sys_log(test_ctx(), NULL, itemstore_root(config.itemstore_ctx));
  VALUE_t ret = pop_stack(config.vm->stack);
  ASSERT_EQ_INT(VALUE_nil, ret.type);
  fflush(stdout);

  ASSERT_TRUE(dup2(saved_stdout, STDOUT_FILENO) >= 0);
  close(saved_stdout);
  rewind(capture);
  char buffer[128] = {0};
  size_t n = fread(buffer, 1, sizeof(buffer) - 1, capture);
  buffer[n] = '\0';
  ASSERT_TRUE(strcmp(buffer, expected) == 0);
  fclose(capture);
}

static void assert_net_write_output(VALUE_t out, const char *expected) {
  push_stack(config.vm->stack, (VALUE_t){VALUE_int, {.i = 0}});
  push_stack(config.vm->stack, out);
  (void)lc_net_write(test_ctx(), NULL, itemstore_root(config.itemstore_ctx));
  VALUE_t ret = pop_stack(config.vm->stack);
  ASSERT_EQ_INT(VALUE_nil, ret.type);
  unsigned char output[256] = {0};
  size_t output_len = network_runtime_test_take_output(
      test_network_runtime(), 0, output, sizeof(output));
  ASSERT_EQ_INT(strlen(expected), output_len);
  ASSERT_TRUE(memcmp(output, expected, output_len) == 0);
}

static void assert_net_write_render_failure(VALUE_t out) {
  push_stack(config.vm->stack, (VALUE_t){VALUE_int, {.i = 0}});
  push_stack(config.vm->stack, out);
  (void)lc_net_write(test_ctx(), NULL, itemstore_root(config.itemstore_ctx));
  VALUE_t ret = pop_stack(config.vm->stack);
  ASSERT_EQ_INT(VALUE_bool, ret.type);
  ASSERT_EQ_INT(0, ret.i);
  unsigned char output[8] = {0};
  ASSERT_EQ_INT(0, network_runtime_test_take_output(
                        test_network_runtime(), 0, output, sizeof(output)));
}

static uint8_t *test_noop_libcall(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item) {
  (void)ctx; (void)item;
  return nextop;
}
void test_libcall_registry_roundtrip(void) {
  struct manifest { const char *lib, *call; uint8_t li, ci, args; OP_t fn; };
  static const struct manifest manifest[] = {
      {"sys", "backup", 1, 0, 0, lc_sys_backup},
      {"sys", "log", 1, 1, 1, lc_sys_log},
      {"sys", "shutdown", 1, 2, 0, lc_sys_shutdown},
      {"sys", "abort", 1, 3, 0, lc_sys_abort},
      {"sys", "compile", 1, 4, 1, lc_sys_compile},
      {"sys", "exists", 1, 5, 1, lc_sys_exists},
      {"sys", "delete", 1, 6, 1, lc_sys_delete},
      {"sys", "nthname", 1, 7, 2, lc_sys_nthname},
      {"sys", "rootname", 1, 8, 1, lc_sys_rootname},
      {"sys", "save", 1, 9, 0, lc_sys_save},
      {"sys", "thisitem", 1, 10, 0, lc_sys_thisitem},
      {"sys", "parentitem", 1, 11, 0, lc_sys_parentitem},
      {"sys", "itemtype", 1, 12, 1, lc_sys_itemtype},
      {"sys", "childcount", 1, 13, 1, lc_sys_childcount},
      {"sys", "rootcount", 1, 14, 0, lc_sys_rootcount},
      {"sys", "version", 1, 15, 0, lc_sys_version},
      {"sys", "now", 1, 16, 0, lc_sys_now},
      {"sys", "monotime", 1, 17, 0, lc_sys_monotime},
      {"sys", "calleritem", 1, 18, 0, lc_sys_calleritem},
      {"sys", "paramcount", 1, 19, 1, lc_sys_paramcount},
      {"sys", "source", 1, 20, 1, lc_sys_source},
      {"sys", "itemref", 1, 21, 1, lc_sys_itemref},
      {"sys", "itemname", 1, 22, 1, lc_sys_itemname},
      {"sys", "fetch", 1, 23, 1, lc_sys_fetch},
      {"sys", "call", 1, 24, 2, lc_sys_call},
      {"list", "length", 5, 0, 1, lc_list_length},
      {"list", "get", 5, 1, 2, lc_list_get},
      {"list", "append", 5, 2, 2, lc_list_append},
      {"list", "set", 5, 3, 3, lc_list_set},
      {"list", "concat", 5, 4, 2, lc_list_concat},
      {"list", "slice", 5, 5, 3, lc_list_slice},
      {"list", "islist", 5, 6, 1, lc_list_islist},
      {"math", "abs", 6, 0, 1, lc_math_abs},
      {"math", "min", 6, 1, 2, lc_math_min},
      {"math", "max", 6, 2, 2, lc_math_max},
      {"math", "floor", 6, 3, 1, lc_math_floor},
      {"math", "ceil", 6, 4, 1, lc_math_ceil},
      {"math", "round", 6, 5, 1, lc_math_round},
      {"math", "sqrt", 6, 6, 1, lc_math_sqrt},
      {"math", "pow", 6, 7, 2, lc_math_pow},
      {"math", "log", 6, 8, 1, lc_math_log},
      {"math", "log2", 6, 9, 1, lc_math_log2},
      {"math", "log10", 6, 10, 1, lc_math_log10},
      {"math", "exp", 6, 11, 1, lc_math_exp},
      {"net", "input", 3, 0, 0, lc_net_input},
      {"net", "write", 3, 1, 2, lc_net_write},
      {"net", "ditch", 3, 2, 1, lc_net_ditch},
      {"net", "flush", 3, 3, 1, lc_net_flush},
      {"net", "echo", 3, 4, 1, lc_net_echo},
      {"net", "maxlines", 3, 5, 0, lc_net_maxlines},
      {"net", "connected", 3, 6, 1, lc_net_connected},
      {"net", "address", 3, 7, 1, lc_net_address},
      {"str", "capitalise", 4, 0, 1, lc_str_capitalise},
      {"str", "upper", 4, 1, 1, lc_str_upper},
      {"str", "lower", 4, 2, 1, lc_str_lower},
      {"str", "len", 4, 3, 1, lc_str_len},
      {"str", "trim", 4, 4, 1, lc_str_trim},
      {"str", "ltrim", 4, 5, 1, lc_str_ltrim},
      {"str", "rtrim", 4, 6, 1, lc_str_rtrim},
      {"str", "substr", 4, 7, 3, lc_str_substr},
      {"str", "find", 4, 8, 2, lc_str_find},
      {"str", "contains", 4, 9, 2, lc_str_contains},
      {"str", "startswith", 4, 10, 2, lc_str_startswith},
      {"str", "endswith", 4, 11, 2, lc_str_endswith},
      {"str", "eqcasei", 4, 12, 2, lc_str_eqcasei},
      {"str", "valtostr", 4, 13, 1, lc_str_valtostr},
      {"str", "replace", 4, 14, 3, lc_str_replace},
      {"str", "repeat", 4, 15, 2, lc_str_repeat},
      {"str", "padleft", 4, 16, 2, lc_str_padleft},
      {"str", "padright", 4, 17, 2, lc_str_padright},
      {"task", "newgametask", 2, 0, 3, lc_task_newgametask},
      {"task", "killtask", 2, 1, 1, lc_task_killtask},
      {"task", "thisid", 2, 2, 0, lc_task_thisid},
      {"task", "exists", 2, 3, 1, lc_task_exists},
      {"task", "count", 2, 4, 0, lc_task_count},
      {NULL, NULL, 0, 0, 0, NULL},
  };
  size_t manifest_count = sizeof(manifest) / sizeof(manifest[0]) - 1;
  size_t canonical_count = 0;
  while (libcalls[canonical_count].libname != NULL) canonical_count++;
  ASSERT_EQ_INT(canonical_count, manifest_count);
  uint8_t li = 0, ci = 0, args = 0;
  libcall_reset_registry_for_tests();
  ASSERT_TRUE(libcall_init_registry());
  ASSERT_TRUE(libcall_validate_registry());
  for (size_t i = 0; manifest[i].lib != NULL; i++) {
    li = 0;
    ci = 0;
    args = 0;
    ASSERT_TRUE(libcall_lookup_pair(manifest[i].lib, manifest[i].call, &li, &ci, &args));
    ASSERT_EQ_INT(manifest[i].li, li);
    ASSERT_EQ_INT(manifest[i].ci, ci);
    ASSERT_EQ_INT(manifest[i].args, args);
    ASSERT_TRUE(libcall_func_pair(li, ci) == manifest[i].fn);
    args = 0;
    ASSERT_TRUE(libcall_pair_arg_count(li, ci, &args));
    ASSERT_EQ_INT(manifest[i].args, args);
  }
  ASSERT_TRUE(!libcall_lookup_pair("missing", "missing", &li, &ci, &args));
  static const struct { uint8_t li, ci, args; } sparse = {255, 255, 0};
  ASSERT_TRUE(libcall_func_pair(sparse.li, sparse.ci) == NULL);
  args = sparse.args;
  ASSERT_TRUE(!libcall_pair_arg_count(sparse.li, sparse.ci, &args));
  ASSERT_TRUE(!libcall_pair_arg_count(5, 255, &args));

  /* The shutdown calls are part of the registry contract, but their
   * observable state changes belong to the handler.  Exercise both paths
   * here so registry-only coverage cannot hide a broken shutdown ABI. */
  setup_libcall_runtime();
  RuntimeContext *ctx = test_ctx();
  uv_loop_t loop;
  ASSERT_EQ_INT(0, uv_loop_init(&loop));
  uv_loop_t *saved_loop = ctx->loop;
  bool *saved_safe_shutdown = ctx->safe_shutdown;
  bool *saved_shutdown_requested = ctx->shutdown_requested;
  bool saved_safe_shutdown_value = config.safe_shutdown;
  bool saved_shutdown_requested_value = config.shutdown_requested;
  ctx->loop = &loop;
  ctx->shutdown_requested = &config.shutdown_requested;

  config.safe_shutdown = true;
  config.shutdown_requested = false;
  (void)lc_sys_shutdown(ctx, NULL, itemstore_root(config.itemstore_ctx));
  VALUE_t shutdown_result = pop_stack(config.vm->stack);
  ASSERT_EQ_INT(VALUE_nil, shutdown_result.type);
  ASSERT_TRUE(config.safe_shutdown);
  ASSERT_TRUE(config.shutdown_requested);
  value_free(&shutdown_result);
  (void)uv_run(&loop, UV_RUN_NOWAIT);

  config.safe_shutdown = true;
  config.shutdown_requested = false;
  (void)lc_sys_abort(ctx, NULL, itemstore_root(config.itemstore_ctx));
  VALUE_t abort_result = pop_stack(config.vm->stack);
  ASSERT_EQ_INT(VALUE_nil, abort_result.type);
  ASSERT_TRUE(!config.safe_shutdown);
  ASSERT_TRUE(config.shutdown_requested);
  value_free(&abort_result);
  (void)uv_run(&loop, UV_RUN_NOWAIT);

  ctx->loop = saved_loop;
  ctx->safe_shutdown = saved_safe_shutdown;
  ctx->shutdown_requested = saved_shutdown_requested;
  config.safe_shutdown = saved_safe_shutdown_value;
  config.shutdown_requested = saved_shutdown_requested_value;
  ASSERT_EQ_INT(0, uv_loop_close(&loop));
  teardown_libcall_runtime();

}

void test_runtime_init_validates_libcalls_once(void) {
  long successful_budget = -1;
  for (long fail_at = 0; fail_at < 4096; fail_at++) {
    LibcallRegistry registry = {0};
    alloc_test_fail_after(fail_at);
    bool initialized = libcall_registry_init(&registry);
    alloc_test_fail_after(-1);
    libcall_registry_destroy(&registry);
    if (initialized) {
      successful_budget = fail_at;
      break;
    }
  }
  ASSERT_TRUE(successful_budget >= 0);

  RuntimeContext ctx;
  runtime_context_init(&ctx, NULL);

  alloc_test_fail_after(successful_budget);
  bool initialized = runtime_init(&ctx, NULL);
  alloc_test_fail_after(-1);
  runtime_destroy(&ctx);
  ASSERT_TRUE(initialized);
  ASSERT_TRUE(ctx.initialized == false);
}

void test_interpret_lazy_init_failure_is_transactional(void) {
  setup_libcall_runtime();

  uint8_t bytecode[] = {
    0x00, 0x00, 'p', 7, 0, 0, 0, 0, 0, 0, 0, 'Q', 'h'
  };
  uint8_t *owned_bytecode = malloc(sizeof(bytecode));
  ASSERT_NOT_NULL(owned_bytecode);
  memcpy(owned_bytecode, bytecode, sizeof(bytecode));
  ITEM_t *code = test_item_set_code(itemstore_root(config.itemstore_ctx),
                                    "test.lazy_init_failure",
                                    sizeof(bytecode), owned_bytecode);
  ASSERT_NOT_NULL(code);

  RuntimeContext ctx;
  runtime_context_init(&ctx, config.vm);
  ctx.itemstore = config.itemstore_ctx;
  ITEM_t *saved_current_item = itemstore_root(config.itemstore_ctx);
  ITEM_t *saved_pending_call_item = code;
  ctx.decoder.frame_start = bytecode;
  ctx.current_item = saved_current_item;
  ctx.pending_call_item = saved_pending_call_item;
  ctx.invocation_callstack_floor = 17;
  ctx.invocation_caller_item = saved_current_item;

  alloc_test_fail_after(0);
  VALUE_t failed = interpret(&ctx, code);
  alloc_test_fail_after(-1);
  ASSERT_EQ_INT(VALUE_nil, failed.type);
  ASSERT_TRUE(!ctx.initialized);
  ASSERT_TRUE(ctx.libcalls == NULL);
  ASSERT_TRUE(ctx.decoder.frame_start == bytecode);
  ASSERT_TRUE(ctx.current_item == saved_current_item);
  ASSERT_TRUE(ctx.pending_call_item == saved_pending_call_item);
  ASSERT_EQ_INT(17, ctx.invocation_callstack_floor);
  ASSERT_TRUE(ctx.invocation_caller_item == saved_current_item);
  ASSERT_EQ_INT(-1, config.vm->stack->current);
  ASSERT_EQ_INT(-1, config.vm->callstack->current);

  VALUE_t retried = interpret(&ctx, code);
  ASSERT_EQ_INT(VALUE_int, retried.type);
  ASSERT_EQ_INT(7, retried.i);
  ASSERT_TRUE(ctx.initialized);
  ASSERT_EQ_INT(-1, config.vm->stack->current);
  ASSERT_EQ_INT(-1, config.vm->callstack->current);
  runtime_destroy(&ctx);
  teardown_libcall_runtime();
}

void test_libcall_registry_init_failure_has_no_partial_state(void) {
  bool reached_success = false;

  for (long fail_at = 1; fail_at < 512; fail_at++) {
    libcall_reset_registry_for_tests();
    alloc_test_fail_after(fail_at);
    if (libcall_init_registry()) {
      reached_success = true;
      break;
    }

    // Every injected failure must leave the registry safe to initialize
    // again, proving that no partial allocation state escaped.
    alloc_test_fail_after(-1);
    ASSERT_TRUE(libcall_init_registry());
    ASSERT_TRUE(libcall_validate_registry());
  }

  ASSERT_TRUE(reached_success);
  alloc_test_fail_after(-1);

  uint8_t li = 0, ci = 0;
  uint8_t args = 0;
  ASSERT_TRUE(libcall_lookup_pair("sys", "log", &li, &ci, &args));
  ASSERT_EQ_INT(1, args);
}

void test_libcall_registry_lifecycle_reinit_sequence(void) {
  uint8_t li = 0, ci = 0;
  uint8_t args = 0;

  libcall_reset_registry_for_tests();
  ASSERT_TRUE(libcall_init_registry());
  ASSERT_TRUE(libcall_lookup_pair("sys", "log", &li, &ci, &args));
  ASSERT_EQ_INT(1, args);

  libcall_free_registry();
  ASSERT_TRUE(!libcall_lookup_pair("doesnot", "exist", &li, &ci, &args));

  ASSERT_TRUE(libcall_init_registry());
  ASSERT_TRUE(libcall_lookup_pair("sys", "log", &li, &ci, &args));
  ASSERT_EQ_INT(1, args);
}

void test_libcall_registry_repeated_teardown_is_safe(void) {
  libcall_reset_registry_for_tests();
  libcall_free_registry();
  libcall_free_registry();
  libcall_free_registry();

  ASSERT_TRUE(libcall_init_registry());
  ASSERT_TRUE(libcall_validate_registry());
}

void test_missing_libcall_is_null_and_interpret_deterministic(void) {
  ASSERT_TRUE(libcall_func_pair(255, 255) == NULL);

  memset(&config, 0, sizeof(config));
  init_errmsg();
  config.itemstore_ctx = itemstore_owner(make_root_item("root"));
  ASSERT_NOT_NULL(itemstore_root(config.itemstore_ctx));
  config.vm = make_vm();
  ASSERT_NOT_NULL(config.vm);

  uint8_t template_bytecode[] = {
    0x00, 0x00,
    'M', 255, 255,
    'h'
  };
  uint8_t *bytecode = malloc(sizeof(template_bytecode));
  ASSERT_NOT_NULL(bytecode);
  memcpy(bytecode, template_bytecode, sizeof(template_bytecode));

  ITEM_t *code = test_item_set_code(itemstore_root(config.itemstore_ctx), "test.missinglibcall",
                                  sizeof(template_bytecode), bytecode);
  ASSERT_NOT_NULL(code);

  VALUE_t v1 = interpret(test_ctx(), code);
  VALUE_t v2 = interpret(test_ctx(), code);
  ASSERT_EQ_INT(VALUE_nil, v1.type);
  ASSERT_EQ_INT(VALUE_nil, v2.type);
  ITEM_t *err_item = find_item(itemstore_root(config.itemstore_ctx), "error");
  ASSERT_NOT_NULL(err_item);
  ASSERT_EQ_INT(VALUE_int, item_value(err_item)->type);
  ASSERT_EQ_INT(ERR_RUNTIME_BYTECODE, item_value(err_item)->i);
  ITEM_t *err_msg = find_item(itemstore_root(config.itemstore_ctx), "error.msg");
  ASSERT_NOT_NULL(err_msg);
  ASSERT_EQ_INT(VALUE_str, item_value(err_msg)->type);
  ASSERT_TRUE(strstr(item_value(err_msg)->s, "unknown libcall pair (255,255)") != NULL);
  ASSERT_EQ_INT(-1, config.vm->stack->current);
  ASSERT_EQ_INT(-1, config.vm->callstack->current);

  destroy_vm(config.vm);
  destroy_item(itemstore_root(config.itemstore_ctx));
}

void test_default_libcall_wrappers_lazy_init_after_reset(void) {
  uint8_t li = 0, ci = 0;
  uint8_t args = 0;

  libcall_reset_registry_for_tests();
  ASSERT_TRUE(libcall_lookup_pair("str", "upper", &li, &ci, &args));
  ASSERT_EQ_INT(1, args);
  ASSERT_NOT_NULL(libcall_func_pair(li, ci));

  libcall_free_registry();
  args = 0;
  ASSERT_TRUE(libcall_pair_arg_count(li, ci, &args));
  ASSERT_EQ_INT(1, args);
  ASSERT_NOT_NULL(libcall_func_pair(li, ci));
  ASSERT_TRUE(!libcall_lookup_pair("missing", "missing", &li, &ci, &args));
}

void test_libcall_registry_self_check_invalid_entries(void) {
  const LIBCALL_t null_name[] = {{NULL, "x", 1, 0, 0, test_noop_libcall}, {NULL,NULL,0,0,0,NULL}};
  ASSERT_TRUE(!libcall_registry_self_check(null_name, false));

  LibcallRegistry valid = {0};
  uint8_t li = 0, ci = 0, args = 0;
  ASSERT_TRUE(libcall_registry_init(&valid));

  const LIBCALL_t bad_args[] = {{"sys", "x", 1, 0, 255, test_noop_libcall}, {NULL,NULL,0,0,0,NULL}};
  ASSERT_TRUE(!libcall_registry_self_check(bad_args, false));

  const LIBCALL_t dup_num[] = {{"sys","a",1,1,0,test_noop_libcall},{"sys","b",1,1,0,test_noop_libcall},{NULL,NULL,0,0,0,NULL}};
  ASSERT_TRUE(!libcall_registry_self_check(dup_num, false));

  const LIBCALL_t dup_text[] = {{"sys","a",1,1,0,test_noop_libcall},{"sys","a",1,2,0,test_noop_libcall},{NULL,NULL,0,0,0,NULL}};
  ASSERT_TRUE(!libcall_registry_self_check(dup_text, false));

  ASSERT_TRUE(libcall_registry_validate(&valid));
  ASSERT_TRUE(libcall_registry_lookup_pair(&valid, "sys", "log", &li, &ci,
                                           &args));
  ASSERT_EQ_INT(1, args);

  const LIBCALL_t gap_lib[] = {{"sys","a",1,0,0,test_noop_libcall},{"net","b",3,0,0,test_noop_libcall},{NULL,NULL,0,0,0,NULL}};
  ASSERT_TRUE(libcall_registry_self_check(gap_lib, false));
  ASSERT_TRUE(!libcall_func_pair(5, 255));
  libcall_registry_destroy(&valid);
}

void test_libcall_invalid_arg_branches_return_contracts(void) {
  setup_libcall_runtime();

  VALUE_t bad_name = {VALUE_int, {.i = 7}};
  VALUE_t start = {VALUE_int, {.i = 1}};
  VALUE_t repeat = {VALUE_int, {.i = 1}};
  push_stack(config.vm->stack, bad_name);
  push_stack(config.vm->stack, start);
  push_stack(config.vm->stack, repeat);
  (void)lc_task_newgametask(test_ctx(), NULL, itemstore_root(config.itemstore_ctx));
  VALUE_t ret = pop_stack(config.vm->stack);
  ASSERT_EQ_INT(VALUE_nil, ret.type);
  ITEM_t *err = find_item(itemstore_root(config.itemstore_ctx), "error");
  ASSERT_NOT_NULL(err);
  ASSERT_EQ_INT(ERR_RUNTIME_INVALIDARGS, item_value(err)->i);
  assert_invalid_args_detail_contains("task.newgametask");

  VALUE_t bad_taskid = {VALUE_str, {.s = strdup("x")}};
  push_stack(config.vm->stack, bad_taskid);
  (void)lc_task_killtask(test_ctx(), NULL, itemstore_root(config.itemstore_ctx));
  ret = pop_stack(config.vm->stack);
  ASSERT_EQ_INT(VALUE_nil, ret.type);
  err = find_item(itemstore_root(config.itemstore_ctx), "error");
  ASSERT_NOT_NULL(err);
  ASSERT_EQ_INT(ERR_RUNTIME_INVALIDARGS, item_value(err)->i);
  assert_invalid_args_detail_contains("task.killtask");

  VALUE_t missing_item = {VALUE_str, {.s = strdup("missing.task")}};
  VALUE_t negative_start = {VALUE_int, {.i = -1}};
  VALUE_t valid_repeat = {VALUE_int, {.i = 1}};
  push_stack(config.vm->stack, missing_item);
  push_stack(config.vm->stack, negative_start);
  push_stack(config.vm->stack, valid_repeat);
  (void)lc_task_newgametask(test_ctx(), NULL, itemstore_root(config.itemstore_ctx));
  ret = pop_stack(config.vm->stack);
  ASSERT_EQ_INT(VALUE_nil, ret.type);
  assert_invalid_args_detail_contains("intervals");

  ASSERT_TRUE(test_network_reset(1));
  VALUE_t bad_line = {VALUE_int, {.i = -1}};
  VALUE_t out = {VALUE_str, {.s = strdup("hello")}};
  push_stack(config.vm->stack, bad_line);
  push_stack(config.vm->stack, out);
  (void)lc_net_write(test_ctx(), NULL, itemstore_root(config.itemstore_ctx));
  ret = pop_stack(config.vm->stack);
  ASSERT_EQ_INT(VALUE_nil, ret.type);
  err = find_item(itemstore_root(config.itemstore_ctx), "error");
  ASSERT_NOT_NULL(err);
  ASSERT_EQ_INT(ERR_RUNTIME_INVALIDARGS, item_value(err)->i);
  assert_invalid_args_detail_contains("net.write");

  teardown_libcall_runtime();
}

void test_libcall_float_integer_only_arguments_rejected(void) {
  setup_libcall_runtime();

  VALUE_t itemname = {VALUE_str, {.s = strdup("missing.task")}};
  VALUE_t float_start = {VALUE_float, {.f = 1.5}};
  VALUE_t repeat = {VALUE_int, {.i = 1}};
  push_stack(config.vm->stack, itemname);
  push_stack(config.vm->stack, float_start);
  push_stack(config.vm->stack, repeat);
  (void)lc_task_newgametask(test_ctx(), NULL, itemstore_root(config.itemstore_ctx));
  VALUE_t ret = pop_stack(config.vm->stack);
  ASSERT_EQ_INT(VALUE_nil, ret.type);
  assert_invalid_args_float_detail_contains("task.newgametask");

  VALUE_t float_taskid = {VALUE_float, {.f = 2.0}};
  push_stack(config.vm->stack, float_taskid);
  (void)lc_task_killtask(test_ctx(), NULL, itemstore_root(config.itemstore_ctx));
  ret = pop_stack(config.vm->stack);
  ASSERT_EQ_INT(VALUE_nil, ret.type);
  assert_invalid_args_float_detail_contains("task.killtask");

  ASSERT_TRUE(test_network_reset(1));
  VALUE_t float_line = {VALUE_float, {.f = 0.0}};
  VALUE_t out = {VALUE_str, {.s = strdup("hello")}};
  push_stack(config.vm->stack, float_line);
  push_stack(config.vm->stack, out);
  (void)lc_net_write(test_ctx(), NULL, itemstore_root(config.itemstore_ctx));
  ret = pop_stack(config.vm->stack);
  ASSERT_EQ_INT(VALUE_nil, ret.type);
  assert_invalid_args_float_detail_contains("net.write");

  push_stack(config.vm->stack, (VALUE_t){VALUE_float, {.f = 0.0}});
  (void)lc_net_flush(test_ctx(), NULL, itemstore_root(config.itemstore_ctx));
  ret = pop_stack(config.vm->stack);
  ASSERT_EQ_INT(VALUE_nil, ret.type);
  assert_invalid_args_float_detail_contains("net.flush");

  push_stack(config.vm->stack, (VALUE_t){VALUE_float, {.f = 0.0}});
  (void)lc_net_ditch(test_ctx(), NULL, itemstore_root(config.itemstore_ctx));
  ret = pop_stack(config.vm->stack);
  ASSERT_EQ_INT(VALUE_nil, ret.type);
  assert_invalid_args_float_detail_contains("net.ditch");

  VALUE_t float_source = {VALUE_float, {.f = 3.25}};
  push_stack(config.vm->stack, float_source);
  (void)lc_sys_compile(test_ctx(), NULL, itemstore_root(config.itemstore_ctx));
  ret = pop_stack(config.vm->stack);
  ASSERT_EQ_INT(VALUE_bool, ret.type);
  ASSERT_EQ_INT(0, ret.i);
  assert_invalid_args_float_detail_contains("sys.compile");

  teardown_libcall_runtime();
}


void test_libcall_output_formats_values(void) {
  typedef struct {
    VALUE_t value;
    const char *expected;
  } output_case_t;

  setup_libcall_runtime();
  assert_logging_controls_and_redirection();

  const output_case_t sys_cases[] = {
    {(VALUE_t){VALUE_str, {.s = strdup("%s literal")}}, "%s literal"},
    {(VALUE_t){VALUE_str, {.s = NULL}}, ""},
    {(VALUE_t){VALUE_int, {.i = INT64_MIN}}, "-9223372036854775808"},
    {(VALUE_t){VALUE_float, {.f = value_float_from_bits(UINT64_C(0x8000000000000000))}}, "-0.0"},
    {(VALUE_t){VALUE_float, {.f = value_float_from_bits(UINT64_C(0x7ff0000000000000))}}, "inf"},
    {(VALUE_t){VALUE_float, {.f = value_float_from_bits(UINT64_C(0xfff0000000000000))}}, "-inf"},
    {(VALUE_t){VALUE_float, {.f = value_float_from_bits(UINT64_C(0x7ff8000000000042))}}, "nan"},
    {(VALUE_t){VALUE_bool, {.i = 1}}, "true"},
    {(VALUE_t){VALUE_bool, {.i = 0}}, "false"},
    {VALUE_NIL, ""},
  };
  for (size_t i = 0; i < sizeof(sys_cases) / sizeof(sys_cases[0]); i++) {
    assert_sys_log_output(sys_cases[i].value, sys_cases[i].expected);
  }

  ASSERT_TRUE(test_network_reset(1));
  ASSERT_TRUE(network_runtime_test_set_line(test_network_runtime(), 0,
                                            NETWORK_TEST_IDLE));

  const output_case_t net_cases[] = {
    {(VALUE_t){VALUE_str, {.s = strdup("hello")}}, "hello"},
    {(VALUE_t){VALUE_str, {.s = NULL}}, ""},
    {(VALUE_t){VALUE_int, {.i = INT64_MIN}}, "-9223372036854775808"},
    {(VALUE_t){VALUE_float, {.f = value_float_from_bits(UINT64_C(0x8000000000000000))}}, "-0.0"},
    {(VALUE_t){VALUE_float, {.f = value_float_from_bits(UINT64_C(0x7ff0000000000000))}}, "inf"},
    {(VALUE_t){VALUE_float, {.f = value_float_from_bits(UINT64_C(0xfff0000000000000))}}, "-inf"},
    {(VALUE_t){VALUE_float, {.f = value_float_from_bits(UINT64_C(0x7ff8000000000042))}}, "nan"},
    {(VALUE_t){VALUE_bool, {.i = 1}}, "true"},
    {(VALUE_t){VALUE_bool, {.i = 0}}, "false"},
    {VALUE_NIL, ""},
  };
  for (size_t i = 0; i < sizeof(net_cases) / sizeof(net_cases[0]); i++) {
    assert_net_write_output(net_cases[i].value, net_cases[i].expected);
  }

  VALUE_t aggregate_parts[] = {
    (VALUE_t){VALUE_str, {.s = strdup("text")}},
    VALUE_NIL,
    (VALUE_t){VALUE_itemref, {.itemref = sin_itemref_create("root.child")}}
  };
  SIN_LIST_t *aggregate_list = sin_list_build_owned(aggregate_parts, 3);
  ASSERT_NOT_NULL(aggregate_list);
  VALUE_t aggregate = {VALUE_list, {.list = aggregate_list}};
  const char *aggregate_expected = "#[\"text\", nil, &root.child]";
  assert_sys_log_output(value_clone(&aggregate), aggregate_expected);
  assert_net_write_output(value_clone(&aggregate), aggregate_expected);
  push_stack(config.vm->stack, value_clone(&aggregate));
  (void)lc_str_valtostr(test_ctx(), NULL, itemstore_root(config.itemstore_ctx));
  VALUE_t aggregate_text = pop_stack(config.vm->stack);
  ASSERT_EQ_INT(VALUE_str, aggregate_text.type);
  ASSERT_TRUE(strcmp(aggregate_text.s, aggregate_expected) == 0);
  value_free(&aggregate_text);
  sin_list_release(aggregate_list);

  VALUE_t malformed_part = {VALUE_list, {.list = NULL}};
  SIN_LIST_t *malformed_list = sin_list_build_owned(&malformed_part, 1);
  ASSERT_NOT_NULL(malformed_list);
  VALUE_t malformed = {VALUE_list, {.list = malformed_list}};
  assert_sys_log_output(value_clone(&malformed), "<value-render-error>");
  assert_net_write_render_failure(value_clone(&malformed));
  sin_list_release(malformed_list);

  teardown_libcall_runtime();
}
