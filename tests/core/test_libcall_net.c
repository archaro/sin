#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "config.h"
#include "error.h"
#include "interpret.h"
#include "item.h"
#include "libcall.h"
#include "network.h"
#include "stack.h"
#include "test_assert.h"
#include "shared/test_libcall_support.h"
#include "value.h"

uint8_t *lc_net_write(RuntimeContext *, uint8_t *, ITEM_t *);
uint8_t *lc_net_input(RuntimeContext *, uint8_t *, ITEM_t *);
uint8_t *lc_net_flush(RuntimeContext *, uint8_t *, ITEM_t *);
uint8_t *lc_net_ditch(RuntimeContext *, uint8_t *, ITEM_t *);
uint8_t *lc_net_echo(RuntimeContext *, uint8_t *, ITEM_t *);
uint8_t *lc_net_maxlines(RuntimeContext *, uint8_t *, ITEM_t *);
uint8_t *lc_net_connected(RuntimeContext *, uint8_t *, ITEM_t *);
uint8_t *lc_net_address(RuntimeContext *, uint8_t *, ITEM_t *);

extern CONFIG_t config;

#define TEST_TELNET_IAC 255u
#define TEST_TELNET_WILL 251u
#define TEST_TELNET_WONT 252u
#define TEST_TELNET_DO 253u
#define TEST_TELNET_ECHO 1u

static ITEM_t *root_item(void) {
  return itemstore_root(config.itemstore_ctx);
}

static VALUE_t call_net_with_line(uint8_t *(*call)(RuntimeContext *, uint8_t *,
                                                   ITEM_t *), int64_t line) {
  push_stack(config.vm->stack, (VALUE_t){VALUE_int, {.i = line}});
  (void)call(test_ctx(), NULL, root_item());
  return pop_stack(config.vm->stack);
}

static VALUE_t call_net_write(int64_t line, const char *text) {
  push_stack(config.vm->stack, (VALUE_t){VALUE_int, {.i = line}});
  push_stack(config.vm->stack,
             (VALUE_t){VALUE_str, {.s = strdup(text ? text : "")}});
  (void)lc_net_write(test_ctx(), NULL, root_item());
  return pop_stack(config.vm->stack);
}

static VALUE_t call_net_echo(VALUE_t value) {
  push_stack(config.vm->stack, value);
  (void)lc_net_echo(test_ctx(), NULL, root_item());
  return pop_stack(config.vm->stack);
}

static void assert_event(int expected, size_t expected_line) {
  VALUE_t event = pop_stack(config.vm->stack);
  ASSERT_EQ_INT(VALUE_int, event.type);
  ASSERT_EQ_INT(expected, event.i);
  ITEM_t *line_item = find_item(root_item(), "input.line");
  ASSERT_NOT_NULL(line_item);
  ASSERT_EQ_INT(expected_line, item_value(line_item)->i);
}

void test_net_maxlines_returns_configured_slot_bound(void) {
  setup_libcall_runtime();
  set_error_item(root_item(), ERR_NETWORK_ERROR, "prior error", NULL);
  test_network_clear();
  (void)lc_net_maxlines(test_ctx(), NULL, root_item());
  VALUE_t ret = pop_stack(config.vm->stack);
  ASSERT_EQ_INT(VALUE_int, ret.type);
  ASSERT_EQ_INT(0, ret.i);
  ITEM_t *error = find_item(root_item(), "error");
  ASSERT_NOT_NULL(error);
  ASSERT_EQ_INT(ERR_NETWORK_ERROR, item_value(error)->i);
  ASSERT_TRUE(test_network_reset(37));
  (void)lc_net_maxlines(test_ctx(), NULL, root_item());
  ret = pop_stack(config.vm->stack);
  ASSERT_EQ_INT(VALUE_int, ret.type);
  ASSERT_EQ_INT(37, ret.i);
  ASSERT_EQ_INT(ERR_NETWORK_ERROR, item_value(error)->i);
  teardown_libcall_runtime();
}

