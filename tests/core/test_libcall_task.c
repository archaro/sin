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
#include "itemref.h"
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

static ITEM_t *insert_compiled_code(ITEM_t *root, const char *name,
                                    const char *source) {
  OUTPUT_t *out = NULL;
  char *errdetail = NULL;
  int8_t rc = compile_source_to_bytecode(source, strlen(source), &out,
                                         &errdetail);
  ASSERT_EQ_INT(ERR_NOERROR, rc);
  ASSERT_TRUE(errdetail == NULL);
  ASSERT_NOT_NULL(out);
  size_t length = (size_t)(out->nextbyte - out->bytecode);
  ASSERT_TRUE(length <= UINT32_MAX);
  uint8_t *bytecode = out->bytecode;
  out->bytecode = NULL;
  ITEM_t *item = test_item_set_code(root, name, (uint32_t)length, bytecode);
  ASSERT_NOT_NULL(item);
  free(out);
  return item;
}

static uint64_t schedule_task(const char *name, int64_t start, int64_t repeat) {
  push_stack(config.vm->stack,
             (VALUE_t){VALUE_str, {.s = strdup(name)}});
  push_stack(config.vm->stack, (VALUE_t){VALUE_int, {.i = start}});
  push_stack(config.vm->stack, (VALUE_t){VALUE_int, {.i = repeat}});
  (void)lc_task_newgametask(test_ctx(), NULL,
                            itemstore_root(config.itemstore_ctx));
  VALUE_t result = pop_stack(config.vm->stack);
  ASSERT_EQ_INT(VALUE_int, result.type);
  ASSERT_TRUE(result.i > 0);
  return (uint64_t)result.i;
}

void test_newgametask_copies_canonical_target_and_defers_zero_delay(void) {
  uv_loop_t loop;
  ASSERT_EQ_INT(0, uv_loop_init(&loop));
  setup_libcall_runtime();
  config.loop = &loop;
  init_tasks();
  insert_compiled_code(itemstore_root(config.itemstore_ctx),
                       "owned.target", "observed.owned = 1;");

  uint64_t id = schedule_task("OwNeD.TaRgEt", 0, 0);
  TASK_t *task = find_task_by_id(id);
  ASSERT_NOT_NULL(task);
  ASSERT_TRUE(strcmp(task->itemname, "owned.target") == 0);
  ASSERT_TRUE(find_item(itemstore_root(config.itemstore_ctx),
                        "observed.owned") == NULL);

  (void)uv_run(&loop, UV_RUN_DEFAULT);
  ITEM_t *observed = find_item(itemstore_root(config.itemstore_ctx),
                               "observed.owned");
  ASSERT_NOT_NULL(observed);
  ASSERT_EQ_INT(VALUE_int, item_value(observed)->type);
  ASSERT_EQ_INT(1, item_value(observed)->i);
  ASSERT_TRUE(find_task_by_id(id) == NULL);

  finalise_tasks(&loop);
  ASSERT_EQ_INT(0, uv_loop_close(&loop));
  teardown_libcall_runtime();
}

void test_newgametask_deleted_target_retires_one_shot(void) {
  uv_loop_t loop;
  ASSERT_EQ_INT(0, uv_loop_init(&loop));
  setup_libcall_runtime();
  config.loop = &loop;
  init_tasks();
  insert_compiled_code(itemstore_root(config.itemstore_ctx),
                       "deleted.before.fire", "observed.deleted = 1;");

  uint64_t id = schedule_task("deleted.before.fire", 0, 0);
  ASSERT_NOT_NULL(find_task_by_id(id));
  ASSERT_EQ_INT(ITEM_MUTATION_DELETED,
                item_delete(itemstore_root(config.itemstore_ctx),
                            "deleted.before.fire").status);
  (void)uv_run(&loop, UV_RUN_DEFAULT);
  ASSERT_TRUE(find_item(itemstore_root(config.itemstore_ctx),
                        "observed.deleted") == NULL);
  ASSERT_TRUE(find_task_by_id(id) == NULL);
  ASSERT_EQ_INT(0, uv_loop_alive(&loop));

  finalise_tasks(&loop);
  ASSERT_EQ_INT(0, uv_loop_close(&loop));
  teardown_libcall_runtime();
}

