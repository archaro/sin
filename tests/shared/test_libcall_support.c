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
#include "test_network_fixture.h"

uint8_t *lc_task_newgametask(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item);
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

extern CONFIG_t config;

#include "shared/test_libcall_support.h"

static RuntimeContext test_runtime_ctx;
static NetworkRuntime *test_network;
static uv_loop_t test_network_loop;
static uv_tcp_t test_network_listener;
static uv_tcp_t test_network_listener_ipv4;

static bool test_network_start(size_t maxconns) {
  if (test_network) return true;
  if (uv_loop_init(&test_network_loop) != 0) return false;
  test_network = network_runtime_create(&test_network_loop,
                                        &test_network_listener,
                                        &test_network_listener_ipv4,
                                        maxconns);
  if (!test_network) {
    uv_loop_close(&test_network_loop);
    return false;
  }
  return true;
}

static void test_network_stop(void) {
  if (!test_network) return;
  network_runtime_shutdown(test_network);
  uv_walk(&test_network_loop, network_close_walk_cb, test_network);
  while (uv_run(&test_network_loop, UV_RUN_DEFAULT) != 0) {}
  ASSERT_TRUE(network_runtime_destroy(test_network));
  test_network = NULL;
  ASSERT_EQ_INT(0, uv_loop_close(&test_network_loop));
}

NetworkRuntime *test_network_runtime(void) {
  return test_network;
}

bool test_network_reset(size_t maxconns) {
  test_network_stop();
  return test_network_start(maxconns);
}

void test_network_clear(void) {
  test_network_stop();
}

void test_network_drain(void) {
  if (!test_network) return;
  while (uv_run(&test_network_loop, UV_RUN_DEFAULT) != 0) {}
}

RuntimeContext *test_ctx(void) {
  runtime_context_init(&test_runtime_ctx, config.vm);
  test_runtime_ctx.itemstore = config.itemstore_ctx;
  test_runtime_ctx.strict_runtime_contracts = config.strict_runtime_contracts;
  test_runtime_ctx.network = test_network;
  test_runtime_ctx.inputline_name = config.inputline;
  test_runtime_ctx.inputtext_name = config.inputtext;
  test_runtime_ctx.loop = config.loop;
  test_runtime_ctx.safe_shutdown = &config.safe_shutdown;
  return &test_runtime_ctx;
}
void setup_libcall_runtime(void) {
  memset(&config, 0, sizeof(config));
  init_errmsg();
  config.itemstore_ctx = itemstore_owner(make_root_item("root"));
  ASSERT_NOT_NULL(itemstore_root(config.itemstore_ctx));
  config.vm = make_vm();
  ASSERT_NOT_NULL(config.vm);
  config.inputline = strdup("input.line");
  config.inputtext = strdup("input.text");
  ASSERT_NOT_NULL(config.inputline);
  ASSERT_NOT_NULL(config.inputtext);
  ASSERT_TRUE(test_network_start(8));
}

void teardown_libcall_runtime(void) {
  test_network_stop();
  free(config.inputline);
  free(config.inputtext);
  config.inputline = NULL;
  config.inputtext = NULL;
  destroy_vm(config.vm);
  destroy_item(itemstore_root(config.itemstore_ctx));
}
void assert_invalid_args_detail_contains(const char *expected) {
  ITEM_t *err = find_item(itemstore_root(config.itemstore_ctx), "error");
  ASSERT_NOT_NULL(err);
  ASSERT_EQ_INT(VALUE_int, item_value(err)->type);
  ASSERT_EQ_INT(ERR_RUNTIME_INVALIDARGS, item_value(err)->i);
  ITEM_t *msg = find_item(itemstore_root(config.itemstore_ctx), "error.msg");
  ASSERT_NOT_NULL(msg);
  ASSERT_EQ_INT(VALUE_str, item_value(msg)->type);
  ASSERT_TRUE(strstr(item_value(msg)->s, expected) != NULL);
}

void assert_invalid_args_float_detail_contains(const char *expected) {
  assert_invalid_args_detail_contains("float");
  assert_invalid_args_detail_contains(expected);
}

void assert_bool_return(VALUE_t value, int expected) {
  ASSERT_EQ_INT(VALUE_bool, value.type);
  ASSERT_EQ_INT(expected, value.i);
}