void test_net_write_ignores_non_writable_lines(void) {
  setup_libcall_runtime();
  ASSERT_TRUE(test_network_reset(3));
  ASSERT_TRUE(network_runtime_test_set_line(test_network_runtime(), 0,
                                            NETWORK_TEST_EMPTY));
  ASSERT_TRUE(network_runtime_test_set_line(test_network_runtime(), 1,
                                            NETWORK_TEST_CONNECTING));
  ASSERT_TRUE(network_runtime_test_set_line(test_network_runtime(), 2,
                                            NETWORK_TEST_DISCONNECTING));
  for (int64_t i = 0; i < 3; i++) {
    VALUE_t ret = call_net_write(i, "hello");
    ASSERT_EQ_INT(VALUE_nil, ret.type);
  }
  ASSERT_TRUE(network_runtime_test_set_line(test_network_runtime(), 0,
                                            NETWORK_TEST_IDLE));
  char *large = malloc(131073);
  ASSERT_NOT_NULL(large);
  memset(large, 'x', 131072);
  large[131072] = '\0';
  ASSERT_EQ_INT(NETWORK_WRITE_REJECTED,
                network_runtime_write(test_network_runtime(), 0, large,
                                      131072));
  free(large);
  teardown_libcall_runtime();
}

void test_net_input_fair_queue_progresses_connect_data_disconnect(void) {
  setup_libcall_runtime();
  ASSERT_TRUE(test_network_reset(3));
  NetworkRuntime *runtime = test_network_runtime();
  ASSERT_TRUE(network_runtime_test_set_line(runtime, 0, NETWORK_TEST_CONNECTING));
  ASSERT_TRUE(network_runtime_test_set_line(runtime, 1, NETWORK_TEST_DATA));
  ASSERT_TRUE(network_runtime_test_set_line(runtime, 2,
                                            NETWORK_TEST_DISCONNECTING));
  ASSERT_TRUE(network_runtime_test_set_input(runtime, 1, "hello\n"));
  (void)lc_net_input(test_ctx(), NULL, root_item());
  assert_event(1, 0);
  (void)lc_net_input(test_ctx(), NULL, root_item());
  assert_event(3, 1);
  ITEM_t *text_item = find_item(root_item(), "input.text");
  ASSERT_NOT_NULL(text_item);
  ASSERT_TRUE(strcmp(item_value(text_item)->s, "hello") == 0);
  test_network_drain();
  (void)lc_net_input(test_ctx(), NULL, root_item());
  assert_event(2, 2);
  teardown_libcall_runtime();
}

void test_net_ditch_then_input_waits_for_close_callback(void) {
  setup_libcall_runtime();
  ASSERT_TRUE(test_network_reset(1));
  NetworkRuntime *runtime = test_network_runtime();
  ASSERT_TRUE(network_runtime_test_set_line(runtime, 0, NETWORK_TEST_IDLE));
  VALUE_t ret = call_net_with_line(lc_net_ditch, 0);
  ASSERT_EQ_INT(VALUE_bool, ret.type);
  ASSERT_EQ_INT(1, ret.i);
  (void)lc_net_input(test_ctx(), NULL, root_item());
  ret = pop_stack(config.vm->stack);
  ASSERT_EQ_INT(VALUE_int, ret.type);
  ASSERT_EQ_INT(0, ret.i);
  test_network_drain();
  (void)lc_net_input(test_ctx(), NULL, root_item());
  ASSERT_EQ_INT(2, pop_stack(config.vm->stack).i);
  (void)lc_net_input(test_ctx(), NULL, root_item());
  ASSERT_EQ_INT(0, pop_stack(config.vm->stack).i);
  ASSERT_TRUE(network_runtime_test_set_line(runtime, 0,
                                            NETWORK_TEST_CONNECTING));
  (void)lc_net_input(test_ctx(), NULL, root_item());
  assert_event(1, 0);
  teardown_libcall_runtime();
}

void test_network_shutdown_drains_client_close_callbacks(void) {
  setup_libcall_runtime();
  ASSERT_TRUE(test_network_reset(1));
  ASSERT_TRUE(network_runtime_test_set_line(test_network_runtime(), 0,
                                            NETWORK_TEST_IDLE));
  network_runtime_shutdown(test_network_runtime());
  test_network_drain();
  teardown_libcall_runtime();
}

void test_net_ditch_disconnects_active_lines(void) {
  setup_libcall_runtime();
  ASSERT_TRUE(test_network_reset(2));
  ASSERT_TRUE(network_runtime_test_set_line(test_network_runtime(), 1,
                                            NETWORK_TEST_IDLE));
  VALUE_t ret = call_net_with_line(lc_net_ditch, 1);
  ASSERT_EQ_INT(VALUE_bool, ret.type);
  ASSERT_EQ_INT(1, ret.i);
  ret = call_net_with_line(lc_net_ditch, 1);
  ASSERT_EQ_INT(VALUE_bool, ret.type);
  ASSERT_EQ_INT(0, ret.i);
  ret = call_net_write(1, "after ditch");
  ASSERT_EQ_INT(VALUE_nil, ret.type);
  teardown_libcall_runtime();
}

