#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <glob.h>
#include <unistd.h>
#include <stdint.h>

#include "libcall.h"
#include "config.h"
#include "error.h"
#include "interpret.h"
#include "item.h"
#include "item_internal.h"
#include "test_assert.h"
#include "test_helpers.h"
#include "task.h"
#include "vm.h"
#include "memory.h"
#include "runtime_value.h"
#include "string_limits.h"

#include "network.h"

uint8_t *lc_task_newgametask(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item);
uint8_t *lc_task_killtask(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item);
uint8_t *lc_net_write(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item);
uint8_t *lc_net_input(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item);
LINE_t *add_line(uv_tcp_t *line_handle);
uint8_t *lc_net_flush(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item);
uint8_t *lc_net_ditch(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item);
uint8_t *lc_sys_log(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item);
uint8_t *lc_sys_backup(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item);
uint8_t *lc_sys_save(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item);
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

extern LINE_t *line;
extern CONFIG_t config;

static RuntimeContext test_runtime_ctx;
static RuntimeContext *test_ctx(void) {
  runtime_context_init(&test_runtime_ctx, config.vm);
  test_runtime_ctx.itemroot = config.itemroot;
  test_runtime_ctx.strict_validation = config.strict_validation;
  test_runtime_ctx.strict_runtime_contracts = config.strict_runtime_contracts;
  test_runtime_ctx.maxconns = &config.maxconns;
  test_runtime_ctx.lastconn = &config.lastconn;
  test_runtime_ctx.inputline_name = config.inputline;
  test_runtime_ctx.inputtext_name = config.inputtext;
  test_runtime_ctx.loop = config.loop;
  test_runtime_ctx.safe_shutdown = &config.safe_shutdown;
  return &test_runtime_ctx;
}

static void setup_libcall_runtime(void) {
  memset(&config, 0, sizeof(config));
  init_errmsg();
  config.itemroot = make_root_item("root");
  ASSERT_NOT_NULL(config.itemroot);
  config.vm = make_vm();
  ASSERT_NOT_NULL(config.vm);
}

static void teardown_libcall_runtime(void) {
  if (line) {
    for (size_t i = 0; i < config.maxconns; i++) {
      if (line[i].telnet) {
        telnet_free(line[i].telnet);
        line[i].telnet = NULL;
      }
    }
    free(line);
    line = NULL;
  }
  destroy_vm(config.vm);
  destroy_item(config.itemroot);
}

static char telnet_capture[128];
static size_t telnet_capture_len;

static void capture_telnet_event(telnet_t *telnet, telnet_event_t *event, void *user_data) {
  (void)telnet;
  (void)user_data;
  if (event->type != TELNET_EV_SEND) return;
  size_t room = sizeof(telnet_capture) - telnet_capture_len - 1;
  size_t n = event->data.size < room ? event->data.size : room;
  memcpy(telnet_capture + telnet_capture_len, event->data.buffer, n);
  telnet_capture_len += n;
  telnet_capture[telnet_capture_len] = '\0';
}

static void reset_telnet_capture(void) {
  telnet_capture[0] = '\0';
  telnet_capture_len = 0;
}

static void assert_sys_log_output(VALUE_t out, const char *expected) {
  FILE *capture = tmpfile();
  ASSERT_NOT_NULL(capture);
  int saved_stdout = dup(STDOUT_FILENO);
  ASSERT_TRUE(saved_stdout >= 0);
  ASSERT_TRUE(dup2(fileno(capture), STDOUT_FILENO) >= 0);

  push_stack(config.vm->stack, out);
  (void)lc_sys_log(test_ctx(), NULL, config.itemroot);
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
  reset_telnet_capture();

  push_stack(config.vm->stack, (VALUE_t){VALUE_int, {.i = 0}});
  push_stack(config.vm->stack, out);
  (void)lc_net_write(test_ctx(), NULL, config.itemroot);
  VALUE_t ret = pop_stack(config.vm->stack);
  ASSERT_EQ_INT(VALUE_nil, ret.type);
  ASSERT_TRUE(strcmp(telnet_capture, expected) == 0);
}

static void assert_invalid_args_detail_contains(const char *expected) {
  ITEM_t *err = find_item(config.itemroot, "error");
  ASSERT_NOT_NULL(err);
  ASSERT_EQ_INT(VALUE_int, err->value.type);
  ASSERT_EQ_INT(ERR_RUNTIME_INVALIDARGS, err->value.i);
  ITEM_t *msg = find_item(config.itemroot, "error.msg");
  ASSERT_NOT_NULL(msg);
  ASSERT_EQ_INT(VALUE_str, msg->value.type);
  ASSERT_TRUE(strstr(msg->value.s, expected) != NULL);
}

static void assert_invalid_args_float_detail_contains(const char *expected) {
  assert_invalid_args_detail_contains("float");
  assert_invalid_args_detail_contains(expected);
}

static void assert_bool_return(VALUE_t value, int expected) {
  ASSERT_EQ_INT(VALUE_bool, value.type);
  ASSERT_EQ_INT(expected, value.i);
}

static void assert_persistence_error(const char *operation,
                                     const char *target,
                                     const char *current_item) {
  ITEM_t *error = find_item(config.itemroot, "error");
  ASSERT_NOT_NULL(error);
  ASSERT_EQ_INT(VALUE_int, error->value.type);
  ASSERT_EQ_INT(ERR_RUNTIME_PERSISTENCE, error->value.i);

  ITEM_t *message = find_item(config.itemroot, "error.msg");
  ASSERT_NOT_NULL(message);
  ASSERT_EQ_INT(VALUE_str, message->value.type);
  ASSERT_TRUE(strstr(message->value.s, errmsg[ERR_RUNTIME_PERSISTENCE]) != NULL);
  ASSERT_TRUE(strstr(message->value.s, operation) != NULL);
  ASSERT_TRUE(strstr(message->value.s, target) != NULL);

  ITEM_t *provenance = find_item(config.itemroot, "error.item");
  ASSERT_NOT_NULL(provenance);
  ASSERT_EQ_INT(VALUE_str, provenance->value.type);
  ASSERT_TRUE(strcmp(provenance->value.s, current_item) == 0);
}

static int persistence_sync_calls;
static int persistence_directory_sync_calls;

static bool count_persistence_sync(FILE *file, const char *path) {
  (void)file;
  (void)path;
  persistence_sync_calls++;
  return true;
}

static bool count_persistence_directory_sync(const char *path) {
  (void)path;
  persistence_directory_sync_calls++;
  return true;
}

static void assert_float_string_libcall_returns_invalidargs_nil(
    uint8_t *(*func)(RuntimeContext *, uint8_t *, ITEM_t *),
    const char *expected) {
  VALUE_t arg = {VALUE_float, {.f = 1.25}};
  push_stack(config.vm->stack, arg);
  (void)func(test_ctx(), NULL, config.itemroot);
  VALUE_t ret = pop_stack(config.vm->stack);
  ASSERT_EQ_INT(VALUE_nil, ret.type);
  assert_invalid_args_detail_contains(expected);
}

static void assert_float_string_libcall_uses_context_itemroot(
    uint8_t *(*func)(RuntimeContext *, uint8_t *, ITEM_t *),
    const char *expected) {
  ITEM_t *context_root = make_root_item("context-root");
  ASSERT_NOT_NULL(context_root);

  RuntimeContext ctx;
  runtime_context_init(&ctx, config.vm);
  ctx.itemroot = context_root;

  VALUE_t arg = {VALUE_float, {.f = 1.25}};
  push_stack(config.vm->stack, arg);
  (void)func(&ctx, NULL, context_root);
  VALUE_t ret = pop_stack(config.vm->stack);
  ASSERT_EQ_INT(VALUE_nil, ret.type);

  ITEM_t *context_err = find_item(context_root, "error");
  ASSERT_NOT_NULL(context_err);
  ASSERT_EQ_INT(VALUE_int, context_err->value.type);
  ASSERT_EQ_INT(ERR_RUNTIME_INVALIDARGS, context_err->value.i);
  ITEM_t *context_msg = find_item(context_root, "error.msg");
  ASSERT_NOT_NULL(context_msg);
  ASSERT_EQ_INT(VALUE_str, context_msg->value.type);
  ASSERT_TRUE(strstr(context_msg->value.s, expected) != NULL);

  ITEM_t *global_err = find_item(config.itemroot, "error");
  ASSERT_TRUE(global_err == NULL || global_err->value.type == VALUE_nil);

  destroy_item(context_root);
}

static void assert_str_unary_result(
    uint8_t *(*func)(RuntimeContext *, uint8_t *, ITEM_t *),
    const char *input,
    const char *expected) {
  VALUE_t text = {VALUE_str, {.s = strdup(input)}};
  push_stack(config.vm->stack, text);
  (void)func(test_ctx(), NULL, config.itemroot);
  VALUE_t ret = pop_stack(config.vm->stack);
  ASSERT_EQ_INT(VALUE_str, ret.type);
  ASSERT_TRUE(strcmp(ret.s, expected) == 0);
  FREE_STR(ret);
}

static void assert_str_substr_result(const char *input, int64_t start,
                                     int64_t len, const char *expected) {
  push_stack(config.vm->stack, (VALUE_t){VALUE_str, {.s = strdup(input)}});
  push_stack(config.vm->stack, (VALUE_t){VALUE_int, {.i = start}});
  push_stack(config.vm->stack, (VALUE_t){VALUE_int, {.i = len}});
  (void)lc_str_substr(test_ctx(), NULL, config.itemroot);
  VALUE_t ret = pop_stack(config.vm->stack);
  ASSERT_EQ_INT(VALUE_str, ret.type);
  ASSERT_TRUE(strcmp(ret.s, expected) == 0);
  FREE_STR(ret);
}