void test_newgametask_resolves_target_on_each_firing(void) {
  uv_loop_t loop;
  ASSERT_EQ_INT(0, uv_loop_init(&loop));
  setup_libcall_runtime();
  config.loop = &loop;
  init_tasks();
  insert_compiled_code(itemstore_root(config.itemstore_ctx), "dynamic.target",
                       "observed.dynamic = 1;");
  uint64_t id = schedule_task("DYNAMIC.TARGET", 0, 1);
  ASSERT_NOT_NULL(find_task_by_id(id));

  (void)uv_run(&loop, UV_RUN_ONCE);
  ASSERT_EQ_INT(1, item_value(find_item(itemstore_root(config.itemstore_ctx),
                                        "observed.dynamic"))->i);

  insert_compiled_code(itemstore_root(config.itemstore_ctx), "dynamic.target",
                       "observed.dynamic = 2;");
  (void)uv_run(&loop, UV_RUN_ONCE);
  ASSERT_EQ_INT(2, item_value(find_item(itemstore_root(config.itemstore_ctx),
                                        "observed.dynamic"))->i);

  ASSERT_EQ_INT(ITEM_MUTATION_DELETED,
                item_delete(itemstore_root(config.itemstore_ctx),
                            "dynamic.target").status);
  (void)uv_run(&loop, UV_RUN_ONCE);
  ASSERT_EQ_INT(2, item_value(find_item(itemstore_root(config.itemstore_ctx),
                                        "observed.dynamic"))->i);
  ASSERT_NOT_NULL(find_task_by_id(id));

  insert_compiled_code(itemstore_root(config.itemstore_ctx), "dynamic.target",
                       "observed.dynamic = 3;");
  (void)uv_run(&loop, UV_RUN_ONCE);
  ASSERT_EQ_INT(3, item_value(find_item(itemstore_root(config.itemstore_ctx),
                                        "observed.dynamic"))->i);
  ASSERT_TRUE(request_task_close(find_task_by_id(id)));

  finalise_tasks(&loop);
  ASSERT_EQ_INT(0, uv_loop_close(&loop));
  teardown_libcall_runtime();
}

void test_task_callback_pins_target_during_execution(void) {
  uv_loop_t loop;
  ASSERT_EQ_INT(0, uv_loop_init(&loop));
  setup_libcall_runtime();
  config.loop = &loop;
  init_tasks();
  insert_compiled_code(itemstore_root(config.itemstore_ctx), "pinned.target",
      "observed.pin_replace = sys.compile{\"pinned.target = code ( observed.pin_replaced = 1; );\"};"
      "observed.pin_delete = sys.delete{&pinned.target};");
  uint64_t id = schedule_task("pinned.target", 0, 0);
  (void)uv_run(&loop, UV_RUN_DEFAULT);

  ITEM_t *target = find_item(itemstore_root(config.itemstore_ctx),
                             "pinned.target");
  ASSERT_NOT_NULL(target);
  ASSERT_EQ_INT(ITEM_code, item_kind(target));
  ASSERT_TRUE(find_item(itemstore_root(config.itemstore_ctx),
                        "observed.pin_replaced") == NULL);
  ITEM_t *replace_result = find_item(itemstore_root(config.itemstore_ctx),
                                     "observed.pin_replace");
  ITEM_t *delete_result = find_item(itemstore_root(config.itemstore_ctx),
                                    "observed.pin_delete");
  ASSERT_NOT_NULL(replace_result);
  ASSERT_NOT_NULL(delete_result);
  ASSERT_EQ_INT(VALUE_bool, item_value(replace_result)->type);
  ASSERT_EQ_INT(VALUE_bool, item_value(delete_result)->type);
  ASSERT_EQ_INT(0, item_value(replace_result)->i);
  ASSERT_EQ_INT(0, item_value(delete_result)->i);
  ASSERT_TRUE(find_task_by_id(id) == NULL);

  finalise_tasks(&loop);
  ASSERT_EQ_INT(0, uv_loop_close(&loop));
  teardown_libcall_runtime();
}

