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
#include "item.h"
#include "test_assert.h"
#include "test_helpers.h"
#include "task.h"
#include "vm.h"
#include "memory.h"
#include "runtime_value.h"
#include "string_limits.h"
#include "version.h"

#include "network.h"

uint8_t *lc_task_newgametask(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item);
uint8_t *lc_task_killtask(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item);
uint8_t *lc_task_thisid(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item);
uint8_t *lc_task_exists(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item);
uint8_t *lc_task_count(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item);
void execute_task_cb(uv_timer_t *req);
uint8_t *lc_net_write(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item);
uint8_t *lc_net_input(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item);
LINE_t *add_line(uv_tcp_t *line_handle);
uint8_t *lc_net_flush(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item);
uint8_t *lc_net_ditch(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item);
uint8_t *lc_net_echo(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item);
uint8_t *lc_net_maxlines(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item);
uint8_t *lc_net_connected(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item);
uint8_t *lc_net_address(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item);
uint8_t *lc_sys_log(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item);
uint8_t *lc_sys_backup(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item);
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

extern LINE_t *line;
extern CONFIG_t config;

#include "shared/test_libcall_support.h"

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
  reset_telnet_capture();

  push_stack(config.vm->stack, (VALUE_t){VALUE_int, {.i = 0}});
  push_stack(config.vm->stack, out);
  (void)lc_net_write(test_ctx(), NULL, itemstore_root(config.itemstore_ctx));
  VALUE_t ret = pop_stack(config.vm->stack);
  ASSERT_EQ_INT(VALUE_nil, ret.type);
  ASSERT_TRUE(strcmp(telnet_capture, expected) == 0);
}