static void assert_str_valtostr_result(VALUE_t input, const char *expected) {
  push_stack(config.vm->stack, input);
  (void)lc_str_valtostr(test_ctx(), NULL, config.itemroot);
  VALUE_t ret = pop_stack(config.vm->stack);
  ASSERT_EQ_INT(VALUE_str, ret.type);
  ASSERT_TRUE(strcmp(ret.s, expected) == 0);
  FREE_STR(ret);
}

static void assert_str_replace_result(const char *text, const char *old_text,
                                      const char *new_text,
                                      const char *expected) {
  push_stack(config.vm->stack, (VALUE_t){VALUE_str, {.s = strdup(text)}});
  push_stack(config.vm->stack, (VALUE_t){VALUE_str, {.s = strdup(old_text)}});
  push_stack(config.vm->stack, (VALUE_t){VALUE_str, {.s = strdup(new_text)}});
  (void)lc_str_replace(test_ctx(), NULL, config.itemroot);
  VALUE_t ret = pop_stack(config.vm->stack);
  ASSERT_EQ_INT(VALUE_str, ret.type);
  ASSERT_TRUE(strcmp(ret.s, expected) == 0);
  FREE_STR(ret);
}

static void assert_str_repeat_result(const char *text, int64_t count,
                                     const char *expected) {
  push_stack(config.vm->stack, (VALUE_t){VALUE_str, {.s = strdup(text)}});
  push_stack(config.vm->stack, (VALUE_t){VALUE_int, {.i = count}});
  (void)lc_str_repeat(test_ctx(), NULL, config.itemroot);
  VALUE_t ret = pop_stack(config.vm->stack);
  ASSERT_EQ_INT(VALUE_str, ret.type);
  ASSERT_TRUE(strcmp(ret.s, expected) == 0);
  FREE_STR(ret);
}

static void assert_str_pad_result(
    uint8_t *(*func)(RuntimeContext *, uint8_t *, ITEM_t *),
    const char *text, int64_t width, const char *expected) {
  push_stack(config.vm->stack, (VALUE_t){VALUE_str, {.s = strdup(text)}});
  push_stack(config.vm->stack, (VALUE_t){VALUE_int, {.i = width}});
  (void)func(test_ctx(), NULL, config.itemroot);
  VALUE_t ret = pop_stack(config.vm->stack);
  ASSERT_EQ_INT(VALUE_str, ret.type);
  ASSERT_TRUE(strcmp(ret.s, expected) == 0);
  FREE_STR(ret);
}

static void assert_str_find_result(const char *haystack, const char *needle,
                                   int64_t expected) {
  push_stack(config.vm->stack, (VALUE_t){VALUE_str, {.s = strdup(haystack)}});
  push_stack(config.vm->stack, (VALUE_t){VALUE_str, {.s = strdup(needle)}});
  (void)lc_str_find(test_ctx(), NULL, config.itemroot);
  VALUE_t ret = pop_stack(config.vm->stack);
  ASSERT_EQ_INT(VALUE_int, ret.type);
  ASSERT_EQ_INT(expected, ret.i);
}

static void assert_str_contains_result(const char *haystack, const char *needle,
                                       int expected) {
  push_stack(config.vm->stack, (VALUE_t){VALUE_str, {.s = strdup(haystack)}});
  push_stack(config.vm->stack, (VALUE_t){VALUE_str, {.s = strdup(needle)}});
  (void)lc_str_contains(test_ctx(), NULL, config.itemroot);
  VALUE_t ret = pop_stack(config.vm->stack);
  ASSERT_EQ_INT(VALUE_bool, ret.type);
  ASSERT_EQ_INT(expected, ret.i);
}

static void assert_str_affix_result(
    uint8_t *(*func)(RuntimeContext *, uint8_t *, ITEM_t *),
    const char *haystack, const char *needle, int expected) {
  push_stack(config.vm->stack, (VALUE_t){VALUE_str, {.s = strdup(haystack)}});
  push_stack(config.vm->stack, (VALUE_t){VALUE_str, {.s = strdup(needle)}});
  (void)func(test_ctx(), NULL, config.itemroot);
  VALUE_t ret = pop_stack(config.vm->stack);
  ASSERT_EQ_INT(VALUE_bool, ret.type);
  ASSERT_EQ_INT(expected, ret.i);
}

static uint8_t *test_noop_libcall(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item) {
  (void)ctx; (void)item;
  return nextop;
}

static void assert_newgametask_invalid_interval_returns_nil(int64_t start_value,
                                                            int64_t repeat_value) {
  static int task_suffix = 0;
  char task_name[32];
  snprintf(task_name, sizeof(task_name), "valid.task.%d", task_suffix++);

  uint8_t *bytecode = malloc(1);
  ASSERT_NOT_NULL(bytecode);
  bytecode[0] = 'h';
  ITEM_t *task_item = insert_code_item(config.itemroot, task_name, 1, bytecode);
  ASSERT_NOT_NULL(task_item);

  RuntimeContext *ctx = test_ctx();
  ctx->loop = NULL;

  VALUE_t itemname = {VALUE_str, {.s = strdup(task_name)}};
  VALUE_t start = {VALUE_int, {.i = start_value}};
  VALUE_t repeat = {VALUE_int, {.i = repeat_value}};
  push_stack(config.vm->stack, itemname);
  push_stack(config.vm->stack, start);
  push_stack(config.vm->stack, repeat);

  (void)lc_task_newgametask(ctx, NULL, config.itemroot);

  VALUE_t ret = pop_stack(config.vm->stack);
  ASSERT_EQ_INT(VALUE_nil, ret.type);
  assert_invalid_args_detail_contains("task.newgametask intervals must be non-negative and within timer range");
}

void test_libcall_registry_roundtrip(void) {
  libcall_reset_registry_for_tests();
  ASSERT_TRUE(libcall_init_registry());
  ASSERT_TRUE(libcall_validate_registry());

  uint8_t token = 0;
  uint8_t args = 0;
  ASSERT_TRUE(libcall_lookup_token("sys", "log", &token, &args));
  ASSERT_EQ_INT(1, args);

  ASSERT_NOT_NULL(libcall_func_token(token));
  args = 0;
  ASSERT_TRUE(libcall_token_arg_count(token, &args));
  ASSERT_EQ_INT(1, args);
  ASSERT_TRUE(libcall_func_token(255) == NULL);
  ASSERT_TRUE(!libcall_token_arg_count(255, &args));

  ASSERT_TRUE(libcall_lookup_token("sys", "save", &token, &args));
  ASSERT_EQ_INT(33, token);
  ASSERT_EQ_INT(0, args);
  ASSERT_TRUE(libcall_func_token(token) == lc_sys_save);
  ASSERT_EQ_INT(1, libcalls[token].lib_index);
  ASSERT_EQ_INT(9, libcalls[token].call_index);
  ASSERT_EQ_INT(0, libcalls[token].args);
  ASSERT_TRUE(libcalls[token].func == lc_sys_save);

  ASSERT_TRUE(libcall_lookup_token("task", "newgametask", &token, &args));
  ASSERT_EQ_INT(3, args);
  args = 0;
  ASSERT_TRUE(libcall_token_arg_count(token, &args));
  ASSERT_EQ_INT(3, args);
  ASSERT_TRUE(!libcall_lookup_token("missing", "missing", &token, &args));

  ASSERT_TRUE(libcall_lookup_token("str", "valtostr", &token, &args));
  ASSERT_EQ_INT(1, args);
  ASSERT_NOT_NULL(libcall_func_token(token));
  ASSERT_TRUE(libcall_lookup_token("str", "replace", &token, &args));
  ASSERT_EQ_INT(3, args);
  ASSERT_NOT_NULL(libcall_func_token(token));
  ASSERT_TRUE(libcall_lookup_token("str", "repeat", &token, &args));
  ASSERT_EQ_INT(2, args);
  ASSERT_NOT_NULL(libcall_func_token(token));
  ASSERT_TRUE(libcall_lookup_token("str", "padleft", &token, &args));
  ASSERT_EQ_INT(2, args);
  ASSERT_NOT_NULL(libcall_func_token(token));
  ASSERT_TRUE(libcall_lookup_token("str", "padright", &token, &args));
  ASSERT_EQ_INT(2, args);
  ASSERT_NOT_NULL(libcall_func_token(token));
  ASSERT_TRUE(libcall_lookup_token("net", "flush", &token, &args));
  ASSERT_EQ_INT(32, token);
  ASSERT_EQ_INT(1, args);
  ASSERT_NOT_NULL(libcall_func_token(token));

  alloc_test_fail_after(0);
  token = 0;
  args = 0;
  ASSERT_TRUE(libcall_lookup_token("sys", "log", &token, &args));
  ASSERT_EQ_INT(1, args);
  ASSERT_TRUE(!libcall_lookup_token("missing", "missing", &token, &args));
  alloc_test_fail_after(-1);
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

  uint8_t token = 0;
  uint8_t args = 0;
  ASSERT_TRUE(libcall_lookup_token("sys", "log", &token, &args));
  ASSERT_EQ_INT(1, args);
}

