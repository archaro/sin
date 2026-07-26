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

void test_net_maxlines_returns_configured_slot_bound(void) {
  setup_libcall_runtime();

  set_error_item(itemstore_root(config.itemstore_ctx), ERR_NETWORK_ERROR, "prior error", NULL);
  ASSERT_EQ_INT(0, size_stack(config.vm->stack));
  (void)lc_net_maxlines(test_ctx(), NULL, itemstore_root(config.itemstore_ctx));
  ASSERT_EQ_INT(1, size_stack(config.vm->stack));
  VALUE_t ret = pop_stack(config.vm->stack);
  ASSERT_EQ_INT(VALUE_int, ret.type);
  ASSERT_EQ_INT(0, ret.i);
  ITEM_t *err = find_item(itemstore_root(config.itemstore_ctx), "error");
  ASSERT_NOT_NULL(err);
  ASSERT_EQ_INT(ERR_NETWORK_ERROR, item_value(err)->i);

  config.maxconns = 37;
  (void)lc_net_maxlines(test_ctx(), NULL, itemstore_root(config.itemstore_ctx));
  ASSERT_EQ_INT(1, size_stack(config.vm->stack));
  ret = pop_stack(config.vm->stack);
  ASSERT_EQ_INT(VALUE_int, ret.type);
  ASSERT_EQ_INT(37, ret.i);
  err = find_item(itemstore_root(config.itemstore_ctx), "error");
  ASSERT_NOT_NULL(err);
  ASSERT_EQ_INT(ERR_NETWORK_ERROR, item_value(err)->i);

  teardown_libcall_runtime();
}
void test_net_write_ignores_non_writable_lines(void) {
  setup_libcall_runtime();

  config.maxconns = 3;
  line = calloc((size_t)config.maxconns, sizeof(LINE_t));
  ASSERT_NOT_NULL(line);
  const LINE_STATUS_t statuses[] = {
    LINE_empty, LINE_connecting, LINE_disconnecting
  };
  for (size_t i = 0; i < sizeof(statuses) / sizeof(statuses[0]); i++) {
    line[i].status = statuses[i];
    line[i].telnet = NULL;

    push_stack(config.vm->stack, (VALUE_t){VALUE_int, {.i = (int64_t)i}});
    push_stack(config.vm->stack,
               (VALUE_t){VALUE_str, {.s = strdup("hello")}});
    (void)lc_net_write(test_ctx(), NULL, itemstore_root(config.itemstore_ctx));
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
  (void)lc_net_input(ctx, NULL, itemstore_root(config.itemstore_ctx));
  VALUE_t ret = pop_stack(config.vm->stack);
  ASSERT_EQ_INT(VALUE_int, ret.type);
  ASSERT_EQ_INT(1, ret.i);
  ITEM_t *input_line_item = find_item(itemstore_root(config.itemstore_ctx), "input.line");
  ASSERT_NOT_NULL(input_line_item);
  ASSERT_EQ_INT(0, item_value(input_line_item)->i);

  (void)lc_net_input(ctx, NULL, itemstore_root(config.itemstore_ctx));
  ret = pop_stack(config.vm->stack);
  ASSERT_EQ_INT(VALUE_int, ret.type);
  ASSERT_EQ_INT(3, ret.i);
  ASSERT_EQ_INT(1, item_value(input_line_item)->i);
  ITEM_t *input_text_item = find_item(itemstore_root(config.itemstore_ctx), "input.text");
  ASSERT_NOT_NULL(input_text_item);
  ASSERT_TRUE(strcmp(item_value(input_text_item)->s, "hello") == 0);

  (void)lc_net_input(ctx, NULL, itemstore_root(config.itemstore_ctx));
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
  (void)lc_net_ditch(test_ctx(), NULL, itemstore_root(config.itemstore_ctx));
  VALUE_t ret = pop_stack(config.vm->stack);
  ASSERT_EQ_INT(VALUE_bool, ret.type);
  ASSERT_EQ_INT(1, ret.i);
  ASSERT_EQ_INT(LINE_disconnecting, line[1].status);

  push_stack(config.vm->stack, (VALUE_t){VALUE_int, {.i = 1}});
  (void)lc_net_ditch(test_ctx(), NULL, itemstore_root(config.itemstore_ctx));
  ret = pop_stack(config.vm->stack);
  ASSERT_EQ_INT(VALUE_bool, ret.type);
  ASSERT_EQ_INT(0, ret.i);

  reset_telnet_capture();
  push_stack(config.vm->stack, (VALUE_t){VALUE_int, {.i = 1}});
  push_stack(config.vm->stack,
             (VALUE_t){VALUE_str, {.s = strdup("after ditch")}});
  (void)lc_net_write(test_ctx(), NULL, itemstore_root(config.itemstore_ctx));
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
  (void)lc_net_flush(test_ctx(), NULL, itemstore_root(config.itemstore_ctx));
  VALUE_t ret = pop_stack(config.vm->stack);
  ASSERT_EQ_INT(VALUE_bool, ret.type);
  ASSERT_EQ_INT(1, ret.i);

  push_stack(config.vm->stack, (VALUE_t){VALUE_int, {.i = 1}});
  (void)lc_net_flush(test_ctx(), NULL, itemstore_root(config.itemstore_ctx));
  ret = pop_stack(config.vm->stack);
  ASSERT_EQ_INT(VALUE_bool, ret.type);
  ASSERT_EQ_INT(0, ret.i);
  ITEM_t *err = find_item(itemstore_root(config.itemstore_ctx), "error");
  ASSERT_NOT_NULL(err);
  ASSERT_EQ_INT(ERR_NETWORK_ERROR, item_value(err)->i);

  push_stack(config.vm->stack, (VALUE_t){VALUE_float, {.f = 0.0}});
  (void)lc_net_flush(test_ctx(), NULL, itemstore_root(config.itemstore_ctx));
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
    (void)lc_net_ditch(test_ctx(), NULL, itemstore_root(config.itemstore_ctx));
    VALUE_t ret = pop_stack(config.vm->stack);
    ASSERT_EQ_INT(VALUE_bool, ret.type);
    ASSERT_EQ_INT(0, ret.i);
    ITEM_t *err = find_item(itemstore_root(config.itemstore_ctx), "error");
    ASSERT_NOT_NULL(err);
    ASSERT_EQ_INT(ERR_NETWORK_ERROR, item_value(err)->i);
  }

  teardown_libcall_runtime();
}