void test_tasks_are_runtime_only_across_sys_save_and_load(void) {
  uv_loop_t loop;
  char store_path[128];
  ASSERT_EQ_INT(0, uv_loop_init(&loop));
  setup_libcall_runtime();
  config.loop = &loop;
  init_tasks();
  ASSERT_EQ_INT(0, test_make_temp_path("sin-task-runtime-only", store_path,
                                      sizeof(store_path)));
  insert_compiled_code(itemstore_root(config.itemstore_ctx), "persist.target",
                       "observed.persist = 1;");
  uint64_t id = schedule_task("persist.target", 100, 100);
  RuntimeContext *ctx = test_ctx();
  ctx->itemstore_filename = store_path;
  (void)lc_sys_save(ctx, NULL, itemstore_root(config.itemstore_ctx));
  assert_bool_return(pop_stack(config.vm->stack), 1);
  ASSERT_NOT_NULL(find_task_by_id(id));
  ASSERT_EQ_INT(1, (int)task_list_count());
  finalise_tasks(&loop);
  init_tasks();
  ASSERT_EQ_INT(0, (int)task_list_count());
  ASSERT_TRUE(find_task_by_id(id) == NULL);
  ITEM_t *loaded = load_itemstore(store_path);
  ASSERT_NOT_NULL(loaded);
  ASSERT_NOT_NULL(find_item(loaded, "persist.target"));
  ASSERT_TRUE(find_item(loaded, "observed.persist") == NULL);
  ASSERT_EQ_INT(0, (int)task_list_count());
  ASSERT_TRUE(find_task_by_id(id) == NULL);
  destroy_item(loaded);
  ASSERT_EQ_INT(0, unlink(store_path));

  finalise_tasks(&loop);
  ASSERT_EQ_INT(0, uv_loop_close(&loop));
  teardown_libcall_runtime();
}

static void assert_newgametask_invalid_interval_returns_nil(int64_t start_value,
                                                            int64_t repeat_value) {
  static int task_suffix = 0;
  char task_name[32];
  snprintf(task_name, sizeof(task_name), "valid.task.%d", task_suffix++);

  uint8_t *bytecode = malloc(1);
  ASSERT_NOT_NULL(bytecode);
  bytecode[0] = 'h';
  ITEM_t *task_item = test_item_set_code(itemstore_root(config.itemstore_ctx), task_name, 1, bytecode);
  ASSERT_NOT_NULL(task_item);

  RuntimeContext *ctx = test_ctx();
  ctx->loop = NULL;

  VALUE_t itemname = {VALUE_str, {.s = strdup(task_name)}};
  VALUE_t start = {VALUE_int, {.i = start_value}};
  VALUE_t repeat = {VALUE_int, {.i = repeat_value}};
  push_stack(config.vm->stack, itemname);
  push_stack(config.vm->stack, start);
  push_stack(config.vm->stack, repeat);

  (void)lc_task_newgametask(ctx, NULL, itemstore_root(config.itemstore_ctx));

  VALUE_t ret = pop_stack(config.vm->stack);
  ASSERT_EQ_INT(VALUE_nil, ret.type);
  assert_invalid_args_detail_contains("task.newgametask intervals must be non-negative and within timer range");
}

void test_newgametask_rejects_invalid_intervals_before_timer_start(void) {
  setup_libcall_runtime();

  assert_newgametask_invalid_interval_returns_nil(-1, 1);
  assert_newgametask_invalid_interval_returns_nil(1, -1);
  assert_newgametask_invalid_interval_returns_nil(-1, -1);
  assert_newgametask_invalid_interval_returns_nil((INT64_MAX / 100) + 1, 1);
  assert_newgametask_invalid_interval_returns_nil(1, (INT64_MAX / 100) + 1);

  push_stack(config.vm->stack,
             (VALUE_t){VALUE_str, {.s = strdup("not-valid")}});
  push_stack(config.vm->stack, (VALUE_t){VALUE_int, {.i = 0}});
  push_stack(config.vm->stack, (VALUE_t){VALUE_int, {.i = 0}});
  (void)lc_task_newgametask(test_ctx(), NULL,
                            itemstore_root(config.itemstore_ctx));
  VALUE_t result = pop_stack(config.vm->stack);
  ASSERT_EQ_INT(VALUE_nil, result.type);
  ASSERT_EQ_INT(ERR_RUNTIME_NOSUCHITEM,
                item_value(find_item(itemstore_root(config.itemstore_ctx),
                                     "error"))->i);

  teardown_libcall_runtime();
}