void test_libcall_registry_lifecycle_reinit_sequence(void) {
  uint8_t token = 0;
  uint8_t args = 0;

  libcall_reset_registry_for_tests();
  ASSERT_TRUE(libcall_init_registry());
  ASSERT_TRUE(libcall_lookup_token("sys", "log", &token, &args));
  ASSERT_EQ_INT(1, args);

  libcall_free_registry();
  ASSERT_TRUE(!libcall_lookup_token("doesnot", "exist", &token, &args));

  ASSERT_TRUE(libcall_init_registry());
  ASSERT_TRUE(libcall_lookup_token("sys", "log", &token, &args));
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
  ASSERT_TRUE(libcall_func_token(255) == NULL);

  memset(&config, 0, sizeof(config));
  init_errmsg();
  config.itemroot = make_root_item("root");
  ASSERT_NOT_NULL(config.itemroot);
  config.vm = make_vm();
  ASSERT_NOT_NULL(config.vm);

  uint8_t template_bytecode[] = {
    0x00, 0x00,
    'M', 255,
    'h'
  };
  uint8_t *bytecode = malloc(sizeof(template_bytecode));
  ASSERT_NOT_NULL(bytecode);
  memcpy(bytecode, template_bytecode, sizeof(template_bytecode));

  ITEM_t *code = insert_code_item(config.itemroot, "test.missinglibcall",
                                  sizeof(template_bytecode), bytecode);
  ASSERT_NOT_NULL(code);

  VALUE_t v1 = interpret(test_ctx(), code);
  VALUE_t v2 = interpret(test_ctx(), code);
  ASSERT_EQ_INT(VALUE_nil, v1.type);
  ASSERT_EQ_INT(VALUE_nil, v2.type);
  ITEM_t *err_item = find_item(config.itemroot, "error");
  ASSERT_NOT_NULL(err_item);
  ASSERT_EQ_INT(VALUE_int, err_item->value.type);
  ASSERT_EQ_INT(ERR_RUNTIME_INVLIB, err_item->value.i);
  ITEM_t *err_msg = find_item(config.itemroot, "error.msg");
  ASSERT_NOT_NULL(err_msg);
  ASSERT_EQ_INT(VALUE_str, err_msg->value.type);
  ASSERT_TRUE(strstr(err_msg->value.s, "Unknown libcall token 255") != NULL);
  ASSERT_EQ_INT(-1, config.vm->stack->current);
  ASSERT_EQ_INT(-1, config.vm->callstack->current);

  destroy_vm(config.vm);
  destroy_item(config.itemroot);
}

void test_default_libcall_wrappers_lazy_init_after_reset(void) {
  uint8_t token = 0;
  uint8_t args = 0;

  libcall_reset_registry_for_tests();
  ASSERT_TRUE(libcall_lookup_token("str", "upper", &token, &args));
  ASSERT_EQ_INT(1, args);
  ASSERT_NOT_NULL(libcall_func_token(token));

  libcall_free_registry();
  args = 0;
  ASSERT_TRUE(libcall_token_arg_count(token, &args));
  ASSERT_EQ_INT(1, args);
  ASSERT_NOT_NULL(libcall_func_token(token));
  ASSERT_TRUE(!libcall_lookup_token("missing", "missing", &token, &args));
}

void test_libcall_registry_self_check_invalid_entries(void) {
  const LIBCALL_t null_name[] = {{NULL, "x", 1, 0, 0, test_noop_libcall}, {NULL,NULL,-1,-1,0,NULL}};
  ASSERT_TRUE(!libcall_registry_self_check(null_name, false));

  const LIBCALL_t bad_args[] = {{"sys", "x", 1, 0, 255, test_noop_libcall}, {NULL,NULL,-1,-1,0,NULL}};
  ASSERT_TRUE(!libcall_registry_self_check(bad_args, false));

  const LIBCALL_t dup_num[] = {{"sys","a",1,1,0,test_noop_libcall},{"sys","b",1,1,0,test_noop_libcall},{NULL,NULL,-1,-1,0,NULL}};
  ASSERT_TRUE(!libcall_registry_self_check(dup_num, false));

  const LIBCALL_t dup_text[] = {{"sys","a",1,1,0,test_noop_libcall},{"sys","a",1,2,0,test_noop_libcall},{NULL,NULL,-1,-1,0,NULL}};
  ASSERT_TRUE(!libcall_registry_self_check(dup_text, false));

  const LIBCALL_t gap_lib[] = {{"sys","a",1,0,0,test_noop_libcall},{"net","b",3,0,0,test_noop_libcall},{NULL,NULL,-1,-1,0,NULL}};
  ASSERT_TRUE(!libcall_registry_self_check(gap_lib, false));
}

void test_libcall_invalid_arg_branches_return_contracts(void) {
  setup_libcall_runtime();

  VALUE_t bad_name = {VALUE_int, {.i = 7}};
  VALUE_t start = {VALUE_int, {.i = 1}};
  VALUE_t repeat = {VALUE_int, {.i = 1}};
  push_stack(config.vm->stack, bad_name);
  push_stack(config.vm->stack, start);
  push_stack(config.vm->stack, repeat);
  (void)lc_task_newgametask(test_ctx(), NULL, config.itemroot);
  VALUE_t ret = pop_stack(config.vm->stack);
  ASSERT_EQ_INT(VALUE_nil, ret.type);
  ITEM_t *err = find_item(config.itemroot, "error");
  ASSERT_NOT_NULL(err);
  ASSERT_EQ_INT(ERR_RUNTIME_INVALIDARGS, err->value.i);
  assert_invalid_args_detail_contains("task.newgametask");

  VALUE_t bad_taskid = {VALUE_str, {.s = strdup("x")}};
  push_stack(config.vm->stack, bad_taskid);
  (void)lc_task_killtask(test_ctx(), NULL, config.itemroot);
  ret = pop_stack(config.vm->stack);
  ASSERT_EQ_INT(VALUE_nil, ret.type);
  err = find_item(config.itemroot, "error");
  ASSERT_NOT_NULL(err);
  ASSERT_EQ_INT(ERR_RUNTIME_INVALIDARGS, err->value.i);
  assert_invalid_args_detail_contains("task.killtask");

  VALUE_t missing_item = {VALUE_str, {.s = strdup("missing.task")}};
  VALUE_t negative_start = {VALUE_int, {.i = -1}};
  VALUE_t valid_repeat = {VALUE_int, {.i = 1}};
  push_stack(config.vm->stack, missing_item);
  push_stack(config.vm->stack, negative_start);
  push_stack(config.vm->stack, valid_repeat);
  (void)lc_task_newgametask(test_ctx(), NULL, config.itemroot);
  ret = pop_stack(config.vm->stack);
  ASSERT_EQ_INT(VALUE_nil, ret.type);
  assert_invalid_args_detail_contains("intervals");

  config.maxconns = 1;
  line = calloc((size_t)config.maxconns, sizeof(LINE_t));
  ASSERT_NOT_NULL(line);
  VALUE_t bad_line = {VALUE_int, {.i = -1}};
  VALUE_t out = {VALUE_str, {.s = strdup("hello")}};
  push_stack(config.vm->stack, bad_line);
  push_stack(config.vm->stack, out);
  (void)lc_net_write(test_ctx(), NULL, config.itemroot);
  ret = pop_stack(config.vm->stack);
  ASSERT_EQ_INT(VALUE_nil, ret.type);
  err = find_item(config.itemroot, "error");
  ASSERT_NOT_NULL(err);
  ASSERT_EQ_INT(ERR_RUNTIME_INVALIDARGS, err->value.i);
  assert_invalid_args_detail_contains("net.write");

  teardown_libcall_runtime();
}

void test_newgametask_rejects_invalid_intervals_before_timer_start(void) {
  setup_libcall_runtime();

  assert_newgametask_invalid_interval_returns_nil(-1, 1);
  assert_newgametask_invalid_interval_returns_nil(1, -1);
  assert_newgametask_invalid_interval_returns_nil(-1, -1);
  assert_newgametask_invalid_interval_returns_nil((INT64_MAX / 100) + 1, 1);
  assert_newgametask_invalid_interval_returns_nil(1, (INT64_MAX / 100) + 1);

  teardown_libcall_runtime();
}

void test_newgametask_rejects_missing_event_loop_before_returning_task_id(void) {
  setup_libcall_runtime();

  uint8_t *bytecode = malloc(1);
  ASSERT_NOT_NULL(bytecode);
  bytecode[0] = 'h';
  ASSERT_NOT_NULL(insert_code_item(config.itemroot, "valid.loopless.task", 1, bytecode));

  RuntimeContext *ctx = test_ctx();
  ctx->loop = NULL;

  push_stack(config.vm->stack, (VALUE_t){VALUE_str, {.s = strdup("valid.loopless.task")}});
  push_stack(config.vm->stack, (VALUE_t){VALUE_int, {.i = 1}});
  push_stack(config.vm->stack, (VALUE_t){VALUE_int, {.i = 1}});
  (void)lc_task_newgametask(ctx, NULL, config.itemroot);

  VALUE_t ret = pop_stack(config.vm->stack);
  ASSERT_EQ_INT(VALUE_nil, ret.type);
  assert_invalid_args_detail_contains("task.newgametask requires an active event loop");
  ASSERT_TRUE(find_task_by_id(1) == NULL);

  teardown_libcall_runtime();
}

void test_net_write_ignores_non_writable_lines(void) {
  setup_libcall_runtime();

  config.maxconns = 3;
  line = calloc((size_t)config.maxconns, sizeof(LINE_t));
  ASSERT_NOT_NULL(line);
  const int statuses[] = {
    LINE_empty, LINE_connecting, LINE_disconnecting
  };
  for (size_t i = 0; i < sizeof(statuses) / sizeof(statuses[0]); i++) {
    line[i].status = statuses[i];
    line[i].telnet = NULL;

    push_stack(config.vm->stack, (VALUE_t){VALUE_int, {.i = (int64_t)i}});
    push_stack(config.vm->stack,
               (VALUE_t){VALUE_str, {.s = strdup("hello")}});
    (void)lc_net_write(test_ctx(), NULL, config.itemroot);
    VALUE_t ret = pop_stack(config.vm->stack);
    ASSERT_EQ_INT(VALUE_nil, ret.type);
  }

  teardown_libcall_runtime();
}

