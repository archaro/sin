#include "test_framework.h"

/* This white-box TU owns CONFIG_t, the libuv/Telnet stubs, and direct source
 * inclusion.  Keep it out of the ordinary archive/object link so the
 * framework binary cannot acquire duplicate network globals. */
#define tests legacy_network_tests
#define main legacy_network_main
#include "../../tests/network/test_network.c"
#undef main
#undef tests
#include "item_internal.h"

/* The network white-box binary intentionally does not link the production
 * archive: test_framework.c only needs these reset-hook entry points, while
 * the included legacy TU already supplies its item lookup stubs. */
void alloc_test_fail_after(long allocation) { (void)allocation; }
void itemstore_set_load_constructor_failure_hook_for_tests(
    ITEMSTORE_LOAD_CONSTRUCTOR_FAILURE_HOOK_t hook) { (void)hook; }
void itemstore_set_item_creation_failure_hook_for_tests(
    ITEMSTORE_ITEM_CREATION_FAILURE_HOOK_t hook) { (void)hook; }
void itemstore_set_source_io_hooks_for_tests(ITEMSTORE_SOURCE_WRITE_HOOK_t write_hook,
                                             ITEMSTORE_SOURCE_CLOSE_HOOK_t close_hook) {
  (void)write_hook;
  (void)close_hook;
}
void itemstore_set_directory_sync_hook_for_tests(
    ITEMSTORE_DIRECTORY_SYNC_HOOK_t hook) { (void)hook; }
void itemstore_set_pre_publish_hook_for_tests(
    ITEMSTORE_PRE_PUBLISH_HOOK_t hook) { (void)hook; }
void itemstore_set_sync_hook_for_tests(ITEMSTORE_SYNC_HOOK_t hook) {
  (void)hook;
}

static const TF_TestDescriptor tests[] = {
    {"rewrite.network.test_append_input_lines_and_limits",
     test_append_input_lines_and_limits, "exclusive,network", 30000,
     "api.network.runtime-io,api.network.telnet,baseline.legacy.network.test_append_input_lines_and_limits"},
    {"rewrite.network.test_get_input_cases", test_get_input_cases,
     "exclusive,network", 30000,
     "api.network.runtime-io,baseline.legacy.network.test_get_input_cases"},
    {"rewrite.network.test_input_cursor_drain_and_compaction_counters",
     test_input_cursor_drain_and_compaction_counters, "exclusive,network", 30000,
     "api.network.runtime-io,baseline.legacy.network.test_input_cursor_drain_and_compaction_counters"},
    {"rewrite.network.test_input_buffer_boundary_preserves_64k_limit",
     test_input_buffer_boundary_preserves_64k_limit, "exclusive,network", 30000,
     "api.network.runtime-io,baseline.legacy.network.test_input_buffer_boundary_preserves_64k_limit"},
    {"rewrite.network.test_input_cursor_growth_failure_disconnects",
     test_input_cursor_growth_failure_disconnects, "exclusive,network", 30000,
     "api.network.runtime-io,baseline.legacy.network.test_input_cursor_growth_failure_disconnects"},
    {"rewrite.network.test_runtime_poll_skips_failed_input",
     test_runtime_poll_skips_failed_input, "exclusive,network", 30000,
     "api.network.runtime-io,baseline.legacy.network.test_runtime_poll_skips_failed_input"},
    {"rewrite.network.test_output_flush_limits_and_callback",
     test_output_flush_limits_and_callback, "exclusive,network", 30000,
     "api.network.runtime-io,baseline.legacy.network.test_output_flush_limits_and_callback"},
    {"rewrite.network.test_disconnect_waits_for_pending_output",
     test_disconnect_waits_for_pending_output, "exclusive,network", 30000,
     "api.network.runtime-io,baseline.legacy.network.test_disconnect_waits_for_pending_output"},
    {"rewrite.network.test_line_lifecycle_states_and_reuse",
     test_line_lifecycle_states_and_reuse, "exclusive,network", 30000,
     "api.network.runtime-io,baseline.legacy.network.test_line_lifecycle_states_and_reuse"},
    {"rewrite.network.test_remote_disconnect_marks_line_before_close_callback",
     test_remote_disconnect_marks_line_before_close_callback, "exclusive,network", 30000,
     "api.network.runtime-io,baseline.legacy.network.test_remote_disconnect_marks_line_before_close_callback"},
    {"rewrite.network.test_disconnect_close_write_callback_orders",
     test_disconnect_close_write_callback_orders, "exclusive,network", 30000,
     "api.network.runtime-io,baseline.legacy.network.test_disconnect_close_write_callback_orders"},
    {"rewrite.network.test_destroy_line_does_not_release_live_transport",
     test_destroy_line_does_not_release_live_transport, "exclusive,network", 30000,
     "api.network.runtime-io,baseline.legacy.network.test_destroy_line_does_not_release_live_transport"},
    {"rewrite.network.test_destroy_line_after_real_telnet_init_failure",
     test_destroy_line_after_real_telnet_init_failure, "exclusive,network", 30000,
     "api.network.telnet,baseline.legacy.network.test_destroy_line_after_real_telnet_init_failure"},
    {"rewrite.network.test_runtime_destroy_failure_preserves_pending_disconnect",
     test_runtime_destroy_failure_preserves_pending_disconnect, "exclusive,network", 30000,
     "api.network.runtime-io,baseline.legacy.network.test_runtime_destroy_failure_preserves_pending_disconnect"},
    {"rewrite.network.test_on_new_connection_rejections_and_close_ownership",
     test_on_new_connection_rejections_and_close_ownership, "exclusive,network", 30000,
     "api.network.telnet,baseline.legacy.network.test_on_new_connection_rejections_and_close_ownership"},
    {"rewrite.network.test_adversarial_long_stream_without_newline",
     test_adversarial_long_stream_without_newline, "exclusive,network", 30000,
     "api.network.runtime-io,baseline.legacy.network.test_adversarial_long_stream_without_newline"},
    {"rewrite.network.test_input_processor_releases_interpreter_results",
     test_input_processor_releases_interpreter_results, "exclusive,network", 30000,
     "api.network.runtime-io,baseline.legacy.network.test_input_processor_releases_interpreter_results"},
    {"rewrite.network.test_input_processor_missing_item_requests_unsafe_shutdown",
     test_input_processor_missing_item_requests_unsafe_shutdown, "exclusive,network", 30000,
     "api.network.runtime-io,baseline.legacy.network.test_input_processor_missing_item_requests_unsafe_shutdown"},
    {"rewrite.network.test_input_processor_timer_is_nonblocking_and_sleepable",
     test_input_processor_timer_is_nonblocking_and_sleepable, "exclusive,network", 30000,
     "api.network.runtime-io,baseline.legacy.network.test_input_processor_timer_is_nonblocking_and_sleepable"},
};

int main(int argc, char **argv) {
  return tf_main(argc, argv, tests, sizeof tests / sizeof tests[0]);
}