void test_net_ditch_invalid_line_returns_nil(void) {
  setup_libcall_runtime();

  config.maxconns = 1;
  line = calloc((size_t)config.maxconns, sizeof(LINE_t));
  ASSERT_NOT_NULL(line);

  push_stack(config.vm->stack, (VALUE_t){VALUE_float, {.f = 0.0}});
  (void)lc_net_ditch(test_ctx(), NULL, itemstore_root(config.itemstore_ctx));
  VALUE_t ret = pop_stack(config.vm->stack);
  ASSERT_EQ_INT(VALUE_nil, ret.type);
  assert_invalid_args_detail_contains("net.ditch");

  push_stack(config.vm->stack, (VALUE_t){VALUE_int, {.i = -1}});
  (void)lc_net_ditch(test_ctx(), NULL, itemstore_root(config.itemstore_ctx));
  ret = pop_stack(config.vm->stack);
  ASSERT_EQ_INT(VALUE_nil, ret.type);
  assert_invalid_args_detail_contains("net.ditch");

  teardown_libcall_runtime();
}

static void assert_telnet_capture_bytes(const unsigned char *expected,
                                        size_t expected_len) {
  ASSERT_EQ_INT(expected_len, telnet_capture_len);
  ASSERT_TRUE(memcmp(telnet_capture, expected, expected_len) == 0);
}

static VALUE_t call_net_echo(VALUE_t value) {
  push_stack(config.vm->stack, value);
  (void)lc_net_echo(test_ctx(), NULL, itemstore_root(config.itemstore_ctx));
  return pop_stack(config.vm->stack);
}

void test_net_echo_negotiates_current_line_and_consumes_values(void) {
  static const unsigned char will_echo[] = {
    TELNET_IAC, TELNET_WILL, TELNET_TELOPT_ECHO
  };
  static const unsigned char wont_echo[] = {
    TELNET_IAC, TELNET_WONT, TELNET_TELOPT_ECHO
  };
  static const unsigned char peer_do_echo[] = {
    TELNET_IAC, TELNET_DO, TELNET_TELOPT_ECHO
  };

  setup_libcall_runtime();
  config.maxconns = 2;
  config.lastconn = 1;
  line = calloc((size_t)config.maxconns, sizeof(LINE_t));
  ASSERT_NOT_NULL(line);
  line[0].status = LINE_idle;
  line[1].status = LINE_idle;
  line[1].telnet = telnet_init(NULL, capture_telnet_event, 0, NULL);
  ASSERT_NOT_NULL(line[1].telnet);

  reset_telnet_capture();
  VALUE_t ret = call_net_echo((VALUE_t){VALUE_int, {.i = 0}});
  ASSERT_EQ_INT(VALUE_nil, ret.type);
  assert_telnet_capture_bytes(will_echo, sizeof(will_echo));

  telnet_recv(line[1].telnet, (const char *)peer_do_echo, sizeof(peer_do_echo));
  reset_telnet_capture();
  ret = call_net_echo((VALUE_t){VALUE_str, {.s = strdup("enabled")}});
  ASSERT_EQ_INT(VALUE_nil, ret.type);
  assert_telnet_capture_bytes(wont_echo, sizeof(wont_echo));

  teardown_libcall_runtime();
}