void test_net_input_fair_queue_progresses_connect_data_disconnect(void) {
  setup_libcall_runtime();

  config.maxconns = 3;
  config.lastconn = 2;
  config.inputline = strdup("input.line");
  config.inputtext = strdup("input.text");
  ASSERT_NOT_NULL(config.inputline);
  ASSERT_NOT_NULL(config.inputtext);
  line = calloc(config.maxconns, sizeof(LINE_t));
  ASSERT_NOT_NULL(line);

  for (size_t i = 0; i < config.maxconns; i++) {
    uv_tcp_t *line_handle = calloc(1, sizeof(*line_handle));
    ASSERT_NOT_NULL(line_handle);
    ASSERT_NOT_NULL(add_line(line_handle));
    line[i].telnet = telnet_init(NULL, capture_telnet_event, 0, NULL);
    ASSERT_NOT_NULL(line[i].telnet);
  }

  line[1].status = LINE_data;
  free(line[1].inbuf->buf.base);
  line[1].inbuf->buf.base = strdup("hello\n");
  ASSERT_NOT_NULL(line[1].inbuf->buf.base);
  line[1].inbuf->buf.len = strlen(line[1].inbuf->buf.base);
  line[1].inbuf->length = line[1].inbuf->buf.len + 1;
  line[2].status = LINE_disconnecting;

  RuntimeContext *ctx = test_ctx();
  (void)lc_net_input(ctx, NULL, config.itemroot);
  VALUE_t ret = pop_stack(config.vm->stack);
  ASSERT_EQ_INT(VALUE_int, ret.type);
  ASSERT_EQ_INT(1, ret.i);
  ITEM_t *input_line_item = find_item(config.itemroot, "input.line");
  ASSERT_NOT_NULL(input_line_item);
  ASSERT_EQ_INT(0, input_line_item->value.i);

  (void)lc_net_input(ctx, NULL, config.itemroot);
  ret = pop_stack(config.vm->stack);
  ASSERT_EQ_INT(VALUE_int, ret.type);
  ASSERT_EQ_INT(3, ret.i);
  ASSERT_EQ_INT(1, input_line_item->value.i);
  ITEM_t *input_text_item = find_item(config.itemroot, "input.text");
  ASSERT_NOT_NULL(input_text_item);
  ASSERT_TRUE(strcmp(input_text_item->value.s, "hello") == 0);

  (void)lc_net_input(ctx, NULL, config.itemroot);
  ret = pop_stack(config.vm->stack);
  ASSERT_EQ_INT(VALUE_int, ret.type);
  ASSERT_EQ_INT(2, ret.i);
  ASSERT_EQ_INT(LINE_empty, line[2].status);

  destroy_line(&line[0]);
  destroy_line(&line[1]);
  free(config.inputline);
  free(config.inputtext);
  config.inputline = NULL;
  config.inputtext = NULL;
  teardown_libcall_runtime();
}

void test_net_ditch_disconnects_active_lines(void) {
  setup_libcall_runtime();

  config.maxconns = 2;
  line = calloc((size_t)config.maxconns, sizeof(LINE_t));
  ASSERT_NOT_NULL(line);
  line[1].status = LINE_idle;
  line[1].telnet = telnet_init(NULL, capture_telnet_event, 0, NULL);
  ASSERT_NOT_NULL(line[1].telnet);

  push_stack(config.vm->stack, (VALUE_t){VALUE_int, {.i = 1}});
  (void)lc_net_ditch(test_ctx(), NULL, config.itemroot);
  VALUE_t ret = pop_stack(config.vm->stack);
  ASSERT_EQ_INT(VALUE_bool, ret.type);
  ASSERT_EQ_INT(1, ret.i);
  ASSERT_EQ_INT(LINE_disconnecting, line[1].status);

  push_stack(config.vm->stack, (VALUE_t){VALUE_int, {.i = 1}});
  (void)lc_net_ditch(test_ctx(), NULL, config.itemroot);
  ret = pop_stack(config.vm->stack);
  ASSERT_EQ_INT(VALUE_bool, ret.type);
  ASSERT_EQ_INT(0, ret.i);

  reset_telnet_capture();
  push_stack(config.vm->stack, (VALUE_t){VALUE_int, {.i = 1}});
  push_stack(config.vm->stack,
             (VALUE_t){VALUE_str, {.s = strdup("after ditch")}});
  (void)lc_net_write(test_ctx(), NULL, config.itemroot);
  ret = pop_stack(config.vm->stack);
  ASSERT_EQ_INT(VALUE_nil, ret.type);
  ASSERT_TRUE(strcmp(telnet_capture, "") == 0);

  teardown_libcall_runtime();
}

void test_net_flush_reports_line_status(void) {
  setup_libcall_runtime();

  config.maxconns = 2;
  line = calloc((size_t)config.maxconns, sizeof(LINE_t));
  ASSERT_NOT_NULL(line);
  line[0].status = LINE_idle;
  line[1].status = LINE_empty;

  push_stack(config.vm->stack, (VALUE_t){VALUE_int, {.i = 0}});
  (void)lc_net_flush(test_ctx(), NULL, config.itemroot);
  VALUE_t ret = pop_stack(config.vm->stack);
  ASSERT_EQ_INT(VALUE_bool, ret.type);
  ASSERT_EQ_INT(1, ret.i);

  push_stack(config.vm->stack, (VALUE_t){VALUE_int, {.i = 1}});
  (void)lc_net_flush(test_ctx(), NULL, config.itemroot);
  ret = pop_stack(config.vm->stack);
  ASSERT_EQ_INT(VALUE_bool, ret.type);
  ASSERT_EQ_INT(0, ret.i);
  ITEM_t *err = find_item(config.itemroot, "error");
  ASSERT_NOT_NULL(err);
  ASSERT_EQ_INT(ERR_NETWORK_ERROR, err->value.i);

  push_stack(config.vm->stack, (VALUE_t){VALUE_float, {.f = 0.0}});
  (void)lc_net_flush(test_ctx(), NULL, config.itemroot);
  ret = pop_stack(config.vm->stack);
  ASSERT_EQ_INT(VALUE_nil, ret.type);
  assert_invalid_args_detail_contains("net.flush");

  teardown_libcall_runtime();
}

void test_net_ditch_reports_inactive_lines(void) {
  setup_libcall_runtime();

  config.maxconns = 2;
  line = calloc((size_t)config.maxconns, sizeof(LINE_t));
  ASSERT_NOT_NULL(line);
  line[0].status = LINE_empty;
  line[1].status = LINE_disconnecting;

  for (size_t i = 0; i < config.maxconns; i++) {
    push_stack(config.vm->stack, (VALUE_t){VALUE_int, {.i = (int64_t)i}});
    (void)lc_net_ditch(test_ctx(), NULL, config.itemroot);
    VALUE_t ret = pop_stack(config.vm->stack);
    ASSERT_EQ_INT(VALUE_bool, ret.type);
    ASSERT_EQ_INT(0, ret.i);
    ITEM_t *err = find_item(config.itemroot, "error");
    ASSERT_NOT_NULL(err);
    ASSERT_EQ_INT(ERR_NETWORK_ERROR, err->value.i);
  }

  teardown_libcall_runtime();
}

void test_net_ditch_invalid_line_returns_nil(void) {
  setup_libcall_runtime();

  config.maxconns = 1;
  line = calloc((size_t)config.maxconns, sizeof(LINE_t));
  ASSERT_NOT_NULL(line);

  push_stack(config.vm->stack, (VALUE_t){VALUE_float, {.f = 0.0}});
  (void)lc_net_ditch(test_ctx(), NULL, config.itemroot);
  VALUE_t ret = pop_stack(config.vm->stack);
  ASSERT_EQ_INT(VALUE_nil, ret.type);
  assert_invalid_args_detail_contains("net.ditch");

  push_stack(config.vm->stack, (VALUE_t){VALUE_int, {.i = -1}});
  (void)lc_net_ditch(test_ctx(), NULL, config.itemroot);
  ret = pop_stack(config.vm->stack);
  ASSERT_EQ_INT(VALUE_nil, ret.type);
  assert_invalid_args_detail_contains("net.ditch");

  teardown_libcall_runtime();
}

void test_sys_item_libcalls(void) {
  setup_libcall_runtime();

  ASSERT_NOT_NULL(insert_item(config.itemroot, "parent.first",
                              (VALUE_t){VALUE_int, {.i = 1}}));
  ASSERT_NOT_NULL(insert_item(config.itemroot, "parent.second",
                              (VALUE_t){VALUE_int, {.i = 2}}));
  ASSERT_NOT_NULL(insert_item(config.itemroot, "victim",
                              (VALUE_t){VALUE_bool, {.i = 1}}));

  push_stack(config.vm->stack, (VALUE_t){VALUE_str, {.s = strdup("victim")}});
  (void)lc_sys_exists(test_ctx(), NULL, config.itemroot);
  VALUE_t ret = pop_stack(config.vm->stack);
  ASSERT_EQ_INT(VALUE_bool, ret.type);
  ASSERT_EQ_INT(1, ret.i);

  push_stack(config.vm->stack, (VALUE_t){VALUE_str, {.s = strdup("missing")}});
  (void)lc_sys_exists(test_ctx(), NULL, config.itemroot);
  ret = pop_stack(config.vm->stack);
  ASSERT_EQ_INT(VALUE_bool, ret.type);
  ASSERT_EQ_INT(0, ret.i);

  push_stack(config.vm->stack, (VALUE_t){VALUE_str, {.s = strdup("victim")}});
  (void)lc_sys_delete(test_ctx(), NULL, config.itemroot);
  ret = pop_stack(config.vm->stack);
  ASSERT_EQ_INT(VALUE_nil, ret.type);
  ASSERT_TRUE(find_item(config.itemroot, "victim") == NULL);

  push_stack(config.vm->stack, (VALUE_t){VALUE_str, {.s = strdup("parent")}});
  push_stack(config.vm->stack, (VALUE_t){VALUE_int, {.i = 1}});
  (void)lc_sys_nthname(test_ctx(), NULL, config.itemroot);
  ret = pop_stack(config.vm->stack);
  ASSERT_EQ_INT(VALUE_str, ret.type);
  ASSERT_TRUE(strcmp(ret.s, "second") == 0);
  FREE_STR(ret);

  push_stack(config.vm->stack, (VALUE_t){VALUE_int, {.i = 0}});
  (void)lc_sys_rootname(test_ctx(), NULL, config.itemroot);
  ret = pop_stack(config.vm->stack);
  ASSERT_EQ_INT(VALUE_str, ret.type);
  ASSERT_TRUE(strcmp(ret.s, "parent") == 0);
  FREE_STR(ret);

  push_stack(config.vm->stack, (VALUE_t){VALUE_int, {.i = 1}});
  (void)lc_sys_exists(test_ctx(), NULL, config.itemroot);
  ret = pop_stack(config.vm->stack);
  ASSERT_EQ_INT(VALUE_bool, ret.type);
  ASSERT_EQ_INT(0, ret.i);
  assert_invalid_args_detail_contains("sys.exists");

  teardown_libcall_runtime();
}