void test_newgametask_rejects_missing_event_loop_before_returning_task_id(void) {
  setup_libcall_runtime();

  uint8_t *bytecode = malloc(1);
  ASSERT_NOT_NULL(bytecode);
  bytecode[0] = 'h';
  ASSERT_NOT_NULL(test_item_set_code(itemstore_root(config.itemstore_ctx), "valid.loopless.task", 1, bytecode));

  RuntimeContext *ctx = test_ctx();
  ctx->loop = NULL;

  push_stack(config.vm->stack, (VALUE_t){VALUE_str, {.s = strdup("valid.loopless.task")}});
  push_stack(config.vm->stack, (VALUE_t){VALUE_int, {.i = 1}});
  push_stack(config.vm->stack, (VALUE_t){VALUE_int, {.i = 1}});
  (void)lc_task_newgametask(ctx, NULL, itemstore_root(config.itemstore_ctx));

  VALUE_t ret = pop_stack(config.vm->stack);
  ASSERT_EQ_INT(VALUE_nil, ret.type);
  assert_invalid_args_detail_contains("task.newgametask requires an active event loop");
  ASSERT_TRUE(find_task_by_id(1) == NULL);

  teardown_libcall_runtime();
}

void test_task_introspection_thisid_ordinary_context_returns_nil(void) {
  setup_libcall_runtime();

  /* Set a pre-existing error item to verify it is preserved. */
  set_error_item(itemstore_root(config.itemstore_ctx), ERR_NETWORK_ERROR, "prior error", NULL);

  (void)lc_task_thisid(test_ctx(), NULL, itemstore_root(config.itemstore_ctx));
  VALUE_t ret = pop_stack(config.vm->stack);
  ASSERT_EQ_INT(VALUE_nil, ret.type);

  /* Verify the unrelated error was preserved. */
  ITEM_t *err = find_item(itemstore_root(config.itemstore_ctx), "error");
  ASSERT_NOT_NULL(err);
  ASSERT_EQ_INT(VALUE_int, item_value(err)->type);
  ASSERT_EQ_INT(ERR_NETWORK_ERROR, item_value(err)->i);

  RuntimeContext *task_ctx = test_ctx();
  task_ctx->current_task_id = 7;
  (void)lc_task_thisid(task_ctx, NULL, itemstore_root(config.itemstore_ctx));
  ret = pop_stack(config.vm->stack);
  ASSERT_EQ_INT(VALUE_int, ret.type);
  ASSERT_EQ_INT(7, ret.i);
  ASSERT_EQ_INT(ERR_NETWORK_ERROR, item_value(find_item(itemstore_root(config.itemstore_ctx), "error"))->i);

  teardown_libcall_runtime();
}

void test_task_introspection_exists_valid_and_invalid_ids(void) {
  setup_libcall_runtime();

  /* Set a pre-existing error to verify preservation. */
  set_error_item(itemstore_root(config.itemstore_ctx), ERR_RUNTIME_INVALIDARGS, "some prior error", NULL);

  /* Negative id → false, error preserved. */
  push_stack(config.vm->stack, (VALUE_t){VALUE_int, {.i = -1}});
  (void)lc_task_exists(test_ctx(), NULL, itemstore_root(config.itemstore_ctx));
  VALUE_t ret = pop_stack(config.vm->stack);
  ASSERT_EQ_INT(VALUE_bool, ret.type);
  ASSERT_EQ_INT(0, ret.i);
  ITEM_t *err = find_item(itemstore_root(config.itemstore_ctx), "error");
  ASSERT_NOT_NULL(err);
  ASSERT_EQ_INT(ERR_RUNTIME_INVALIDARGS, item_value(err)->i);

  /* Unknown id → false, error preserved. */
  push_stack(config.vm->stack, (VALUE_t){VALUE_int, {.i = 99999}});
  (void)lc_task_exists(test_ctx(), NULL, itemstore_root(config.itemstore_ctx));
  ret = pop_stack(config.vm->stack);
  ASSERT_EQ_INT(VALUE_bool, ret.type);
  ASSERT_EQ_INT(0, ret.i);
  err = find_item(itemstore_root(config.itemstore_ctx), "error");
  ASSERT_NOT_NULL(err);
  ASSERT_EQ_INT(ERR_RUNTIME_INVALIDARGS, item_value(err)->i);

  teardown_libcall_runtime();
}