static uint8_t *test_noop_libcall(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item) {
  (void)ctx; (void)item;
  return nextop;
}
void test_libcall_registry_roundtrip(void) {
  libcall_reset_registry_for_tests();
  ASSERT_TRUE(libcall_init_registry());
  ASSERT_TRUE(libcall_validate_registry());

  const char *previous_libname = NULL;
  int previous_call_index = -1;
  for (size_t i = 0; libcalls[i].libname != NULL; i++) {
    if (previous_libname &&
        strcmp(previous_libname, libcalls[i].libname) != 0) {
      if (strcmp(previous_libname, "sys") != 0) {
        ASSERT_TRUE(strcmp(previous_libname, libcalls[i].libname) < 0);
      }
      ASSERT_TRUE(strcmp(libcalls[i].libname, "sys") != 0);
      previous_call_index = -1;
    }
    ASSERT_TRUE(libcalls[i].call_index > previous_call_index);
    previous_libname = libcalls[i].libname;
    previous_call_index = libcalls[i].call_index;
  }

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
  ASSERT_EQ_INT(9, token);
  ASSERT_EQ_INT(0, args);
  ASSERT_TRUE(libcall_func_token(token) == lc_sys_save);
  ASSERT_EQ_INT(1, libcalls[token].lib_index);
  ASSERT_EQ_INT(9, libcalls[token].call_index);
  ASSERT_EQ_INT(0, libcalls[token].args);
  ASSERT_TRUE(libcalls[token].func == lc_sys_save);

  ASSERT_TRUE(libcall_lookup_token("net", "echo", &token, &args));
  ASSERT_EQ_INT(1, args);
  ASSERT_EQ_INT(3, libcalls[token].lib_index);
  ASSERT_EQ_INT(4, libcalls[token].call_index);
  ASSERT_TRUE(libcalls[token].func == lc_net_echo);

  ASSERT_TRUE(libcall_lookup_token("net", "maxlines", &token, &args));
  ASSERT_EQ_INT(26, token);
  ASSERT_EQ_INT(0, args);
  ASSERT_EQ_INT(3, libcalls[token].lib_index);
  ASSERT_EQ_INT(5, libcalls[token].call_index);
  ASSERT_TRUE(libcalls[token].func == lc_net_maxlines);

  ASSERT_TRUE(libcall_lookup_token("net", "connected", &token, &args));
  ASSERT_EQ_INT(27, token);
  ASSERT_EQ_INT(1, args);
  ASSERT_EQ_INT(3, libcalls[token].lib_index);
  ASSERT_EQ_INT(6, libcalls[token].call_index);
  ASSERT_TRUE(libcalls[token].func == lc_net_connected);

  ASSERT_TRUE(libcall_lookup_token("net", "address", &token, &args));
  ASSERT_EQ_INT(28, token);
  ASSERT_EQ_INT(1, args);
  ASSERT_EQ_INT(3, libcalls[token].lib_index);
  ASSERT_EQ_INT(7, libcalls[token].call_index);
  ASSERT_TRUE(libcalls[token].func == lc_net_address);

  const struct {
    const char *name;
    uint8_t token;
    int call_index;
    uint8_t arity;
    OP_t handler;
  } sys_introspection_calls[] = {
    {"thisitem", 10, 10, 0, lc_sys_thisitem},
    {"parentitem", 11, 11, 0, lc_sys_parentitem},
    {"itemtype", 12, 12, 1, lc_sys_itemtype},
    {"childcount", 13, 13, 1, lc_sys_childcount},
    {"rootcount", 14, 14, 0, lc_sys_rootcount},
    {"version", 15, 15, 0, lc_sys_version},
    {"now", 16, 16, 0, lc_sys_now},
    {"monotime", 17, 17, 0, lc_sys_monotime},
    {"calleritem", 18, 18, 0, lc_sys_calleritem},
    {"paramcount", 19, 19, 1, lc_sys_paramcount},
    {"source", 20, 20, 1, lc_sys_source},
  };
  for (size_t i = 0; i < sizeof(sys_introspection_calls) /
                              sizeof(sys_introspection_calls[0]); i++) {
    const uint8_t expected_token = sys_introspection_calls[i].token;
    token = 0;
    args = 255;
    ASSERT_TRUE(libcall_lookup_token("sys", sys_introspection_calls[i].name,
                                     &token, &args));
    ASSERT_EQ_INT(expected_token, token);
    ASSERT_EQ_INT(sys_introspection_calls[i].arity, args);
    ASSERT_EQ_INT(1, libcalls[token].lib_index);
    ASSERT_EQ_INT(sys_introspection_calls[i].call_index,
                  libcalls[token].call_index);
    ASSERT_EQ_INT(sys_introspection_calls[i].arity, libcalls[token].args);
    ASSERT_TRUE(libcalls[token].func == sys_introspection_calls[i].handler);
    ASSERT_TRUE(libcall_func_token(token) == sys_introspection_calls[i].handler);
  }

  const struct {
    const char *name;
    uint8_t token;
    int call_index;
    uint8_t arity;
    OP_t handler;
  } task_calls[] = {
    {"newgametask", 47, 0, 3, lc_task_newgametask},
    {"killtask", 48, 1, 1, lc_task_killtask},
    {"thisid", 49, 2, 0, lc_task_thisid},
    {"exists", 50, 3, 1, lc_task_exists},
    {"count", 51, 4, 0, lc_task_count},
  };
  for (size_t i = 0; i < sizeof(task_calls) / sizeof(task_calls[0]); i++) {
    token = 0;
    args = 255;
    ASSERT_TRUE(libcall_lookup_token("task", task_calls[i].name, &token,
                                     &args));
    ASSERT_EQ_INT(task_calls[i].token, token);
    ASSERT_EQ_INT(task_calls[i].arity, args);
    ASSERT_EQ_INT(2, libcalls[token].lib_index);
    ASSERT_EQ_INT(task_calls[i].call_index, libcalls[token].call_index);
    ASSERT_EQ_INT(task_calls[i].arity, libcalls[token].args);
    ASSERT_TRUE(libcalls[token].func == task_calls[i].handler);
    ASSERT_TRUE(libcall_func_token(token) == task_calls[i].handler);
    args = 255;
    ASSERT_TRUE(libcall_token_arg_count(token, &args));
    ASSERT_EQ_INT(task_calls[i].arity, args);
  }
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
  ASSERT_EQ_INT(24, token);
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
  config.itemstore_ctx = itemstore_owner(make_root_item("root"));
  ASSERT_NOT_NULL(itemstore_root(config.itemstore_ctx));
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

  ITEM_t *code = insert_code_item(itemstore_root(config.itemstore_ctx), "test.missinglibcall",
                                  sizeof(template_bytecode), bytecode);
  ASSERT_NOT_NULL(code);

  VALUE_t v1 = interpret(test_ctx(), code);
  VALUE_t v2 = interpret(test_ctx(), code);
  ASSERT_EQ_INT(VALUE_nil, v1.type);
  ASSERT_EQ_INT(VALUE_nil, v2.type);
  ITEM_t *err_item = find_item(itemstore_root(config.itemstore_ctx), "error");
  ASSERT_NOT_NULL(err_item);
  ASSERT_EQ_INT(VALUE_int, item_value(err_item)->type);
  ASSERT_EQ_INT(ERR_RUNTIME_INVLIB, item_value(err_item)->i);
  ITEM_t *err_msg = find_item(itemstore_root(config.itemstore_ctx), "error.msg");
  ASSERT_NOT_NULL(err_msg);
  ASSERT_EQ_INT(VALUE_str, item_value(err_msg)->type);
  ASSERT_TRUE(strstr(item_value(err_msg)->s, "Unknown libcall token 255") != NULL);
  ASSERT_EQ_INT(-1, config.vm->stack->current);
  ASSERT_EQ_INT(-1, config.vm->callstack->current);

  destroy_vm(config.vm);
  destroy_item(itemstore_root(config.itemstore_ctx));
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

  config.maxconns = 1;
  line = calloc((size_t)config.maxconns, sizeof(LINE_t));
  ASSERT_NOT_NULL(line);
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

  config.maxconns = 1;
  line = calloc((size_t)config.maxconns, sizeof(LINE_t));
  ASSERT_NOT_NULL(line);
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