void test_sys_persistence_libcalls(void) {
  setup_libcall_runtime();

  char store_path[128];
  ASSERT_EQ_INT(0, test_make_temp_path("sin-sys-save", store_path,
                                      sizeof(store_path)));
  ITEM_t *caller = insert_item(config.itemroot, "persistence.caller",
                               (VALUE_t){VALUE_bool, {.i = 1}});
  ASSERT_NOT_NULL(caller);
  ASSERT_NOT_NULL(insert_item(config.itemroot, "checkpoint.value",
                              (VALUE_t){VALUE_int, {.i = 1}}));

  RuntimeContext ctx;
  runtime_context_init(&ctx, config.vm);
  ctx.itemroot = config.itemroot;
  ctx.itemstore_filename = store_path;
  ctx.current_item = caller;

  itemstore_set_sync_hook_for_tests(count_persistence_sync);
  itemstore_set_directory_sync_hook_for_tests(
      count_persistence_directory_sync);

  set_error_item(config.itemroot, ERR_RUNTIME_INVALIDARGS,
                 "unrelated prior error", caller);
  ITEM_t *prior_message = find_item(config.itemroot, "error.msg");
  ASSERT_NOT_NULL(prior_message);
  ASSERT_EQ_INT(VALUE_str, prior_message->value.type);
  char *prior_message_text = strdup(prior_message->value.s);
  ASSERT_NOT_NULL(prior_message_text);

  config.itemstore_durability = ITEMSTORE_DURABLE_FULL;
  ctx.itemstore_durability = ITEMSTORE_DURABLE_FAST;
  persistence_sync_calls = 0;
  persistence_directory_sync_calls = 0;
  (void)lc_sys_save(&ctx, NULL, caller);
  assert_bool_return(pop_stack(config.vm->stack), 1);
  ASSERT_EQ_INT(0, persistence_sync_calls);
  ASSERT_EQ_INT(0, persistence_directory_sync_calls);
  ITEM_t *error = find_item(config.itemroot, "error");
  ASSERT_NOT_NULL(error);
  ASSERT_EQ_INT(ERR_RUNTIME_INVALIDARGS, error->value.i);
  prior_message = find_item(config.itemroot, "error.msg");
  ASSERT_NOT_NULL(prior_message);
  ASSERT_EQ_INT(VALUE_str, prior_message->value.type);
  ASSERT_TRUE(strcmp(prior_message->value.s, prior_message_text) == 0);
  free(prior_message_text);

  ASSERT_NOT_NULL(insert_item(config.itemroot, "checkpoint.value",
                              (VALUE_t){VALUE_int, {.i = 2}}));
  ITEM_t *loaded = load_itemstore(store_path);
  ASSERT_NOT_NULL(loaded);
  ITEM_t *loaded_checkpoint = find_item(loaded, "checkpoint.value");
  ASSERT_NOT_NULL(loaded_checkpoint);
  ASSERT_EQ_INT(VALUE_int, loaded_checkpoint->value.type);
  ASSERT_EQ_INT(1, loaded_checkpoint->value.i);
  destroy_item(loaded);

  config.itemstore_durability = ITEMSTORE_DURABLE_FAST;
  ctx.itemstore_durability = ITEMSTORE_DURABLE_FULL;
  persistence_sync_calls = 0;
  persistence_directory_sync_calls = 0;
  (void)lc_sys_save(&ctx, NULL, caller);
  assert_bool_return(pop_stack(config.vm->stack), 1);
  ASSERT_EQ_INT(1, persistence_sync_calls);
  ASSERT_EQ_INT(1, persistence_directory_sync_calls);

  ASSERT_NOT_NULL(insert_item(config.itemroot, "backup.only",
                              (VALUE_t){VALUE_int, {.i = 3}}));
  set_error_item(config.itemroot, ERR_RUNTIME_INVALIDARGS,
                 "backup prior error", caller);
  prior_message = find_item(config.itemroot, "error.msg");
  ASSERT_NOT_NULL(prior_message);
  prior_message_text = strdup(prior_message->value.s);
  ASSERT_NOT_NULL(prior_message_text);
  ctx.itemstore_durability = ITEMSTORE_DURABLE_FULL;
  persistence_sync_calls = 0;
  persistence_directory_sync_calls = 0;
  (void)lc_sys_backup(&ctx, NULL, caller);
  assert_bool_return(pop_stack(config.vm->stack), 1);
  ASSERT_EQ_INT(1, persistence_sync_calls);
  ASSERT_EQ_INT(1, persistence_directory_sync_calls);
  error = find_item(config.itemroot, "error");
  ASSERT_NOT_NULL(error);
  ASSERT_EQ_INT(ERR_RUNTIME_INVALIDARGS, error->value.i);
  prior_message = find_item(config.itemroot, "error.msg");
  ASSERT_NOT_NULL(prior_message);
  ASSERT_TRUE(strcmp(prior_message->value.s, prior_message_text) == 0);
  free(prior_message_text);

  loaded = load_itemstore(store_path);
  ASSERT_NOT_NULL(loaded);
  ASSERT_TRUE(find_item(loaded, "backup.only") == NULL);
  destroy_item(loaded);

  char backup_pattern[sizeof(store_path) + 4u];
  int written = snprintf(backup_pattern, sizeof(backup_pattern), "%s_*",
                         store_path);
  ASSERT_TRUE(written > 0 && (size_t)written < sizeof(backup_pattern));
  glob_t backups = {0};
  ASSERT_EQ_INT(0, glob(backup_pattern, 0, NULL, &backups));
  ASSERT_EQ_INT(1, backups.gl_pathc);
  loaded = load_itemstore(backups.gl_pathv[0]);
  ASSERT_NOT_NULL(loaded);
  ASSERT_NOT_NULL(find_item(loaded, "backup.only"));
  loaded_checkpoint = find_item(loaded, "checkpoint.value");
  ASSERT_NOT_NULL(loaded_checkpoint);
  ASSERT_EQ_INT(2, loaded_checkpoint->value.i);
  destroy_item(loaded);
  ASSERT_EQ_INT(0, unlink(backups.gl_pathv[0]));
  globfree(&backups);

  char missing_parent[128];
  ASSERT_EQ_INT(0, test_make_temp_path("sin-sys-save-missing",
                                      missing_parent,
                                      sizeof(missing_parent)));
  char failing_path[sizeof(missing_parent) + 16u];
  written = snprintf(failing_path, sizeof(failing_path), "%s/store",
                     missing_parent);
  ASSERT_TRUE(written > 0 && (size_t)written < sizeof(failing_path));
  ctx.itemstore_filename = failing_path;

  (void)lc_sys_save(&ctx, NULL, caller);
  assert_bool_return(pop_stack(config.vm->stack), 0);
  assert_persistence_error("sys.save", failing_path, "persistence.caller");

  (void)lc_sys_backup(&ctx, NULL, caller);
  assert_bool_return(pop_stack(config.vm->stack), 0);
  assert_persistence_error("sys.backup", failing_path,
                           "persistence.caller");
  ITEM_t *backup_failure_message = find_item(config.itemroot, "error.msg");
  ASSERT_NOT_NULL(backup_failure_message);
  const char *backup_target = strstr(backup_failure_message->value.s,
                                     failing_path);
  ASSERT_NOT_NULL(backup_target);
  ASSERT_EQ_INT('_', backup_target[strlen(failing_path)]);

  ctx.itemstore_filename = NULL;
  (void)lc_sys_save(&ctx, NULL, caller);
  assert_bool_return(pop_stack(config.vm->stack), 0);
  assert_persistence_error("sys.save", "<unconfigured>",
                           "persistence.caller");
  (void)lc_sys_backup(&ctx, NULL, caller);
  assert_bool_return(pop_stack(config.vm->stack), 0);
  assert_persistence_error("sys.backup", "<unconfigured>",
                           "persistence.caller");

  ctx.itemstore_filename = store_path;
  ctx.itemroot = NULL;
  (void)lc_sys_save(&ctx, NULL, caller);
  assert_bool_return(pop_stack(config.vm->stack), 0);
  (void)lc_sys_backup(&ctx, NULL, caller);
  assert_bool_return(pop_stack(config.vm->stack), 0);

  ctx.itemroot = config.itemroot;
  char invalid_runtime_path[128];
  ASSERT_EQ_INT(0, test_make_temp_path("sin-sys-save-invalid-runtime",
                                      invalid_runtime_path,
                                      sizeof(invalid_runtime_path)));
  ctx.itemstore_filename = invalid_runtime_path;
  uint8_t nextop_marker = 0;
  ctx.vm = NULL;
  ASSERT_TRUE(lc_sys_save(&ctx, &nextop_marker, caller) == &nextop_marker);
  assert_persistence_error("sys.save", invalid_runtime_path,
                           "persistence.caller");
  ASSERT_TRUE(lc_sys_backup(&ctx, &nextop_marker, caller) == &nextop_marker);
  assert_persistence_error("sys.backup", invalid_runtime_path,
                           "persistence.caller");
  ASSERT_TRUE(access(invalid_runtime_path, F_OK) != 0);

  VM_t stackless_vm = {0};
  ctx.vm = &stackless_vm;
  ASSERT_TRUE(lc_sys_save(&ctx, &nextop_marker, caller) == &nextop_marker);
  ASSERT_TRUE(lc_sys_backup(&ctx, &nextop_marker, caller) == &nextop_marker);
  ASSERT_TRUE(access(invalid_runtime_path, F_OK) != 0);

  ASSERT_TRUE(lc_sys_save(NULL, &nextop_marker, caller) == &nextop_marker);
  ASSERT_TRUE(lc_sys_backup(NULL, &nextop_marker, caller) == &nextop_marker);
  char invalid_backup_pattern[sizeof(invalid_runtime_path) + 4u];
  written = snprintf(invalid_backup_pattern, sizeof(invalid_backup_pattern),
                     "%s_*", invalid_runtime_path);
  ASSERT_TRUE(written > 0 && (size_t)written < sizeof(invalid_backup_pattern));
  glob_t invalid_backups = {0};
  ASSERT_EQ_INT(GLOB_NOMATCH,
                glob(invalid_backup_pattern, 0, NULL, &invalid_backups));
  globfree(&invalid_backups);

  itemstore_set_sync_hook_for_tests(NULL);
  itemstore_set_directory_sync_hook_for_tests(NULL);
  ASSERT_EQ_INT(0, unlink(store_path));
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
  (void)lc_task_newgametask(test_ctx(), NULL, config.itemroot);
  VALUE_t ret = pop_stack(config.vm->stack);
  ASSERT_EQ_INT(VALUE_nil, ret.type);
  assert_invalid_args_float_detail_contains("task.newgametask");

  VALUE_t float_taskid = {VALUE_float, {.f = 2.0}};
  push_stack(config.vm->stack, float_taskid);
  (void)lc_task_killtask(test_ctx(), NULL, config.itemroot);
  ret = pop_stack(config.vm->stack);
  ASSERT_EQ_INT(VALUE_nil, ret.type);
  assert_invalid_args_float_detail_contains("task.killtask");

  config.maxconns = 1;
  line = calloc((size_t)config.maxconns, sizeof(LINE_t));
  ASSERT_NOT_NULL(line);
  VALUE_t float_line = {VALUE_float, {.f = 0.0}};
  VALUE_t out = {VALUE_str, {.s = strdup("hello")}};
  push_stack(config.vm->stack, float_line);
  push_stack(config.vm->stack, out);
  (void)lc_net_write(test_ctx(), NULL, config.itemroot);
  ret = pop_stack(config.vm->stack);
  ASSERT_EQ_INT(VALUE_nil, ret.type);
  assert_invalid_args_float_detail_contains("net.write");

  push_stack(config.vm->stack, (VALUE_t){VALUE_float, {.f = 0.0}});
  (void)lc_net_flush(test_ctx(), NULL, config.itemroot);
  ret = pop_stack(config.vm->stack);
  ASSERT_EQ_INT(VALUE_nil, ret.type);
  assert_invalid_args_float_detail_contains("net.flush");

  push_stack(config.vm->stack, (VALUE_t){VALUE_float, {.f = 0.0}});
  (void)lc_net_ditch(test_ctx(), NULL, config.itemroot);
  ret = pop_stack(config.vm->stack);
  ASSERT_EQ_INT(VALUE_nil, ret.type);
  assert_invalid_args_float_detail_contains("net.ditch");

  VALUE_t float_source = {VALUE_float, {.f = 3.25}};
  push_stack(config.vm->stack, float_source);
  (void)lc_sys_compile(test_ctx(), NULL, config.itemroot);
  ret = pop_stack(config.vm->stack);
  ASSERT_EQ_INT(VALUE_bool, ret.type);
  ASSERT_EQ_INT(0, ret.i);
  assert_invalid_args_float_detail_contains("sys.compile");

  teardown_libcall_runtime();
}