void test_task_exists_rejects_non_integer(void) {
  setup_libcall_runtime();

  /* Float rejection: consumes + cleans up, sets ERR_RUNTIME_INVALIDARGS,
   * returns nil, detail mentions task.exists. */
  push_stack(config.vm->stack, (VALUE_t){VALUE_float, {.f = 2.5}});
  (void)lc_task_exists(test_ctx(), NULL, itemstore_root(config.itemstore_ctx));
  VALUE_t ret = pop_stack(config.vm->stack);
  ASSERT_EQ_INT(VALUE_nil, ret.type);
  assert_invalid_args_float_detail_contains("task.exists");

  /* Owned string rejection: consumes + cleans up, sets error. */
  push_stack(config.vm->stack, (VALUE_t){VALUE_str, {.s = strdup("xyz")}});
  (void)lc_task_exists(test_ctx(), NULL, itemstore_root(config.itemstore_ctx));
  ret = pop_stack(config.vm->stack);
  ASSERT_EQ_INT(VALUE_nil, ret.type);
  assert_invalid_args_detail_contains("task.exists");

  /* Bool rejection. */
  push_stack(config.vm->stack, (VALUE_t){VALUE_bool, {.i = 1}});
  (void)lc_task_exists(test_ctx(), NULL, itemstore_root(config.itemstore_ctx));
  ret = pop_stack(config.vm->stack);
  ASSERT_EQ_INT(VALUE_nil, ret.type);
  assert_invalid_args_detail_contains("task.exists");

  teardown_libcall_runtime();
}

void test_task_introspection_count_and_exists_with_lifecycle(void) {
  uv_loop_t loop;
  ASSERT_EQ_INT(0, uv_loop_init(&loop));
  setup_libcall_runtime();
  config.loop = &loop;
  init_tasks();
  set_error_item(itemstore_root(config.itemstore_ctx), ERR_RUNTIME_INVALIDARGS, "prior error", NULL);

  (void)lc_task_count(test_ctx(), NULL, itemstore_root(config.itemstore_ctx));
  VALUE_t ret = pop_stack(config.vm->stack);
  ASSERT_EQ_INT(VALUE_int, ret.type);
  ASSERT_EQ_INT(0, ret.i);

  TASK_t *task = make_task("lifecycle.task", 1);
  ASSERT_NOT_NULL(task);
  uint64_t id = task->id;
  ASSERT_TRUE(start_task_timer(task, &loop, execute_task_cb, 1000));

  (void)lc_task_count(test_ctx(), NULL, itemstore_root(config.itemstore_ctx));
  ret = pop_stack(config.vm->stack);
  ASSERT_EQ_INT(VALUE_int, ret.type);
  ASSERT_EQ_INT(1, ret.i);
  push_stack(config.vm->stack, (VALUE_t){VALUE_int, {.i = (int64_t)id}});
  (void)lc_task_exists(test_ctx(), NULL, itemstore_root(config.itemstore_ctx));
  assert_bool_return(pop_stack(config.vm->stack), 1);

  ASSERT_TRUE(request_task_close(task));
  (void)lc_task_count(test_ctx(), NULL, itemstore_root(config.itemstore_ctx));
  ret = pop_stack(config.vm->stack);
  ASSERT_EQ_INT(VALUE_int, ret.type);
  ASSERT_EQ_INT(0, ret.i);
  push_stack(config.vm->stack, (VALUE_t){VALUE_int, {.i = (int64_t)id}});
  (void)lc_task_exists(test_ctx(), NULL, itemstore_root(config.itemstore_ctx));
  assert_bool_return(pop_stack(config.vm->stack), 0);
  ASSERT_EQ_INT(ERR_RUNTIME_INVALIDARGS, item_value(find_item(itemstore_root(config.itemstore_ctx), "error"))->i);

  finalise_tasks(&loop);
  ASSERT_EQ_INT(0, uv_loop_close(&loop));
  teardown_libcall_runtime();
}

