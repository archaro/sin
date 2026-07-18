#include <stdint.h>

#include <uv.h>

#include "item.h"
#include "memory.h"
#include "runtime_context.h"
#include "task.h"
#include "test_assert.h"

void execute_task_cb(uv_timer_t *req);
uint8_t *lc_task_killtask(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item);

static unsigned callback_count;

static void count_timer_cb(uv_timer_t *handle) {
  (void)handle;
  callback_count++;
}

static void setup_task_loop(uv_loop_t *loop) {
  ASSERT_EQ_INT(0, uv_loop_init(loop));
  init_tasks();
}

static void teardown_task_loop(uv_loop_t *loop) {
  finalise_tasks(loop);
  ASSERT_EQ_INT(0, uv_loop_close(loop));
}

void test_task_one_shot_auto_retires(void) {
  uv_loop_t loop;
  ITEM_t *root;
  TASK_t *task;
  uint64_t id;

  setup_task_loop(&loop);
  root = make_root_item("root");
  ASSERT_NOT_NULL(root);
  task = make_task("missing", 0);
  ASSERT_NOT_NULL(task);
  task->itemroot = root;
  id = task->id;
  ASSERT_TRUE(start_task_timer(task, &loop, execute_task_cb, 0));

  (void)uv_run(&loop, UV_RUN_DEFAULT);
  ASSERT_TRUE(find_task_by_id(id) == NULL);
  ASSERT_EQ_INT(0, uv_loop_alive(&loop));
  destroy_item(root);
  teardown_task_loop(&loop);
}

void test_task_repeating_execution_and_explicit_kill(void) {
  uv_loop_t loop;
  TASK_t *task;
  uint64_t id;

  setup_task_loop(&loop);
  callback_count = 0;
  task = make_task("unused", 1);
  ASSERT_NOT_NULL(task);
  id = task->id;
  ASSERT_TRUE(start_task_timer(task, &loop, count_timer_cb, 0));
  (void)uv_run(&loop, UV_RUN_ONCE);
  ASSERT_TRUE(callback_count >= 1);
  ASSERT_TRUE(find_task_by_id(id) == task);
  ASSERT_EQ_INT(TASK_ACTIVE, task->state);
  RuntimeContext kill_context;
  runtime_context_init(&kill_context, task->vm);
  push_stack(task->vm->stack, (VALUE_t){VALUE_int, {.i = (int64_t)id}});
  (void)lc_task_killtask(&kill_context, NULL, NULL);
  VALUE_t result = pop_stack(task->vm->stack);
  ASSERT_EQ_INT(VALUE_bool, result.type);
  ASSERT_EQ_INT(1, result.i);
  push_stack(task->vm->stack, (VALUE_t){VALUE_int, {.i = (int64_t)id}});
  (void)lc_task_killtask(&kill_context, NULL, NULL);
  result = pop_stack(task->vm->stack);
  ASSERT_EQ_INT(VALUE_bool, result.type);
  ASSERT_EQ_INT(0, result.i);
  ASSERT_TRUE(find_task_by_id(id) == NULL);
  teardown_task_loop(&loop);
}

void test_task_setup_failures_unwind(void) {
  uv_loop_t loop;
  TASK_t *task;

  setup_task_loop(&loop);
  task = make_task("runtime-failure", 1);
  ASSERT_NOT_NULL(task);
  runtime_context_init(&task->runtime_context, task->vm);
  alloc_test_fail_after(0);
  ASSERT_TRUE(!runtime_init(&task->runtime_context, task->vm));
  alloc_test_fail_after(-1);
  destroy_task(task);

  task = make_task("timer-init-failure", 1);
  ASSERT_NOT_NULL(task);
  ASSERT_TRUE(!start_task_timer(task, NULL, count_timer_cb, 0));
  destroy_task(task);

  task = make_task("timer-start-failure", 1);
  ASSERT_NOT_NULL(task);
  ASSERT_TRUE(!start_task_timer(task, &loop, NULL, 0));
  ASSERT_TRUE(find_task_by_id(task->id) == NULL);
  teardown_task_loop(&loop);
}

void test_task_id_reuse_is_exactly_once(void) {
  uv_loop_t loop;
  TASK_t *task;
  TASK_t *replacement;
  TASK_t *second_replacement;
  uint64_t id;

  setup_task_loop(&loop);
  task = make_task("active", 1000);
  ASSERT_NOT_NULL(task);
  id = task->id;
  ASSERT_TRUE(start_task_timer(task, &loop, count_timer_cb, 1000));
  ASSERT_TRUE(request_task_close(task));
  ASSERT_TRUE(!request_task_close(task));
  (void)uv_run(&loop, UV_RUN_NOWAIT);

  replacement = make_task("replacement", 0);
  ASSERT_NOT_NULL(replacement);
  ASSERT_EQ_INT(id, replacement->id);
  second_replacement = make_task("second", 0);
  ASSERT_NOT_NULL(second_replacement);
  ASSERT_TRUE(second_replacement->id != id);
  ASSERT_TRUE(second_replacement->id != replacement->id);
  destroy_task(replacement);
  destroy_task(second_replacement);
  teardown_task_loop(&loop);
}

void test_task_finalise_handles_active_and_closing(void) {
  uv_loop_t loop;
  TASK_t *closing;
  TASK_t *active;
  uint64_t closing_id;
  uint64_t active_id;

  setup_task_loop(&loop);
  closing = make_task("closing", 1000);
  active = make_task("active", 1000);
  ASSERT_NOT_NULL(closing);
  ASSERT_NOT_NULL(active);
  closing_id = closing->id;
  active_id = active->id;
  ASSERT_TRUE(start_task_timer(closing, &loop, count_timer_cb, 1000));
  ASSERT_TRUE(start_task_timer(active, &loop, count_timer_cb, 1000));
  ASSERT_TRUE(request_task_close(closing));
  ASSERT_TRUE(find_task_by_id(closing_id) == NULL);
  ASSERT_TRUE(find_task_by_id(active_id) == active);

  finalise_tasks(&loop);
  ASSERT_TRUE(find_task_by_id(active_id) == NULL);
  ASSERT_EQ_INT(0, uv_loop_alive(&loop));
  finalise_tasks(&loop);
  ASSERT_EQ_INT(0, uv_loop_close(&loop));
}