void test_str_libcalls_float_returns_invalidargs_nil(void) {
  setup_libcall_runtime();

  assert_float_string_libcall_returns_invalidargs_nil(lc_str_capitalise,
      "str.capitalise");
  assert_float_string_libcall_returns_invalidargs_nil(lc_str_upper,
      "str.upper");
  assert_float_string_libcall_returns_invalidargs_nil(lc_str_lower,
      "str.lower");
  assert_float_string_libcall_returns_invalidargs_nil(lc_str_len,
      "str.len");
  assert_float_string_libcall_returns_invalidargs_nil(lc_str_trim,
      "str.trim");
  assert_float_string_libcall_returns_invalidargs_nil(lc_str_ltrim,
      "str.ltrim");
  assert_float_string_libcall_returns_invalidargs_nil(lc_str_rtrim,
      "str.rtrim");

  teardown_libcall_runtime();
}

void test_str_len_returns_string_byte_length(void) {
  setup_libcall_runtime();

  VALUE_t text = {VALUE_str, {.s = strdup("hello")}};
  push_stack(config.vm->stack, text);
  (void)lc_str_len(test_ctx(), NULL, config.itemroot);
  VALUE_t ret = pop_stack(config.vm->stack);
  ASSERT_EQ_INT(VALUE_int, ret.type);
  ASSERT_EQ_INT(5, ret.i);

  VALUE_t empty = {VALUE_str, {.s = strdup("")}};
  push_stack(config.vm->stack, empty);
  (void)lc_str_len(test_ctx(), NULL, config.itemroot);
  ret = pop_stack(config.vm->stack);
  ASSERT_EQ_INT(VALUE_int, ret.type);
  ASSERT_EQ_INT(0, ret.i);

  teardown_libcall_runtime();
}

void test_str_valtostr_converts_values_to_strings(void) {
  setup_libcall_runtime();

  assert_str_valtostr_result((VALUE_t){VALUE_int, {.i = -42}}, "-42");
  assert_str_valtostr_result((VALUE_t){VALUE_float, {.f = 3.5}}, "3.5");
  assert_str_valtostr_result((VALUE_t){VALUE_bool, {.i = 1}}, "true");
  assert_str_valtostr_result((VALUE_t){VALUE_bool, {.i = 0}}, "false");
  assert_str_valtostr_result(VALUE_NIL, "nil");

  char *original = strdup("already text");
  ASSERT_NOT_NULL(original);
  push_stack(config.vm->stack, (VALUE_t){VALUE_str, {.s = original}});
  (void)lc_str_valtostr(test_ctx(), NULL, config.itemroot);
  VALUE_t ret = pop_stack(config.vm->stack);
  ASSERT_EQ_INT(VALUE_str, ret.type);
  ASSERT_TRUE(ret.s == original);
  ASSERT_TRUE(strcmp(ret.s, "already text") == 0);
  FREE_STR(ret);

  teardown_libcall_runtime();
}

void test_str_case_libcalls_mutate_strings_in_place(void) {
  setup_libcall_runtime();

  assert_str_unary_result(lc_str_capitalise, "hello", "Hello");
  assert_str_unary_result(lc_str_capitalise, "aLREADY", "ALREADY");
  assert_str_unary_result(lc_str_capitalise, "", "");
  assert_str_unary_result(lc_str_upper, "MiXeD 123!", "MIXED 123!");
  assert_str_unary_result(lc_str_lower, "MiXeD 123!", "mixed 123!");

  teardown_libcall_runtime();
}

void test_str_trim_libcalls_return_trimmed_strings(void) {
  setup_libcall_runtime();

  assert_str_unary_result(lc_str_trim, " \t hello world \r\n", "hello world");
  assert_str_unary_result(lc_str_trim, "   \t\n", "");
  assert_str_unary_result(lc_str_trim, "already clean", "already clean");

  assert_str_unary_result(lc_str_ltrim, " \t hello world \r\n",
                          "hello world \r\n");
  assert_str_unary_result(lc_str_ltrim, "   \t\n", "");
  assert_str_unary_result(lc_str_ltrim, "already clean", "already clean");

  assert_str_unary_result(lc_str_rtrim, " \t hello world \r\n",
                          " \t hello world");
  assert_str_unary_result(lc_str_rtrim, "   \t\n", "");
  assert_str_unary_result(lc_str_rtrim, "already clean", "already clean");

  teardown_libcall_runtime();
}

void test_str_substr_returns_requested_byte_range(void) {
  setup_libcall_runtime();

  assert_str_substr_result("abcdef", 0, 3, "abc");
  assert_str_substr_result("abcdef", 2, 3, "cde");
  assert_str_substr_result("abcdef", 4, 99, "ef");
  assert_str_substr_result("abcdef", 6, 2, "");
  assert_str_substr_result("abcdef", 7, 2, "");

  push_stack(config.vm->stack, (VALUE_t){VALUE_str, {.s = strdup("abcdef")}});
  push_stack(config.vm->stack, (VALUE_t){VALUE_int, {.i = 2}});
  push_stack(config.vm->stack, (VALUE_t){VALUE_int, {.i = 0}});
  (void)lc_str_substr(test_ctx(), NULL, config.itemroot);
  VALUE_t ret = pop_stack(config.vm->stack);
  ASSERT_EQ_INT(VALUE_nil, ret.type);

  teardown_libcall_runtime();
}

