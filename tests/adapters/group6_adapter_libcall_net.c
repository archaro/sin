#include "test_framework.h"

void test_net_maxlines_returns_configured_slot_bound(void);
void test_net_connected_reports_writable_telnet_states(void);
void test_net_connected_invalid_line_returns_nil(void);
void test_net_address_returns_owned_numeric_peer_address(void);
void test_net_address_invalid_line_returns_nil(void);
void test_net_write_ignores_non_writable_lines(void);
void test_net_input_fair_queue_progresses_connect_data_disconnect(void);
void test_net_ditch_then_input_waits_for_close_callback(void);
void test_network_shutdown_drains_client_close_callbacks(void);
void test_net_ditch_disconnects_active_lines(void);
void test_net_flush_reports_line_status(void);
void test_net_ditch_reports_inactive_lines(void);
void test_net_ditch_invalid_line_returns_nil(void);
void test_net_echo_negotiates_current_line_and_consumes_values(void);
void test_net_echo_ignores_unavailable_current_line(void);
void test_net_connecting_line_operation_contracts(void);

static const TF_TestDescriptor tests[] = {
    {"test.runtime.test_net_maxlines_returns_configured_slot_bound", test_net_maxlines_returns_configured_slot_bound, "exclusive", 30000, "libcall.net.maxlines"},
    {"test.runtime.test_net_connected_reports_writable_telnet_states", test_net_connected_reports_writable_telnet_states, "exclusive", 30000, "libcall.net.connected"},
    {"test.runtime.test_net_connected_invalid_line_returns_nil", test_net_connected_invalid_line_returns_nil, "exclusive", 30000, "test.runtime.test_net_connected_invalid_line_returns_nil"},
    {"test.runtime.test_net_address_returns_owned_numeric_peer_address", test_net_address_returns_owned_numeric_peer_address, "exclusive", 30000, "libcall.net.address"},
    {"test.runtime.test_net_address_invalid_line_returns_nil", test_net_address_invalid_line_returns_nil, "exclusive", 30000, "test.runtime.test_net_address_invalid_line_returns_nil"},
    {"test.runtime.test_net_write_ignores_non_writable_lines", test_net_write_ignores_non_writable_lines, "exclusive", 30000, "api.libcall.net,libcall.net.write"},
    {"test.runtime.test_net_input_fair_queue_progresses_connect_data_disconnect", test_net_input_fair_queue_progresses_connect_data_disconnect, "exclusive", 30000, "api.libcall.net,libcall.net.input"},
    {"test.runtime.test_net_ditch_then_input_waits_for_close_callback", test_net_ditch_then_input_waits_for_close_callback, "exclusive", 30000, "test.runtime.test_net_ditch_then_input_waits_for_close_callback"},
    {"test.runtime.test_network_shutdown_drains_client_close_callbacks", test_network_shutdown_drains_client_close_callbacks, "exclusive", 30000, "test.runtime.test_network_shutdown_drains_client_close_callbacks"},
    {"test.runtime.test_net_ditch_disconnects_active_lines", test_net_ditch_disconnects_active_lines, "exclusive", 30000, "libcall.net.ditch"},
    {"test.runtime.test_net_flush_reports_line_status", test_net_flush_reports_line_status, "exclusive", 30000, "libcall.net.flush"},
    {"test.runtime.test_net_ditch_reports_inactive_lines", test_net_ditch_reports_inactive_lines, "exclusive", 30000, "test.runtime.test_net_ditch_reports_inactive_lines"},
    {"test.runtime.test_net_ditch_invalid_line_returns_nil", test_net_ditch_invalid_line_returns_nil, "exclusive", 30000, "api.libcall.net,libcall.net.ditch"},
    {"test.runtime.test_net_echo_negotiates_current_line_and_consumes_values", test_net_echo_negotiates_current_line_and_consumes_values, "exclusive", 30000, "libcall.net.echo"},
    {"test.runtime.test_net_echo_ignores_unavailable_current_line", test_net_echo_ignores_unavailable_current_line, "exclusive", 30000, "test.runtime.test_net_echo_ignores_unavailable_current_line"},
    {"test.runtime.test_net_connecting_line_operation_contracts", test_net_connecting_line_operation_contracts, "exclusive", 30000, "test.runtime.test_net_connecting_line_operation_contracts"},
};

int main(int argc, char **argv) {
  return tf_main(argc, argv, tests, sizeof tests / sizeof tests[0]);
}