void test_net_flush_reports_line_status(void) {
  setup_libcall_runtime();
  ASSERT_TRUE(test_network_reset(2));
  ASSERT_TRUE(network_runtime_test_set_line(test_network_runtime(), 0,
                                            NETWORK_TEST_IDLE));
  VALUE_t ret = call_net_with_line(lc_net_flush, 0);
  ASSERT_EQ_INT(VALUE_bool, ret.type);
  ASSERT_EQ_INT(1, ret.i);
  ret = call_net_with_line(lc_net_flush, 1);
  ASSERT_EQ_INT(VALUE_bool, ret.type);
  ASSERT_EQ_INT(0, ret.i);
  ASSERT_EQ_INT(ERR_NETWORK_ERROR, item_value(find_item(
                                  root_item(), "error"))->i);
  push_stack(config.vm->stack, (VALUE_t){VALUE_int, {.i = 2}});
  (void)lc_net_flush(test_ctx(), NULL, root_item());
  ret = pop_stack(config.vm->stack);
  ASSERT_EQ_INT(VALUE_bool, ret.type);
  ASSERT_EQ_INT(0, ret.i);
  push_stack(config.vm->stack, (VALUE_t){VALUE_float, {.f = 0.0}});
  (void)lc_net_flush(test_ctx(), NULL, root_item());
  ASSERT_EQ_INT(VALUE_nil, pop_stack(config.vm->stack).type);
  assert_invalid_args_detail_contains("net.flush");
  push_stack(config.vm->stack, (VALUE_t){VALUE_int, {.i = -1}});
  (void)lc_net_flush(test_ctx(), NULL, root_item());
  ASSERT_EQ_INT(VALUE_nil, pop_stack(config.vm->stack).type);
  assert_invalid_args_detail_contains("net.flush");
  teardown_libcall_runtime();
}

void test_net_ditch_reports_inactive_lines(void) {
  setup_libcall_runtime();
  ASSERT_TRUE(test_network_reset(2));
  ASSERT_TRUE(network_runtime_test_set_line(test_network_runtime(), 0,
                                            NETWORK_TEST_EMPTY));
  ASSERT_TRUE(network_runtime_test_set_line(test_network_runtime(), 1,
                                            NETWORK_TEST_DISCONNECTING));
  for (int64_t i = 0; i < 2; i++) {
    VALUE_t ret = call_net_with_line(lc_net_ditch, i);
    ASSERT_EQ_INT(VALUE_bool, ret.type);
    ASSERT_EQ_INT(0, ret.i);
    ASSERT_EQ_INT(ERR_NETWORK_ERROR,
                  item_value(find_item(root_item(), "error"))->i);
  }
  teardown_libcall_runtime();
}

void test_net_ditch_invalid_line_returns_nil(void) {
  setup_libcall_runtime();
  ASSERT_TRUE(test_network_reset(1));
  VALUE_t ret = call_net_with_line(lc_net_ditch, -1);
  ASSERT_EQ_INT(VALUE_nil, ret.type);
  assert_invalid_args_detail_contains("net.ditch");
  push_stack(config.vm->stack, (VALUE_t){VALUE_int, {.i = 1}});
  (void)lc_net_ditch(test_ctx(), NULL, root_item());
  ASSERT_EQ_INT(VALUE_bool, pop_stack(config.vm->stack).type);
  ASSERT_EQ_INT(ERR_NETWORK_ERROR,
                item_value(find_item(root_item(), "error"))->i);
  push_stack(config.vm->stack, (VALUE_t){VALUE_float, {.f = 0.0}});
  (void)lc_net_ditch(test_ctx(), NULL, root_item());
  ASSERT_EQ_INT(VALUE_nil, pop_stack(config.vm->stack).type);
  assert_invalid_args_detail_contains("net.ditch");
  teardown_libcall_runtime();
}