void test_str_substr_invalid_args_return_nil(void) {
  setup_libcall_runtime();

  push_stack(config.vm->stack, (VALUE_t){VALUE_float, {.f = 1.25}});
  push_stack(config.vm->stack, (VALUE_t){VALUE_int, {.i = 0}});
  push_stack(config.vm->stack, (VALUE_t){VALUE_int, {.i = 1}});
  (void)lc_str_substr(test_ctx(), NULL, config.itemroot);
  VALUE_t ret = pop_stack(config.vm->stack);
  ASSERT_EQ_INT(VALUE_nil, ret.type);
  assert_invalid_args_detail_contains("str.substr");

  push_stack(config.vm->stack, (VALUE_t){VALUE_str, {.s = strdup("abcdef")}});
  push_stack(config.vm->stack, (VALUE_t){VALUE_float, {.f = 0.0}});
  push_stack(config.vm->stack, (VALUE_t){VALUE_int, {.i = 1}});
  (void)lc_str_substr(test_ctx(), NULL, config.itemroot);
  ret = pop_stack(config.vm->stack);
  ASSERT_EQ_INT(VALUE_nil, ret.type);
  assert_invalid_args_detail_contains("str.substr");

  push_stack(config.vm->stack, (VALUE_t){VALUE_str, {.s = strdup("abcdef")}});
  push_stack(config.vm->stack, (VALUE_t){VALUE_int, {.i = -1}});
  push_stack(config.vm->stack, (VALUE_t){VALUE_int, {.i = 1}});
  (void)lc_str_substr(test_ctx(), NULL, config.itemroot);
  ret = pop_stack(config.vm->stack);
  ASSERT_EQ_INT(VALUE_nil, ret.type);
  assert_invalid_args_detail_contains("str.substr start");

  push_stack(config.vm->stack, (VALUE_t){VALUE_str, {.s = strdup("abcdef")}});
  push_stack(config.vm->stack, (VALUE_t){VALUE_int, {.i = 0}});
  push_stack(config.vm->stack, (VALUE_t){VALUE_float, {.f = 1.0}});
  (void)lc_str_substr(test_ctx(), NULL, config.itemroot);
  ret = pop_stack(config.vm->stack);
  ASSERT_EQ_INT(VALUE_nil, ret.type);
  assert_invalid_args_detail_contains("str.substr");

  teardown_libcall_runtime();
}

void test_str_find_and_contains_return_expected_results(void) {
  setup_libcall_runtime();

  assert_str_find_result("abcdef", "abc", 0);
  assert_str_find_result("abcdef", "cd", 2);
  assert_str_find_result("abcdef", "f", 5);
  assert_str_find_result("abcdef", "missing", -1);
  assert_str_find_result("abcdef", "", 0);

  assert_str_contains_result("abcdef", "abc", 1);
  assert_str_contains_result("abcdef", "cd", 1);
  assert_str_contains_result("abcdef", "missing", 0);
  assert_str_contains_result("abcdef", "", 1);

  teardown_libcall_runtime();
}

void test_str_startswith_and_endswith_return_expected_results(void) {
  setup_libcall_runtime();

  assert_str_affix_result(lc_str_startswith, "abcdef", "abc", 1);
  assert_str_affix_result(lc_str_startswith, "abcdef", "abC", 0);
  assert_str_affix_result(lc_str_startswith, "abcdef", "bc", 0);
  assert_str_affix_result(lc_str_startswith, "abcdef", "abcdefg", 0);
  assert_str_affix_result(lc_str_startswith, "abcdef", "", 1);

  assert_str_affix_result(lc_str_endswith, "abcdef", "def", 1);
  assert_str_affix_result(lc_str_endswith, "abcdef", "dEf", 0);
  assert_str_affix_result(lc_str_endswith, "abcdef", "de", 0);
  assert_str_affix_result(lc_str_endswith, "abcdef", "zabcdef", 0);
  assert_str_affix_result(lc_str_endswith, "abcdef", "", 1);

  teardown_libcall_runtime();
}

void test_str_eqcasei_returns_expected_results(void) {
  setup_libcall_runtime();

  assert_str_affix_result(lc_str_eqcasei, "abcdef", "ABCDEF", 1);
  assert_str_affix_result(lc_str_eqcasei, "MiXeD 123!", "mixed 123!", 1);
  assert_str_affix_result(lc_str_eqcasei, "", "", 1);
  assert_str_affix_result(lc_str_eqcasei, "abcdef", "abcdeg", 0);
  assert_str_affix_result(lc_str_eqcasei, "abcdef", "abc", 0);
  assert_str_affix_result(lc_str_eqcasei, "abc", "abcdef", 0);

  teardown_libcall_runtime();
}

void test_str_replace_returns_expected_results(void) {
  setup_libcall_runtime();

  assert_str_replace_result("one two one", "one", "three", "three two three");
  assert_str_replace_result("aaaa", "aa", "b", "bb");
  assert_str_replace_result("abc", "x", "y", "abc");
  assert_str_replace_result("abc", "", "x", "abc");
  assert_str_replace_result("abc", "b", "", "ac");
  assert_str_replace_result("", "x", "y", "");

  teardown_libcall_runtime();
}

void test_str_repeat_returns_expected_results(void) {
  setup_libcall_runtime();

  assert_str_repeat_result("ab", 3, "ababab");
  assert_str_repeat_result("ab", 1, "ab");
  assert_str_repeat_result("ab", 0, "");
  assert_str_repeat_result("", 5, "");

  teardown_libcall_runtime();
}

void test_str_padleft_and_padright_return_expected_results(void) {
  setup_libcall_runtime();

  assert_str_pad_result(lc_str_padleft, "abc", 5, "  abc");
  assert_str_pad_result(lc_str_padright, "abc", 5, "abc  ");
  assert_str_pad_result(lc_str_padleft, "abc", 3, "abc");
  assert_str_pad_result(lc_str_padright, "abc", 2, "abc");
  assert_str_pad_result(lc_str_padleft, "", 2, "  ");
  assert_str_pad_result(lc_str_padright, "", 2, "  ");

  teardown_libcall_runtime();
}

void test_str_find_and_contains_invalid_args_return_contracts(void) {
  setup_libcall_runtime();

  push_stack(config.vm->stack, (VALUE_t){VALUE_float, {.f = 1.25}});
  push_stack(config.vm->stack, (VALUE_t){VALUE_str, {.s = strdup("needle")}});
  (void)lc_str_find(test_ctx(), NULL, config.itemroot);
  VALUE_t ret = pop_stack(config.vm->stack);
  ASSERT_EQ_INT(VALUE_nil, ret.type);
  assert_invalid_args_detail_contains("str.find");

  push_stack(config.vm->stack, (VALUE_t){VALUE_str, {.s = strdup("haystack")}});
  push_stack(config.vm->stack, (VALUE_t){VALUE_float, {.f = 1.25}});
  (void)lc_str_find(test_ctx(), NULL, config.itemroot);
  ret = pop_stack(config.vm->stack);
  ASSERT_EQ_INT(VALUE_nil, ret.type);
  assert_invalid_args_detail_contains("str.find");

  push_stack(config.vm->stack, (VALUE_t){VALUE_float, {.f = 1.25}});
  push_stack(config.vm->stack, (VALUE_t){VALUE_str, {.s = strdup("needle")}});
  (void)lc_str_contains(test_ctx(), NULL, config.itemroot);
  ret = pop_stack(config.vm->stack);
  ASSERT_EQ_INT(VALUE_bool, ret.type);
  ASSERT_EQ_INT(0, ret.i);
  assert_invalid_args_detail_contains("str.contains");

  teardown_libcall_runtime();
}

void test_str_startswith_and_endswith_invalid_args_return_contracts(void) {
  setup_libcall_runtime();

  push_stack(config.vm->stack, (VALUE_t){VALUE_float, {.f = 1.25}});
  push_stack(config.vm->stack, (VALUE_t){VALUE_str, {.s = strdup("needle")}});
  (void)lc_str_startswith(test_ctx(), NULL, config.itemroot);
  VALUE_t ret = pop_stack(config.vm->stack);
  ASSERT_EQ_INT(VALUE_bool, ret.type);
  ASSERT_EQ_INT(0, ret.i);
  assert_invalid_args_detail_contains("str.startswith");

  push_stack(config.vm->stack, (VALUE_t){VALUE_str, {.s = strdup("haystack")}});
  push_stack(config.vm->stack, (VALUE_t){VALUE_float, {.f = 1.25}});
  (void)lc_str_endswith(test_ctx(), NULL, config.itemroot);
  ret = pop_stack(config.vm->stack);
  ASSERT_EQ_INT(VALUE_bool, ret.type);
  ASSERT_EQ_INT(0, ret.i);
  assert_invalid_args_detail_contains("str.endswith");

  teardown_libcall_runtime();
}

void test_str_eqcasei_invalid_args_return_contracts(void) {
  setup_libcall_runtime();

  push_stack(config.vm->stack, (VALUE_t){VALUE_float, {.f = 1.25}});
  push_stack(config.vm->stack, (VALUE_t){VALUE_str, {.s = strdup("right")}});
  (void)lc_str_eqcasei(test_ctx(), NULL, config.itemroot);
  VALUE_t ret = pop_stack(config.vm->stack);
  ASSERT_EQ_INT(VALUE_bool, ret.type);
  ASSERT_EQ_INT(0, ret.i);
  assert_invalid_args_detail_contains("str.eqcasei");

  push_stack(config.vm->stack, (VALUE_t){VALUE_str, {.s = strdup("left")}});
  push_stack(config.vm->stack, (VALUE_t){VALUE_float, {.f = 1.25}});
  (void)lc_str_eqcasei(test_ctx(), NULL, config.itemroot);
  ret = pop_stack(config.vm->stack);
  ASSERT_EQ_INT(VALUE_bool, ret.type);
  ASSERT_EQ_INT(0, ret.i);
  assert_invalid_args_detail_contains("str.eqcasei");

  teardown_libcall_runtime();
}

