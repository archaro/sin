#include "test_framework.h"

void test_newgametask_rejects_invalid_intervals_before_timer_start(void);
void test_newgametask_rejects_missing_event_loop_before_returning_task_id(void);
void test_task_introspection_thisid_ordinary_context_returns_nil(void);
void test_task_introspection_exists_valid_and_invalid_ids(void);
void test_task_exists_rejects_non_integer(void);
void test_task_introspection_count_and_exists_with_lifecycle(void);
void test_task_thisid_in_callback_survives_self_close(void);
void test_task_callback_frees_aggregate_return_values(void);
void test_newgametask_child_callback_uses_own_identity(void);

static const TF_TestDescriptor tests[] = {
    {"rewrite.runtime.test_newgametask_rejects_invalid_intervals_before_timer_start", test_newgametask_rejects_invalid_intervals_before_timer_start, "exclusive", 30000, "api.libcall.task,api.runtime.task,libcall.task.newgametask"},
    {"rewrite.runtime.test_newgametask_rejects_missing_event_loop_before_returning_task_id", test_newgametask_rejects_missing_event_loop_before_returning_task_id, "exclusive", 30000, "test.runtime.test_newgametask_rejects_missing_event_loop_before_returning_task_id"},
    {"rewrite.runtime.test_task_introspection_thisid_ordinary_context_returns_nil", test_task_introspection_thisid_ordinary_context_returns_nil, "exclusive", 30000, "libcall.task.thisid"},
    {"rewrite.runtime.test_task_introspection_exists_valid_and_invalid_ids", test_task_introspection_exists_valid_and_invalid_ids, "exclusive", 30000, "api.libcall.task,libcall.task.exists"},
    {"rewrite.runtime.test_task_exists_rejects_non_integer", test_task_exists_rejects_non_integer, "exclusive", 30000, "test.runtime.test_task_exists_rejects_non_integer"},
    {"rewrite.runtime.test_task_introspection_count_and_exists_with_lifecycle", test_task_introspection_count_and_exists_with_lifecycle, "exclusive", 30000, "libcall.task.count"},
    {"rewrite.runtime.test_task_thisid_in_callback_survives_self_close", test_task_thisid_in_callback_survives_self_close, "exclusive", 30000, "test.runtime.test_task_thisid_in_callback_survives_self_close"},
    {"rewrite.runtime.test_task_callback_frees_aggregate_return_values", test_task_callback_frees_aggregate_return_values, "exclusive", 30000, "test.runtime.test_task_callback_frees_aggregate_return_values"},
    {"rewrite.runtime.test_newgametask_child_callback_uses_own_identity", test_newgametask_child_callback_uses_own_identity, "exclusive", 30000, "libcall.task.newgametask"},
};

int main(int argc, char **argv) {
  return tf_main(argc, argv, tests, sizeof tests / sizeof tests[0]);
}