void test_net_echo_negotiates_current_line_and_consumes_values(void) {
  static const unsigned char will_echo[] = {TEST_TELNET_IAC, TEST_TELNET_WILL,
                                            TEST_TELNET_ECHO};
  static const unsigned char wont_echo[] = {TEST_TELNET_IAC, TEST_TELNET_WONT,
                                            TEST_TELNET_ECHO};
  setup_libcall_runtime();
  ASSERT_TRUE(test_network_reset(2));
  NetworkRuntime *runtime = test_network_runtime();
  ASSERT_TRUE(network_runtime_test_set_line(runtime, 1,
                                            NETWORK_TEST_CONNECTING));
  (void)lc_net_input(test_ctx(), NULL, root_item());
  ASSERT_EQ_INT(1, pop_stack(config.vm->stack).i);
  ASSERT_EQ_INT(VALUE_nil, call_net_echo((VALUE_t){VALUE_int, {.i = 0}}).type);
  unsigned char output[8];
  ASSERT_EQ_INT(sizeof(will_echo), network_runtime_test_take_output(
                                      runtime, 1, output, sizeof(output)));
  ASSERT_TRUE(memcmp(output, will_echo, sizeof(will_echo)) == 0);
  static const unsigned char peer_do_echo[] = {TEST_TELNET_IAC, TEST_TELNET_DO,
                                               TEST_TELNET_ECHO};
  ASSERT_TRUE(network_runtime_test_feed(runtime, 1,
                                        (const char *)peer_do_echo,
                                        sizeof(peer_do_echo)));
  ASSERT_EQ_INT(VALUE_nil,
                call_net_echo((VALUE_t){VALUE_str, {.s = strdup("enabled")}})
                    .type);
  ASSERT_EQ_INT(sizeof(wont_echo), network_runtime_test_take_output(
                                      runtime, 1, output, sizeof(output)));
  ASSERT_TRUE(memcmp(output, wont_echo, sizeof(wont_echo)) == 0);
  teardown_libcall_runtime();
}

void test_net_echo_ignores_unavailable_current_line(void) {
  setup_libcall_runtime();
  ASSERT_TRUE(test_network_reset(1));
  ASSERT_EQ_INT(VALUE_nil, call_net_echo((VALUE_t){VALUE_bool, {.i = 0}}).type);
  teardown_libcall_runtime();
}

void test_net_connecting_line_operation_contracts(void) {
  setup_libcall_runtime();
  ASSERT_TRUE(test_network_reset(3));
  NetworkRuntime *runtime = test_network_runtime();
  ASSERT_TRUE(network_runtime_test_set_line(runtime, 0,
                                            NETWORK_TEST_CONNECTING));
  ASSERT_TRUE(network_runtime_test_set_line(runtime, 1,
                                            NETWORK_TEST_CONNECTING));
  ASSERT_TRUE(network_runtime_test_set_line(runtime, 2,
                                            NETWORK_TEST_CONNECTING));

  VALUE_t ret = call_net_with_line(lc_net_flush, 0);
  ASSERT_EQ_INT(VALUE_bool, ret.type);
  ASSERT_EQ_INT(1, ret.i);

  ASSERT_TRUE(network_runtime_test_select_line(runtime, 1));
  ret = call_net_echo((VALUE_t){VALUE_bool, {.i = 0}});
  ASSERT_EQ_INT(VALUE_nil, ret.type);
  unsigned char output[8];
  ASSERT_EQ_INT(0, network_runtime_test_take_output(runtime, 1, output,
                                                    sizeof(output)));

  ret = call_net_with_line(lc_net_ditch, 2);
  ASSERT_EQ_INT(VALUE_bool, ret.type);
  ASSERT_EQ_INT(1, ret.i);
  teardown_libcall_runtime();
}

void test_net_connected_reports_writable_telnet_states(void) {
  setup_libcall_runtime();
  ASSERT_TRUE(test_network_reset(6));
  NetworkRuntime *runtime = test_network_runtime();
  ASSERT_TRUE(network_runtime_test_set_line(runtime, 0, NETWORK_TEST_CONNECTING));
  ASSERT_TRUE(network_runtime_test_set_line(runtime, 1, NETWORK_TEST_IDLE));
  ASSERT_TRUE(network_runtime_test_set_line(runtime, 2, NETWORK_TEST_DATA));
  ASSERT_TRUE(network_runtime_test_set_line(runtime, 3,
                                            NETWORK_TEST_DISCONNECTING));
  ASSERT_TRUE(network_runtime_test_set_line(runtime, 4, NETWORK_TEST_EMPTY));
  ASSERT_TRUE(network_runtime_test_set_line(runtime, 5,
                                            NETWORK_TEST_IDLE_NO_TRANSPORT));
  ASSERT_EQ_INT(0, call_net_with_line(lc_net_connected, 0).i);
  ASSERT_EQ_INT(1, call_net_with_line(lc_net_connected, 1).i);
  ASSERT_EQ_INT(1, call_net_with_line(lc_net_connected, 2).i);
  ASSERT_EQ_INT(0, call_net_with_line(lc_net_connected, 3).i);
  ASSERT_EQ_INT(0, call_net_with_line(lc_net_connected, 4).i);
  ASSERT_EQ_INT(0, call_net_with_line(lc_net_connected, 5).i);
  teardown_libcall_runtime();
}