void test_str_replace_invalid_args_return_nil(void) {
  setup_libcall_runtime();

  push_stack(config.vm->stack, (VALUE_t){VALUE_float, {.f = 1.25}});
  push_stack(config.vm->stack, (VALUE_t){VALUE_str, {.s = strdup("old")}});
  push_stack(config.vm->stack, (VALUE_t){VALUE_str, {.s = strdup("new")}});
  (void)lc_str_replace(test_ctx(), NULL, config.itemroot);
  VALUE_t ret = pop_stack(config.vm->stack);
  ASSERT_EQ_INT(VALUE_nil, ret.type);
  assert_invalid_args_detail_contains("str.replace");

  push_stack(config.vm->stack, (VALUE_t){VALUE_str, {.s = strdup("text")}});
  push_stack(config.vm->stack, (VALUE_t){VALUE_float, {.f = 1.25}});
  push_stack(config.vm->stack, (VALUE_t){VALUE_str, {.s = strdup("new")}});
  (void)lc_str_replace(test_ctx(), NULL, config.itemroot);
  ret = pop_stack(config.vm->stack);
  ASSERT_EQ_INT(VALUE_nil, ret.type);
  assert_invalid_args_detail_contains("str.replace");

  push_stack(config.vm->stack, (VALUE_t){VALUE_str, {.s = strdup("text")}});
  push_stack(config.vm->stack, (VALUE_t){VALUE_str, {.s = strdup("old")}});
  push_stack(config.vm->stack, (VALUE_t){VALUE_float, {.f = 1.25}});
  (void)lc_str_replace(test_ctx(), NULL, config.itemroot);
  ret = pop_stack(config.vm->stack);
  ASSERT_EQ_INT(VALUE_nil, ret.type);
  assert_invalid_args_detail_contains("str.replace");

  teardown_libcall_runtime();
}

void test_str_repeat_invalid_args_return_nil(void) {
  setup_libcall_runtime();

  push_stack(config.vm->stack, (VALUE_t){VALUE_float, {.f = 1.25}});
  push_stack(config.vm->stack, (VALUE_t){VALUE_int, {.i = 2}});
  (void)lc_str_repeat(test_ctx(), NULL, config.itemroot);
  VALUE_t ret = pop_stack(config.vm->stack);
  ASSERT_EQ_INT(VALUE_nil, ret.type);
  assert_invalid_args_detail_contains("str.repeat");

  push_stack(config.vm->stack, (VALUE_t){VALUE_str, {.s = strdup("text")}});
  push_stack(config.vm->stack, (VALUE_t){VALUE_float, {.f = 2.0}});
  (void)lc_str_repeat(test_ctx(), NULL, config.itemroot);
  ret = pop_stack(config.vm->stack);
  ASSERT_EQ_INT(VALUE_nil, ret.type);
  assert_invalid_args_detail_contains("str.repeat");

  push_stack(config.vm->stack, (VALUE_t){VALUE_str, {.s = strdup("text")}});
  push_stack(config.vm->stack, (VALUE_t){VALUE_int, {.i = -1}});
  (void)lc_str_repeat(test_ctx(), NULL, config.itemroot);
  ret = pop_stack(config.vm->stack);
  ASSERT_EQ_INT(VALUE_nil, ret.type);
  assert_invalid_args_detail_contains("str.repeat");

  teardown_libcall_runtime();
}

void test_str_growth_libcalls_enforce_string_limit(void) {
  setup_libcall_runtime();

  char *text = malloc(40001);
  ASSERT_NOT_NULL(text);
  memset(text, 'a', 40000);
  text[40000] = '\0';
  push_stack(config.vm->stack, (VALUE_t){VALUE_str, {.s = text}});
  push_stack(config.vm->stack, (VALUE_t){VALUE_str, {.s = strdup("a")}});
  push_stack(config.vm->stack, (VALUE_t){VALUE_str, {.s = strdup("aa")}});
  (void)lc_str_replace(test_ctx(), NULL, config.itemroot);
  VALUE_t ret = pop_stack(config.vm->stack);
  ASSERT_EQ_INT(VALUE_nil, ret.type);

  push_stack(config.vm->stack, (VALUE_t){VALUE_str, {.s = strdup("0123456789")}});
  push_stack(config.vm->stack, (VALUE_t){VALUE_int, {.i = 7000}});
  (void)lc_str_repeat(test_ctx(), NULL, config.itemroot);
  ret = pop_stack(config.vm->stack);
  ASSERT_EQ_INT(VALUE_nil, ret.type);

  push_stack(config.vm->stack, (VALUE_t){VALUE_str, {.s = strdup("text")}});
  push_stack(config.vm->stack, (VALUE_t){VALUE_int, {.i = (int64_t)SIN_MAX_STRING_BYTES + 1}});
  (void)lc_str_padleft(test_ctx(), NULL, config.itemroot);
  ret = pop_stack(config.vm->stack);
  ASSERT_EQ_INT(VALUE_nil, ret.type);

  teardown_libcall_runtime();
}

void test_str_padleft_and_padright_invalid_args_return_nil(void) {
  setup_libcall_runtime();

  push_stack(config.vm->stack, (VALUE_t){VALUE_float, {.f = 1.25}});
  push_stack(config.vm->stack, (VALUE_t){VALUE_int, {.i = 5}});
  (void)lc_str_padleft(test_ctx(), NULL, config.itemroot);
  VALUE_t ret = pop_stack(config.vm->stack);
  ASSERT_EQ_INT(VALUE_nil, ret.type);
  assert_invalid_args_detail_contains("str.padleft");

  push_stack(config.vm->stack, (VALUE_t){VALUE_str, {.s = strdup("text")}});
  push_stack(config.vm->stack, (VALUE_t){VALUE_float, {.f = 5.0}});
  (void)lc_str_padright(test_ctx(), NULL, config.itemroot);
  ret = pop_stack(config.vm->stack);
  ASSERT_EQ_INT(VALUE_nil, ret.type);
  assert_invalid_args_detail_contains("str.padright");

  push_stack(config.vm->stack, (VALUE_t){VALUE_str, {.s = strdup("text")}});
  push_stack(config.vm->stack, (VALUE_t){VALUE_int, {.i = 0}});
  (void)lc_str_padleft(test_ctx(), NULL, config.itemroot);
  ret = pop_stack(config.vm->stack);
  ASSERT_EQ_INT(VALUE_nil, ret.type);
  assert_invalid_args_detail_contains("str.padleft");

  push_stack(config.vm->stack, (VALUE_t){VALUE_str, {.s = strdup("text")}});
  push_stack(config.vm->stack, (VALUE_t){VALUE_int, {.i = -1}});
  (void)lc_str_padright(test_ctx(), NULL, config.itemroot);
  ret = pop_stack(config.vm->stack);
  ASSERT_EQ_INT(VALUE_nil, ret.type);
  assert_invalid_args_detail_contains("str.padright");

  teardown_libcall_runtime();
}

void test_str_libcall_invalidargs_uses_context_itemroot(void) {
  setup_libcall_runtime();

  assert_float_string_libcall_uses_context_itemroot(lc_str_capitalise,
      "str.capitalise");
  assert_float_string_libcall_uses_context_itemroot(lc_str_upper,
      "str.upper");
  assert_float_string_libcall_uses_context_itemroot(lc_str_lower,
      "str.lower");
  assert_float_string_libcall_uses_context_itemroot(lc_str_len,
      "str.len");
  assert_float_string_libcall_uses_context_itemroot(lc_str_trim,
      "str.trim");
  assert_float_string_libcall_uses_context_itemroot(lc_str_ltrim,
      "str.ltrim");
  assert_float_string_libcall_uses_context_itemroot(lc_str_rtrim,
      "str.rtrim");

  ITEM_t *context_root = make_root_item("context-root");
  ASSERT_NOT_NULL(context_root);

  RuntimeContext ctx;
  runtime_context_init(&ctx, config.vm);
  ctx.itemroot = context_root;

  push_stack(config.vm->stack, (VALUE_t){VALUE_str, {.s = strdup("abcdef")}});
  push_stack(config.vm->stack, (VALUE_t){VALUE_int, {.i = -1}});
  push_stack(config.vm->stack, (VALUE_t){VALUE_int, {.i = 1}});
  (void)lc_str_substr(&ctx, NULL, context_root);
  VALUE_t ret = pop_stack(config.vm->stack);
  ASSERT_EQ_INT(VALUE_nil, ret.type);

  ITEM_t *context_msg = find_item(context_root, "error.msg");
  ASSERT_NOT_NULL(context_msg);
  ASSERT_EQ_INT(VALUE_str, context_msg->value.type);
  ASSERT_TRUE(strstr(context_msg->value.s, "str.substr start") != NULL);

  destroy_item(context_root);

  teardown_libcall_runtime();
}

void test_libcall_output_formats_values(void) {
  typedef struct {
    VALUE_t value;
    const char *expected;
  } output_case_t;

  setup_libcall_runtime();

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

  config.maxconns = 1;
  line = calloc((size_t)config.maxconns, sizeof(LINE_t));
  ASSERT_NOT_NULL(line);
  line[0].status = LINE_idle;
  line[0].telnet = telnet_init(NULL, capture_telnet_event, 0, NULL);
  ASSERT_NOT_NULL(line[0].telnet);

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

  teardown_libcall_runtime();
}