void test_task_thisid_in_callback_survives_self_close(void) {
  uv_loop_t loop;
  ASSERT_EQ_INT(0, uv_loop_init(&loop));
  setup_libcall_runtime();
  config.loop = &loop;
  init_tasks();
  insert_compiled_code(itemstore_root(config.itemstore_ctx), "callback.helper", "return task.thisid;");
  insert_compiled_code(itemstore_root(config.itemstore_ctx), "task.callback",
      "observed.before = callback.helper;"
      "task.killtask{task.thisid};"
      "observed.after = task.thisid;");
  set_error_item(itemstore_root(config.itemstore_ctx), ERR_NETWORK_ERROR, "prior error", NULL);

  TASK_t *task = make_task("task.callback", 1);
  ASSERT_NOT_NULL(task);
  uint64_t id = task->id;
  task->itemstore = config.itemstore_ctx;
  ASSERT_TRUE(start_task_timer(task, &loop, execute_task_cb, 0));
  (void)uv_run(&loop, UV_RUN_DEFAULT);

  ITEM_t *before = find_item(itemstore_root(config.itemstore_ctx), "observed.before");
  ITEM_t *after = find_item(itemstore_root(config.itemstore_ctx), "observed.after");
  ASSERT_NOT_NULL(before);
  ASSERT_NOT_NULL(after);
  ASSERT_EQ_INT(VALUE_int, item_value(before)->type);
  ASSERT_EQ_INT(VALUE_int, item_value(after)->type);
  ASSERT_EQ_INT((int64_t)id, item_value(before)->i);
  ASSERT_EQ_INT((int64_t)id, item_value(after)->i);
  ASSERT_TRUE(find_task_by_id(id) == NULL);

  finalise_tasks(&loop);
  ASSERT_EQ_INT(0, uv_loop_close(&loop));
  teardown_libcall_runtime();
}

void test_task_callback_frees_aggregate_return_values(void) {
  uv_loop_t loop;
  ASSERT_EQ_INT(0, uv_loop_init(&loop));
  setup_libcall_runtime();
  config.loop = &loop;
  init_tasks();
  insert_compiled_code(itemstore_root(config.itemstore_ctx), "task.list_return",
                       "return #[1, &fred];");
  insert_compiled_code(itemstore_root(config.itemstore_ctx), "task.ref_return",
                       "return &fred;");
  push_stack(config.vm->stack,
      (VALUE_t){VALUE_str, {.s = strdup(
          "frame_task.param_default = code {@p} ( observed.task_param = @p; );")}});
  (void)lc_sys_compile(test_ctx(), NULL, itemstore_root(config.itemstore_ctx));
  VALUE_t compiled = pop_stack(config.vm->stack);
  ASSERT_EQ_INT(VALUE_bool, compiled.type);
  ASSERT_EQ_INT(1, compiled.i);

  TASK_t *list_task = make_task("task.list_return", 0);
  TASK_t *ref_task = make_task("task.ref_return", 0);
  TASK_t *param_task = make_task("frame_task.param_default", 0);
  ASSERT_NOT_NULL(list_task);
  ASSERT_NOT_NULL(ref_task);
  ASSERT_NOT_NULL(param_task);
  list_task->itemstore = config.itemstore_ctx;
  ref_task->itemstore = config.itemstore_ctx;
  param_task->itemstore = config.itemstore_ctx;
  uint64_t list_id = list_task->id;
  uint64_t ref_id = ref_task->id;
  uint64_t param_id = param_task->id;
  ASSERT_TRUE(start_task_timer(list_task, &loop, execute_task_cb, 0));
  ASSERT_TRUE(start_task_timer(ref_task, &loop, execute_task_cb, 0));
  ASSERT_TRUE(start_task_timer(param_task, &loop, execute_task_cb, 0));
  (void)uv_run(&loop, UV_RUN_DEFAULT);
  ASSERT_TRUE(find_task_by_id(list_id) == NULL);
  ASSERT_TRUE(find_task_by_id(ref_id) == NULL);
  ASSERT_TRUE(find_task_by_id(param_id) == NULL);
  ITEM_t *observed_param = find_item(itemstore_root(config.itemstore_ctx),
                                     "observed.task_param");
  ASSERT_NOT_NULL(observed_param);
  ASSERT_EQ_INT(VALUE_nil, item_value(observed_param)->type);

  finalise_tasks(&loop);
  ASSERT_EQ_INT(0, uv_loop_close(&loop));
  teardown_libcall_runtime();
}