void test_net_connected_invalid_line_returns_nil(void) {
  setup_libcall_runtime();
  ASSERT_TRUE(test_network_reset(1));
  ASSERT_EQ_INT(VALUE_nil, call_net_with_line(lc_net_connected, -1).type);
  ASSERT_EQ_INT(VALUE_nil, call_net_with_line(lc_net_connected, 1).type);
  push_stack(config.vm->stack, (VALUE_t){VALUE_float, {.f = 0.0}});
  (void)lc_net_connected(test_ctx(), NULL, root_item());
  ASSERT_EQ_INT(VALUE_nil, pop_stack(config.vm->stack).type);
  assert_invalid_args_detail_contains("net.connected");
  ASSERT_EQ_INT(VALUE_nil,
                call_net_with_line(lc_net_connected, INT64_C(4294967296)).type);
  teardown_libcall_runtime();
}

void test_net_address_returns_owned_numeric_peer_address(void) {
  setup_libcall_runtime();
  ASSERT_TRUE(test_network_reset(7));
  NetworkRuntime *runtime = test_network_runtime();
  ASSERT_TRUE(network_runtime_test_set_line(runtime, 0, NETWORK_TEST_IDLE));
  ASSERT_TRUE(network_runtime_test_set_line(runtime, 1, NETWORK_TEST_DATA));
  ASSERT_TRUE(network_runtime_test_set_line(runtime, 2, NETWORK_TEST_CONNECTING));
  ASSERT_TRUE(network_runtime_test_set_line(runtime, 3,
                                            NETWORK_TEST_DISCONNECTING));
  ASSERT_TRUE(network_runtime_test_set_line(runtime, 5,
                                            NETWORK_TEST_IDLE_NO_TRANSPORT));
  ASSERT_TRUE(network_runtime_test_set_line(runtime, 6, NETWORK_TEST_IDLE));
  ASSERT_TRUE(network_runtime_test_set_address(runtime, 0, "192.0.2.45"));
  ASSERT_TRUE(network_runtime_test_set_address(runtime, 1, "2001:db8::45"));
  ASSERT_TRUE(network_runtime_test_set_address(runtime, 2, "192.0.2.46"));
  ASSERT_TRUE(network_runtime_test_set_address(runtime, 3, "192.0.2.47"));
  ASSERT_TRUE(network_runtime_test_set_address(runtime, 4, "stale-address"));
  ASSERT_TRUE(network_runtime_test_set_address(runtime, 5, "192.0.2.48"));
  VALUE_t ret = call_net_with_line(lc_net_address, 0);
  ASSERT_EQ_INT(VALUE_str, ret.type);
  ASSERT_TRUE(strcmp(ret.s, "192.0.2.45") == 0);
  ASSERT_TRUE(network_runtime_test_set_address(runtime, 0, "changed"));
  ASSERT_TRUE(strcmp(ret.s, "192.0.2.45") == 0);
  value_free(&ret);
  ret = call_net_with_line(lc_net_address, 1);
  ASSERT_EQ_INT(VALUE_str, ret.type);
  ASSERT_TRUE(strcmp(ret.s, "2001:db8::45") == 0);
  value_free(&ret);
  for (int64_t i = 2; i < 7; i++) {
    ASSERT_EQ_INT(VALUE_nil, call_net_with_line(lc_net_address, i).type);
  }
  teardown_libcall_runtime();
}

void test_net_address_invalid_line_returns_nil(void) {
  setup_libcall_runtime();
  ASSERT_TRUE(test_network_reset(1));
  ASSERT_EQ_INT(VALUE_nil, call_net_with_line(lc_net_address, -1).type);
  ASSERT_EQ_INT(VALUE_nil, call_net_with_line(lc_net_address, 1).type);
  push_stack(config.vm->stack, (VALUE_t){VALUE_float, {.f = 0.0}});
  (void)lc_net_address(test_ctx(), NULL, root_item());
  ASSERT_EQ_INT(VALUE_nil, pop_stack(config.vm->stack).type);
  assert_invalid_args_detail_contains("net.address");
  ASSERT_EQ_INT(VALUE_nil,
                call_net_with_line(lc_net_address, INT64_C(4294967296)).type);
  teardown_libcall_runtime();
}