void test_net_echo_ignores_unavailable_current_line(void) {
  setup_libcall_runtime();
  config.maxconns = 1;
  line = calloc((size_t)config.maxconns, sizeof(LINE_t));
  ASSERT_NOT_NULL(line);
  set_error_item(itemstore_root(config.itemstore_ctx), ERR_NETWORK_ERROR, "prior error", NULL);

  config.lastconn = 1;
  VALUE_t ret = call_net_echo((VALUE_t){VALUE_str, {.s = strdup("value")}});
  ASSERT_EQ_INT(VALUE_nil, ret.type);
  ITEM_t *error = find_item(itemstore_root(config.itemstore_ctx), "error");
  ASSERT_NOT_NULL(error);
  ASSERT_EQ_INT(ERR_NETWORK_ERROR, item_value(error)->i);

  config.lastconn = 0;
  reset_telnet_capture();
  ret = call_net_echo((VALUE_t){VALUE_bool, {.i = 0}});
  ASSERT_EQ_INT(VALUE_nil, ret.type);
  ASSERT_EQ_INT(0, telnet_capture_len);
  ASSERT_EQ_INT(ERR_NETWORK_ERROR, item_value(error)->i);

  line[0].status = LINE_disconnecting;
  line[0].telnet = telnet_init(NULL, capture_telnet_event, 0, NULL);
  ASSERT_NOT_NULL(line[0].telnet);
  reset_telnet_capture();
  ret = call_net_echo((VALUE_t){VALUE_bool, {.i = 1}});
  ASSERT_EQ_INT(VALUE_nil, ret.type);
  ASSERT_EQ_INT(0, telnet_capture_len);
  ASSERT_EQ_INT(ERR_NETWORK_ERROR, item_value(error)->i);

  teardown_libcall_runtime();
}

void test_net_connected_reports_writable_telnet_states(void) {
  setup_libcall_runtime();

  config.maxconns = 6;
  line = calloc((size_t)config.maxconns, sizeof(LINE_t));
  ASSERT_NOT_NULL(line);

  line[0].status = LINE_connecting;
  line[0].telnet = telnet_init(NULL, capture_telnet_event, 0, NULL);
  ASSERT_NOT_NULL(line[0].telnet);
  line[1].status = LINE_idle;
  line[1].telnet = telnet_init(NULL, capture_telnet_event, 0, NULL);
  ASSERT_NOT_NULL(line[1].telnet);
  line[2].status = LINE_data;
  line[2].telnet = telnet_init(NULL, capture_telnet_event, 0, NULL);
  ASSERT_NOT_NULL(line[2].telnet);
  line[3].status = LINE_disconnecting;
  line[3].telnet = telnet_init(NULL, capture_telnet_event, 0, NULL);
  ASSERT_NOT_NULL(line[3].telnet);
  line[4].status = LINE_empty;
  line[5].status = LINE_idle;

  set_error_item(itemstore_root(config.itemstore_ctx), ERR_NETWORK_ERROR, "prior error", NULL);
  ITEM_t *err = find_item(itemstore_root(config.itemstore_ctx), "error");
  ASSERT_NOT_NULL(err);
  ASSERT_EQ_INT(ERR_NETWORK_ERROR, item_value(err)->i);

  push_stack(config.vm->stack, (VALUE_t){VALUE_int, {.i = 1}});
  (void)lc_net_connected(test_ctx(), NULL, itemstore_root(config.itemstore_ctx));
  VALUE_t ret = pop_stack(config.vm->stack);
  ASSERT_EQ_INT(VALUE_bool, ret.type);
  ASSERT_EQ_INT(1, ret.i);

  push_stack(config.vm->stack, (VALUE_t){VALUE_int, {.i = 2}});
  (void)lc_net_connected(test_ctx(), NULL, itemstore_root(config.itemstore_ctx));
  ret = pop_stack(config.vm->stack);
  ASSERT_EQ_INT(VALUE_bool, ret.type);
  ASSERT_EQ_INT(1, ret.i);

  push_stack(config.vm->stack, (VALUE_t){VALUE_int, {.i = 0}});
  (void)lc_net_connected(test_ctx(), NULL, itemstore_root(config.itemstore_ctx));
  ret = pop_stack(config.vm->stack);
  ASSERT_EQ_INT(VALUE_bool, ret.type);
  ASSERT_EQ_INT(0, ret.i);

  push_stack(config.vm->stack, (VALUE_t){VALUE_int, {.i = 3}});
  (void)lc_net_connected(test_ctx(), NULL, itemstore_root(config.itemstore_ctx));
  ret = pop_stack(config.vm->stack);
  ASSERT_EQ_INT(VALUE_bool, ret.type);
  ASSERT_EQ_INT(0, ret.i);

  push_stack(config.vm->stack, (VALUE_t){VALUE_int, {.i = 4}});
  (void)lc_net_connected(test_ctx(), NULL, itemstore_root(config.itemstore_ctx));
  ret = pop_stack(config.vm->stack);
  ASSERT_EQ_INT(VALUE_bool, ret.type);
  ASSERT_EQ_INT(0, ret.i);

  push_stack(config.vm->stack, (VALUE_t){VALUE_int, {.i = 5}});
  (void)lc_net_connected(test_ctx(), NULL, itemstore_root(config.itemstore_ctx));
  ret = pop_stack(config.vm->stack);
  ASSERT_EQ_INT(VALUE_bool, ret.type);
  ASSERT_EQ_INT(0, ret.i);

  ASSERT_EQ_INT(ERR_NETWORK_ERROR, item_value(err)->i);

  teardown_libcall_runtime();
}