void test_newgametask_child_callback_uses_own_identity(void) {
  uv_loop_t loop;
  ASSERT_EQ_INT(0, uv_loop_init(&loop));
  setup_libcall_runtime();
  config.loop = &loop;
  init_tasks();
  insert_compiled_code(itemstore_root(config.itemstore_ctx), "task.child",
                       "observed.child = task.thisid;");

  TASK_t *creator = make_task("task.creator", 1);
  ASSERT_NOT_NULL(creator);
  RuntimeContext *creator_ctx = &creator->runtime_context;
  runtime_context_init(creator_ctx, creator->vm);
  creator_ctx->itemstore = config.itemstore_ctx;
  creator_ctx->loop = &loop;
  creator_ctx->current_task_id = creator->id;
  push_stack(creator->vm->stack, (VALUE_t){VALUE_str, {.s = strdup("TaSk.ChIlD")}});
  push_stack(creator->vm->stack, (VALUE_t){VALUE_int, {.i = 0}});
  push_stack(creator->vm->stack, (VALUE_t){VALUE_int, {.i = 1}});
  (void)lc_task_newgametask(creator_ctx, NULL, itemstore_root(config.itemstore_ctx));
  VALUE_t result = pop_stack(creator->vm->stack);
  ASSERT_EQ_INT(VALUE_int, result.type);
  uint64_t child_id = (uint64_t)result.i;
  ASSERT_TRUE(child_id != creator->id);
  TASK_t *child = find_task_by_id(child_id);
  ASSERT_NOT_NULL(child);
  (void)lc_task_thisid(&child->runtime_context, NULL, itemstore_root(config.itemstore_ctx));
  result = pop_stack(child->vm->stack);
  ASSERT_EQ_INT(VALUE_nil, result.type);

  (void)uv_run(&loop, UV_RUN_ONCE);
  ITEM_t *observed = find_item(itemstore_root(config.itemstore_ctx), "observed.child");
  ASSERT_NOT_NULL(observed);
  ASSERT_EQ_INT(VALUE_int, item_value(observed)->type);
  ASSERT_EQ_INT((int64_t)child_id, item_value(observed)->i);
  ASSERT_TRUE(find_task_by_id(child_id) == child);
  (void)lc_task_thisid(&child->runtime_context, NULL, itemstore_root(config.itemstore_ctx));
  result = pop_stack(child->vm->stack);
  ASSERT_EQ_INT(VALUE_nil, result.type);

  ASSERT_TRUE(request_task_close(child));
  destroy_task(creator);
  finalise_tasks(&loop);
  ASSERT_EQ_INT(0, uv_loop_close(&loop));
  teardown_libcall_runtime();
}

void test_newgametask_itemref_creates_and_executes_one_shot(void) {
  uv_loop_t loop;
  ASSERT_EQ_INT(0, uv_loop_init(&loop));
  setup_libcall_runtime();
  config.loop = &loop;
  init_tasks();
  insert_compiled_code(itemstore_root(config.itemstore_ctx), "task.itemref",
                       "observed.itemref = 1;");

  SIN_ITEMREF_t *ref = sin_itemref_create("TaSk.ItEmReF");
  ASSERT_NOT_NULL(ref);
  push_stack(config.vm->stack,
      (VALUE_t){VALUE_itemref, {.itemref = ref}});
  push_stack(config.vm->stack, (VALUE_t){VALUE_int, {.i = 0}});
  push_stack(config.vm->stack, (VALUE_t){VALUE_int, {.i = 0}});
  (void)lc_task_newgametask(test_ctx(), NULL,
                            itemstore_root(config.itemstore_ctx));

  VALUE_t result = pop_stack(config.vm->stack);
  ASSERT_EQ_INT(VALUE_int, result.type);
  uint64_t task_id = (uint64_t)result.i;
  TASK_t *task = find_task_by_id(task_id);
  ASSERT_NOT_NULL(task);
  ASSERT_TRUE(strcmp(task->itemname, "task.itemref") == 0);

  (void)uv_run(&loop, UV_RUN_DEFAULT);
  ITEM_t *observed = find_item(itemstore_root(config.itemstore_ctx),
                               "observed.itemref");
  ASSERT_NOT_NULL(observed);
  ASSERT_EQ_INT(VALUE_int, item_value(observed)->type);
  ASSERT_EQ_INT(1, item_value(observed)->i);
  ASSERT_TRUE(find_task_by_id(task_id) == NULL);

  finalise_tasks(&loop);
  ASSERT_EQ_INT(0, uv_loop_close(&loop));
  teardown_libcall_runtime();
}
