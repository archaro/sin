#include "test_framework.h"

void test_task_one_shot_auto_retires(void);
void test_task_repeating_execution_and_explicit_kill(void);
void test_task_setup_failures_unwind(void);
void test_task_id_reuse_is_exactly_once(void);
void test_task_finalise_handles_active_and_closing(void);

static const TF_TestDescriptor tests[] = {
    {"rewrite.runtime.test_task_one_shot_auto_retires", test_task_one_shot_auto_retires, "exclusive", 30000, "test.runtime.test_task_one_shot_auto_retires"},
    {"rewrite.runtime.test_task_repeating_execution_and_explicit_kill", test_task_repeating_execution_and_explicit_kill, "exclusive", 30000, "api.libcall.task,api.runtime.task,libcall.task.killtask"},
    {"rewrite.runtime.test_task_setup_failures_unwind", test_task_setup_failures_unwind, "exclusive", 30000, "test.runtime.test_task_setup_failures_unwind"},
    {"rewrite.runtime.test_task_id_reuse_is_exactly_once", test_task_id_reuse_is_exactly_once, "exclusive", 30000, "api.runtime.task"},
    {"rewrite.runtime.test_task_finalise_handles_active_and_closing", test_task_finalise_handles_active_and_closing, "exclusive", 30000, "test.runtime.test_task_finalise_handles_active_and_closing"},
};

int main(int argc, char **argv) {
  return tf_main(argc, argv, tests, sizeof tests / sizeof tests[0]);
}