void test_net_connected_invalid_line_returns_nil(void) {
  setup_libcall_runtime();

  config.maxconns = 1;
  line = calloc((size_t)config.maxconns, sizeof(LINE_t));
  ASSERT_NOT_NULL(line);

  push_stack(config.vm->stack, (VALUE_t){VALUE_float, {.f = 0.0}});
  (void)lc_net_connected(test_ctx(), NULL, itemstore_root(config.itemstore_ctx));
  VALUE_t ret = pop_stack(config.vm->stack);
  ASSERT_EQ_INT(VALUE_nil, ret.type);
  assert_invalid_args_detail_contains("net.connected");

  push_stack(config.vm->stack, (VALUE_t){VALUE_int, {.i = -1}});
  (void)lc_net_connected(test_ctx(), NULL, itemstore_root(config.itemstore_ctx));
  ret = pop_stack(config.vm->stack);
  ASSERT_EQ_INT(VALUE_nil, ret.type);
  assert_invalid_args_detail_contains("net.connected");

  push_stack(config.vm->stack, (VALUE_t){VALUE_int, {.i = 1}});
  (void)lc_net_connected(test_ctx(), NULL, itemstore_root(config.itemstore_ctx));
  ret = pop_stack(config.vm->stack);
  ASSERT_EQ_INT(VALUE_nil, ret.type);
  assert_invalid_args_detail_contains("net.connected");

  push_stack(config.vm->stack,
             (VALUE_t){VALUE_int, {.i = INT64_C(4294967296)}});
  (void)lc_net_connected(test_ctx(), NULL, itemstore_root(config.itemstore_ctx));
  ret = pop_stack(config.vm->stack);
  ASSERT_EQ_INT(VALUE_nil, ret.type);
  assert_invalid_args_detail_contains("net.connected");

  teardown_libcall_runtime();
}

