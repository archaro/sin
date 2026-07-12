#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <stdint.h>

#include "libcall.h"
#include "config.h"
#include "error.h"
#include "interpret.h"
#include "item.h"
#include "test_assert.h"
#include "vm.h"
#include "memory.h"

#include "network.h"

uint8_t *lc_task_newgametask(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item);
uint8_t *lc_task_killtask(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item);
uint8_t *lc_net_write(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item);
uint8_t *lc_sys_log(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item);
uint8_t *lc_sys_compile(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item);
uint8_t *lc_str_capitalise(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item);
uint8_t *lc_str_upper(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item);
uint8_t *lc_str_lower(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item);
uint8_t *lc_str_len(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item);
uint8_t *lc_str_trim(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item);
uint8_t *lc_str_ltrim(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item);
uint8_t *lc_str_rtrim(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item);
uint8_t *lc_str_substr(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item);
uint8_t *lc_str_find(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item);
uint8_t *lc_str_contains(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item);
uint8_t *lc_str_startswith(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item);
uint8_t *lc_str_endswith(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item);

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

  ASSERT_TRUE(libcall_lookup_token("task", "newgametask", &token, &args));
  ASSERT_EQ_INT(3, args);
  args = 0;
  ASSERT_TRUE(libcall_token_arg_count(token, &args));
  ASSERT_EQ_INT(3, args);
  ASSERT_TRUE(!libcall_lookup_token("missing", "missing", &token, &args));

  alloc_test_fail_after(0);
  token = 0;
  args = 0;
  ASSERT_TRUE(libcall_lookup_token("sys", "log", &token, &args));
  ASSERT_EQ_INT(1, args);
  ASSERT_TRUE(!libcall_lookup_token("missing", "missing", &token, &args));
  alloc_test_fail_after(-1);
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

void test_libcall_name_duplicate_detection(void) {
  const LIBCALL_t dup_calls[] = {
    {"sys", "log", 1, 1, 1, NULL},
    {"sys", "log", 1, 9, 1, NULL},
    {NULL, NULL, -1, -1, 0, NULL}
  };

  ASSERT_TRUE(!libcall_names_unique(dup_calls));
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
  setup_libcall_runtime();

  assert_sys_log_output((VALUE_t){VALUE_float, {.f = 3.5}}, "3.5");
  assert_sys_log_output((VALUE_t){VALUE_str, {.s = strdup("%s literal")}},
                        "%s literal");

  config.maxconns = 1;
  line = calloc((size_t)config.maxconns, sizeof(LINE_t));
  ASSERT_NOT_NULL(line);
  line[0].status = LINE_idle;
  line[0].telnet = telnet_init(NULL, capture_telnet_event, 0, NULL);
  ASSERT_NOT_NULL(line[0].telnet);

  assert_net_write_output((VALUE_t){VALUE_str, {.s = strdup("hello")}},
                          "hello");
  assert_net_write_output((VALUE_t){VALUE_int, {.i = -42}}, "-42");
  assert_net_write_output((VALUE_t){VALUE_float, {.f = 3.5}}, "3.5");
  assert_net_write_output((VALUE_t){VALUE_bool, {.i = 1}}, "true");
  assert_net_write_output((VALUE_t){VALUE_bool, {.i = 0}}, "false");
  assert_net_write_output(VALUE_NIL, "");

  teardown_libcall_runtime();
}