void test_net_address_returns_owned_numeric_peer_address(void) {
  setup_libcall_runtime();

  config.maxconns = 7;
  line = calloc((size_t)config.maxconns, sizeof(LINE_t));
  ASSERT_NOT_NULL(line);

  line[0].status = LINE_idle;
  line[0].telnet = telnet_init(NULL, capture_telnet_event, 0, NULL);
  ASSERT_NOT_NULL(line[0].telnet);
  strcpy(line[0].address, "192.0.2.45");
  line[1].status = LINE_data;
  line[1].telnet = telnet_init(NULL, capture_telnet_event, 0, NULL);
  ASSERT_NOT_NULL(line[1].telnet);
  strcpy(line[1].address, "2001:db8::45");
  line[2].status = LINE_connecting;
  line[2].telnet = telnet_init(NULL, capture_telnet_event, 0, NULL);
  ASSERT_NOT_NULL(line[2].telnet);
  strcpy(line[2].address, "192.0.2.46");
  line[3].status = LINE_disconnecting;
  line[3].telnet = telnet_init(NULL, capture_telnet_event, 0, NULL);
  ASSERT_NOT_NULL(line[3].telnet);
  strcpy(line[3].address, "192.0.2.47");
  strcpy(line[4].address, "stale-address");
  line[5].status = LINE_idle;
  strcpy(line[5].address, "192.0.2.48");
  line[6].status = LINE_idle;
  line[6].telnet = telnet_init(NULL, capture_telnet_event, 0, NULL);
  ASSERT_NOT_NULL(line[6].telnet);

  set_error_item(itemstore_root(config.itemstore_ctx), ERR_NETWORK_ERROR, "prior error", NULL);
  ITEM_t *err = find_item(itemstore_root(config.itemstore_ctx), "error");
  ASSERT_NOT_NULL(err);

  push_stack(config.vm->stack, (VALUE_t){VALUE_int, {.i = 0}});
  (void)lc_net_address(test_ctx(), NULL, itemstore_root(config.itemstore_ctx));
  VALUE_t ret = pop_stack(config.vm->stack);
  ASSERT_EQ_INT(VALUE_str, ret.type);
  ASSERT_TRUE(strcmp(ret.s, "192.0.2.45") == 0);
  ASSERT_TRUE(ret.s != line[0].address);
  strcpy(line[0].address, "changed");
  ASSERT_TRUE(strcmp(ret.s, "192.0.2.45") == 0);
  value_free(&ret);

  push_stack(config.vm->stack, (VALUE_t){VALUE_int, {.i = 1}});
  (void)lc_net_address(test_ctx(), NULL, itemstore_root(config.itemstore_ctx));
  ret = pop_stack(config.vm->stack);
  ASSERT_EQ_INT(VALUE_str, ret.type);
  ASSERT_TRUE(strcmp(ret.s, "2001:db8::45") == 0);
  ASSERT_TRUE(ret.s != line[1].address);
  value_free(&ret);

  for (int64_t index = 2; index < 7; index++) {
    push_stack(config.vm->stack, (VALUE_t){VALUE_int, {.i = index}});
    (void)lc_net_address(test_ctx(), NULL, itemstore_root(config.itemstore_ctx));
    ret = pop_stack(config.vm->stack);
    ASSERT_EQ_INT(VALUE_nil, ret.type);
  }

  ASSERT_EQ_INT(ERR_NETWORK_ERROR, item_value(err)->i);
  teardown_libcall_runtime();
}

void test_net_address_invalid_line_returns_nil(void) {
  setup_libcall_runtime();

  config.maxconns = 1;
  line = calloc((size_t)config.maxconns, sizeof(LINE_t));
  ASSERT_NOT_NULL(line);

  push_stack(config.vm->stack, (VALUE_t){VALUE_float, {.f = 0.0}});
  (void)lc_net_address(test_ctx(), NULL, itemstore_root(config.itemstore_ctx));
  VALUE_t ret = pop_stack(config.vm->stack);
  ASSERT_EQ_INT(VALUE_nil, ret.type);
  assert_invalid_args_detail_contains("net.address");

  push_stack(config.vm->stack, (VALUE_t){VALUE_int, {.i = -1}});
  (void)lc_net_address(test_ctx(), NULL, itemstore_root(config.itemstore_ctx));
  ret = pop_stack(config.vm->stack);
  ASSERT_EQ_INT(VALUE_nil, ret.type);
  assert_invalid_args_detail_contains("net.address");

  push_stack(config.vm->stack, (VALUE_t){VALUE_int, {.i = 1}});
  (void)lc_net_address(test_ctx(), NULL, itemstore_root(config.itemstore_ctx));
  ret = pop_stack(config.vm->stack);
  ASSERT_EQ_INT(VALUE_nil, ret.type);
  assert_invalid_args_detail_contains("net.address");

  push_stack(config.vm->stack,
             (VALUE_t){VALUE_int, {.i = INT64_C(4294967296)}});
  (void)lc_net_address(test_ctx(), NULL, itemstore_root(config.itemstore_ctx));
  ret = pop_stack(config.vm->stack);
  ASSERT_EQ_INT(VALUE_nil, ret.type);
  assert_invalid_args_detail_contains("net.address");

  teardown_libcall_runtime();
}
