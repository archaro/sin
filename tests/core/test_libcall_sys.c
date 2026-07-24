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
#include "item_internal.h"
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
void lc_sys_backup_set_timestamp_for_tests(const char *ts);
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

static VALUE_t call_sys_noarg(OP_t func, RuntimeContext *ctx) {
  (void)func(ctx, NULL, NULL);
  return pop_stack(ctx->vm->stack);
}
static VALUE_t call_sys_name(OP_t func, RuntimeContext *ctx, VALUE_t name) {
  push_stack(ctx->vm->stack, name);
  (void)func(ctx, NULL, NULL);
  return pop_stack(ctx->vm->stack);
}

static void assert_string_return(VALUE_t value, const char *expected) {
  ASSERT_EQ_INT(VALUE_str, value.type);
  ASSERT_NOT_NULL(value.s);
  ASSERT_TRUE(strcmp(value.s, expected) == 0);
  value_free(&value);
}

static ITEM_t *insert_halt_code(ITEM_t *root, const char *name) {
  uint8_t *bytecode = malloc(3u);
  ASSERT_NOT_NULL(bytecode);
  bytecode[0] = 0;
  bytecode[1] = 0;
  bytecode[2] = (uint8_t)'h';
  ITEM_t *item = insert_code_item(root, name, 3u, bytecode);
  ASSERT_NOT_NULL(item);
  return item;
}

static bool race_pre_publish_hook_fired;
static bool race_pre_publish_path_matches;
static bool race_pre_publish_symlink_created;
static char race_pre_publish_path[256];

static void race_pre_publish_hook(const char *path) {
  if (race_pre_publish_hook_fired) return;
  race_pre_publish_hook_fired = true;
  race_pre_publish_path_matches = strcmp(path, race_pre_publish_path) == 0;
  if (race_pre_publish_path_matches) {
    race_pre_publish_symlink_created =
        symlink("/nonexistent_race_target", path) == 0;
  }
}

static void assert_persistence_error(const char *operation,
                                     const char *target,
                                     const char *current_item) {
  ITEM_t *error = find_item(config.itemroot, "error");
  ASSERT_NOT_NULL(error);
  ASSERT_EQ_INT(VALUE_int, error->value.type);
  ASSERT_EQ_INT(ERR_RUNTIME_PERSISTENCE, error->value.i);

  ITEM_t *message = find_item(config.itemroot, "error.msg");
  ASSERT_NOT_NULL(message);
  ASSERT_EQ_INT(VALUE_str, message->value.type);
  ASSERT_TRUE(strstr(message->value.s, errmsg[ERR_RUNTIME_PERSISTENCE]) != NULL);
  ASSERT_TRUE(strstr(message->value.s, operation) != NULL);
  ASSERT_TRUE(strstr(message->value.s, target) != NULL);

  ITEM_t *provenance = find_item(config.itemroot, "error.item");
  ASSERT_NOT_NULL(provenance);
  ASSERT_EQ_INT(VALUE_str, provenance->value.type);
  ASSERT_TRUE(strcmp(provenance->value.s, current_item) == 0);
}

static int persistence_sync_calls;
static int persistence_directory_sync_calls;

static bool count_persistence_sync(FILE *file, const char *path) {
  (void)file;
  (void)path;
  persistence_sync_calls++;
  return true;
}

static bool count_persistence_directory_sync(const char *path) {
  (void)path;
  persistence_directory_sync_calls++;
  return true;
}

void test_sys_item_libcalls(void) {
  setup_libcall_runtime();

  ASSERT_NOT_NULL(insert_item(config.itemroot, "parent.first",
                              (VALUE_t){VALUE_int, {.i = 1}}));
  ASSERT_NOT_NULL(insert_item(config.itemroot, "parent.second",
                              (VALUE_t){VALUE_int, {.i = 2}}));
  ASSERT_NOT_NULL(insert_item(config.itemroot, "victim",
                              (VALUE_t){VALUE_bool, {.i = 1}}));

  push_stack(config.vm->stack, (VALUE_t){VALUE_str, {.s = strdup("victim")}});
  (void)lc_sys_exists(test_ctx(), NULL, config.itemroot);
  VALUE_t ret = pop_stack(config.vm->stack);
  ASSERT_EQ_INT(VALUE_bool, ret.type);
  ASSERT_EQ_INT(1, ret.i);

  push_stack(config.vm->stack, (VALUE_t){VALUE_str, {.s = strdup("missing")}});
  (void)lc_sys_exists(test_ctx(), NULL, config.itemroot);
  ret = pop_stack(config.vm->stack);
  ASSERT_EQ_INT(VALUE_bool, ret.type);
  ASSERT_EQ_INT(0, ret.i);

  push_stack(config.vm->stack, (VALUE_t){VALUE_str, {.s = strdup("victim")}});
  (void)lc_sys_delete(test_ctx(), NULL, config.itemroot);
  ret = pop_stack(config.vm->stack);
  ASSERT_EQ_INT(VALUE_nil, ret.type);
  ASSERT_TRUE(find_item(config.itemroot, "victim") == NULL);

  push_stack(config.vm->stack, (VALUE_t){VALUE_str, {.s = strdup("parent")}});
  push_stack(config.vm->stack, (VALUE_t){VALUE_int, {.i = 1}});
  (void)lc_sys_nthname(test_ctx(), NULL, config.itemroot);
  ret = pop_stack(config.vm->stack);
  ASSERT_EQ_INT(VALUE_str, ret.type);
  ASSERT_TRUE(strcmp(ret.s, "second") == 0);
  FREE_STR(ret);

  push_stack(config.vm->stack, (VALUE_t){VALUE_int, {.i = 0}});
  (void)lc_sys_rootname(test_ctx(), NULL, config.itemroot);
  ret = pop_stack(config.vm->stack);
  ASSERT_EQ_INT(VALUE_str, ret.type);
  ASSERT_TRUE(strcmp(ret.s, "parent") == 0);
  FREE_STR(ret);

  push_stack(config.vm->stack, (VALUE_t){VALUE_int, {.i = 1}});
  (void)lc_sys_exists(test_ctx(), NULL, config.itemroot);
  ret = pop_stack(config.vm->stack);
  ASSERT_EQ_INT(VALUE_bool, ret.type);
  ASSERT_EQ_INT(0, ret.i);
  assert_invalid_args_detail_contains("sys.exists");

  teardown_libcall_runtime();
}
void test_sys_persistence_libcalls(void) {
  setup_libcall_runtime();

  char store_path[128];
  ASSERT_EQ_INT(0, test_make_temp_path("sin-sys-save", store_path,
                                      sizeof(store_path)));
  ITEM_t *caller = insert_item(config.itemroot, "persistence.caller",
                               (VALUE_t){VALUE_bool, {.i = 1}});
  ASSERT_NOT_NULL(caller);
  ASSERT_NOT_NULL(insert_item(config.itemroot, "checkpoint.value",
                              (VALUE_t){VALUE_int, {.i = 1}}));

  RuntimeContext ctx;
  runtime_context_init(&ctx, config.vm);
  ctx.itemroot = config.itemroot;
  ctx.itemstore_filename = store_path;
  ctx.current_item = caller;

  itemstore_set_sync_hook_for_tests(count_persistence_sync);
  itemstore_set_directory_sync_hook_for_tests(
      count_persistence_directory_sync);

  set_error_item(config.itemroot, ERR_RUNTIME_INVALIDARGS,
                 "unrelated prior error", caller);
  ITEM_t *prior_message = find_item(config.itemroot, "error.msg");
  ASSERT_NOT_NULL(prior_message);
  ASSERT_EQ_INT(VALUE_str, prior_message->value.type);
  char *prior_message_text = strdup(prior_message->value.s);
  ASSERT_NOT_NULL(prior_message_text);

  config.itemstore_durability = ITEMSTORE_DURABLE_FULL;
  ctx.itemstore_durability = ITEMSTORE_DURABLE_FAST;
  persistence_sync_calls = 0;
  persistence_directory_sync_calls = 0;
  (void)lc_sys_save(&ctx, NULL, caller);
  assert_bool_return(pop_stack(config.vm->stack), 1);
  ASSERT_EQ_INT(0, persistence_sync_calls);
  ASSERT_EQ_INT(0, persistence_directory_sync_calls);
  ITEM_t *error = find_item(config.itemroot, "error");
  ASSERT_NOT_NULL(error);
  ASSERT_EQ_INT(ERR_RUNTIME_INVALIDARGS, error->value.i);
  prior_message = find_item(config.itemroot, "error.msg");
  ASSERT_NOT_NULL(prior_message);
  ASSERT_EQ_INT(VALUE_str, prior_message->value.type);
  ASSERT_TRUE(strcmp(prior_message->value.s, prior_message_text) == 0);
  free(prior_message_text);

  ASSERT_NOT_NULL(insert_item(config.itemroot, "checkpoint.value",
                              (VALUE_t){VALUE_int, {.i = 2}}));
  ITEM_t *loaded = load_itemstore(store_path);
  ASSERT_NOT_NULL(loaded);
  ITEM_t *loaded_checkpoint = find_item(loaded, "checkpoint.value");
  ASSERT_NOT_NULL(loaded_checkpoint);
  ASSERT_EQ_INT(VALUE_int, loaded_checkpoint->value.type);
  ASSERT_EQ_INT(1, loaded_checkpoint->value.i);
  destroy_item(loaded);

  config.itemstore_durability = ITEMSTORE_DURABLE_FAST;
  ctx.itemstore_durability = ITEMSTORE_DURABLE_FULL;
  persistence_sync_calls = 0;
  persistence_directory_sync_calls = 0;
  (void)lc_sys_save(&ctx, NULL, caller);
  assert_bool_return(pop_stack(config.vm->stack), 1);
  ASSERT_EQ_INT(1, persistence_sync_calls);
  ASSERT_EQ_INT(1, persistence_directory_sync_calls);

  ASSERT_NOT_NULL(insert_item(config.itemroot, "backup.only",
                              (VALUE_t){VALUE_int, {.i = 3}}));
  set_error_item(config.itemroot, ERR_RUNTIME_INVALIDARGS,
                 "backup prior error", caller);
  prior_message = find_item(config.itemroot, "error.msg");
  ASSERT_NOT_NULL(prior_message);
  prior_message_text = strdup(prior_message->value.s);
  ASSERT_NOT_NULL(prior_message_text);
  ctx.itemstore_durability = ITEMSTORE_DURABLE_FULL;
  persistence_sync_calls = 0;
  persistence_directory_sync_calls = 0;
  (void)lc_sys_backup(&ctx, NULL, caller);
  assert_bool_return(pop_stack(config.vm->stack), 1);
  ASSERT_EQ_INT(1, persistence_sync_calls);
  ASSERT_EQ_INT(1, persistence_directory_sync_calls);
  error = find_item(config.itemroot, "error");
  ASSERT_NOT_NULL(error);
  ASSERT_EQ_INT(ERR_RUNTIME_INVALIDARGS, error->value.i);
  prior_message = find_item(config.itemroot, "error.msg");
  ASSERT_NOT_NULL(prior_message);
  ASSERT_TRUE(strcmp(prior_message->value.s, prior_message_text) == 0);
  free(prior_message_text);

  loaded = load_itemstore(store_path);
  ASSERT_NOT_NULL(loaded);
  ASSERT_TRUE(find_item(loaded, "backup.only") == NULL);
  destroy_item(loaded);

  char backup_pattern[sizeof(store_path) + 4u];
  int written = snprintf(backup_pattern, sizeof(backup_pattern), "%s_*",
                         store_path);
  ASSERT_TRUE(written > 0 && (size_t)written < sizeof(backup_pattern));
  glob_t backups = {0};
  ASSERT_EQ_INT(0, glob(backup_pattern, 0, NULL, &backups));
  ASSERT_EQ_INT(1, backups.gl_pathc);
  loaded = load_itemstore(backups.gl_pathv[0]);
  ASSERT_NOT_NULL(loaded);
  ASSERT_NOT_NULL(find_item(loaded, "backup.only"));
  loaded_checkpoint = find_item(loaded, "checkpoint.value");
  ASSERT_NOT_NULL(loaded_checkpoint);
  ASSERT_EQ_INT(2, loaded_checkpoint->value.i);
  destroy_item(loaded);
  ASSERT_EQ_INT(0, unlink(backups.gl_pathv[0]));
  globfree(&backups);

  lc_sys_backup_set_timestamp_for_tests("20000101-000000");
  char base_backup[256];
  written = snprintf(base_backup, sizeof(base_backup), "%s_%s", store_path,
                     "20000101-000000");
  ASSERT_TRUE(written > 0 && (size_t)written < sizeof(base_backup));
  FILE *base_file = fopen(base_backup, "wb");
  ASSERT_NOT_NULL(base_file);
  ASSERT_TRUE(fputs("occupied backup bytes", base_file) >= 0);
  ASSERT_EQ_INT(0, fclose(base_file));
  char base_snapshot[sizeof(base_backup) + 16u];
  written = snprintf(base_snapshot, sizeof(base_snapshot), "%s.snapshot",
                     base_backup);
  ASSERT_TRUE(written > 0 && (size_t)written < sizeof(base_snapshot));
  ASSERT_EQ_INT(0, link(base_backup, base_snapshot));
  char backup_one[sizeof(base_backup) + 3u];
  written = snprintf(backup_one, sizeof(backup_one), "%s_1", base_backup);
  ASSERT_TRUE(written > 0 && (size_t)written < sizeof(backup_one));

  ASSERT_NOT_NULL(insert_item(config.itemroot, "backup.e2e",
                              (VALUE_t){VALUE_int, {.i = 42}}));
  ctx.itemstore_durability = ITEMSTORE_DURABLE_FAST;
  (void)lc_sys_backup(&ctx, NULL, caller);
  assert_bool_return(pop_stack(config.vm->stack), 1);
  loaded = load_itemstore(backup_one);
  ASSERT_NOT_NULL(loaded);
  ASSERT_NOT_NULL(find_item(loaded, "backup.e2e"));
  destroy_item(loaded);
  char backup_one_snapshot[sizeof(backup_one) + 16u];
  written = snprintf(backup_one_snapshot, sizeof(backup_one_snapshot),
                     "%s.snapshot", backup_one);
  ASSERT_TRUE(written > 0 && (size_t)written < sizeof(backup_one_snapshot));
  ASSERT_EQ_INT(0, link(backup_one, backup_one_snapshot));

  ASSERT_NOT_NULL(insert_item(config.itemroot, "backup.e2e",
                              (VALUE_t){VALUE_int, {.i = 99}}));
  (void)lc_sys_backup(&ctx, NULL, caller);
  assert_bool_return(pop_stack(config.vm->stack), 1);
  char backup_two[sizeof(base_backup) + 3u];
  written = snprintf(backup_two, sizeof(backup_two), "%s_2", base_backup);
  ASSERT_TRUE(written > 0 && (size_t)written < sizeof(backup_two));
  loaded = load_itemstore(backup_two);
  ASSERT_NOT_NULL(loaded);
  ITEM_t *backup_two_value = find_item(loaded, "backup.e2e");
  ASSERT_NOT_NULL(backup_two_value);
  ASSERT_EQ_INT(99, backup_two_value->value.i);
  destroy_item(loaded);
  assert_file_bytes_equal(base_snapshot, base_backup,
                          "sys.backup preserves occupied target bytes");
  assert_file_bytes_equal(backup_one_snapshot, backup_one,
                          "sys.backup preserves prior backup bytes");
  ASSERT_EQ_INT(0, unlink(backup_one_snapshot));
  ASSERT_EQ_INT(0, unlink(base_snapshot));
  ASSERT_EQ_INT(0, unlink(backup_two));
  ASSERT_EQ_INT(0, unlink(backup_one));
  ASSERT_EQ_INT(0, unlink(base_backup));

  written = snprintf(race_pre_publish_path, sizeof(race_pre_publish_path),
                     "%s_%s", store_path, "20000101-000000");
  ASSERT_TRUE(written > 0 && (size_t)written < sizeof(race_pre_publish_path));
  race_pre_publish_hook_fired = false;
  race_pre_publish_path_matches = false;
  race_pre_publish_symlink_created = false;
  itemstore_set_pre_publish_hook_for_tests(race_pre_publish_hook);
  ASSERT_NOT_NULL(insert_item(config.itemroot, "backup.race",
                              (VALUE_t){VALUE_int, {.i = 77}}));
  (void)lc_sys_backup(&ctx, NULL, caller);
  assert_bool_return(pop_stack(config.vm->stack), 1);
  itemstore_set_pre_publish_hook_for_tests(NULL);
  ASSERT_TRUE(race_pre_publish_hook_fired);
  ASSERT_TRUE(race_pre_publish_path_matches);
  ASSERT_TRUE(race_pre_publish_symlink_created);
  char symlink_target[64];
  ssize_t symlink_length = readlink(race_pre_publish_path, symlink_target,
                                    sizeof(symlink_target) - 1u);
  ASSERT_EQ_INT((int)strlen("/nonexistent_race_target"), (int)symlink_length);
  symlink_target[symlink_length] = '\0';
  ASSERT_TRUE(strcmp(symlink_target, "/nonexistent_race_target") == 0);
  char race_backup[sizeof(race_pre_publish_path) + 3u];
  written = snprintf(race_backup, sizeof(race_backup), "%s_1",
                     race_pre_publish_path);
  ASSERT_TRUE(written > 0 && (size_t)written < sizeof(race_backup));
  loaded = load_itemstore(race_backup);
  ASSERT_NOT_NULL(loaded);
  ASSERT_NOT_NULL(find_item(loaded, "backup.race"));
  destroy_item(loaded);
  ASSERT_EQ_INT(0, unlink(race_backup));
  ASSERT_EQ_INT(0, unlink(race_pre_publish_path));
  lc_sys_backup_set_timestamp_for_tests(NULL);

  char missing_parent[128];
  ASSERT_EQ_INT(0, test_make_temp_path("sin-sys-save-missing",
                                      missing_parent,
                                      sizeof(missing_parent)));
  char failing_path[sizeof(missing_parent) + 16u];
  written = snprintf(failing_path, sizeof(failing_path), "%s/store",
                     missing_parent);
  ASSERT_TRUE(written > 0 && (size_t)written < sizeof(failing_path));
  ctx.itemstore_filename = failing_path;

  (void)lc_sys_save(&ctx, NULL, caller);
  assert_bool_return(pop_stack(config.vm->stack), 0);
  assert_persistence_error("sys.save", failing_path, "persistence.caller");

  (void)lc_sys_backup(&ctx, NULL, caller);
  assert_bool_return(pop_stack(config.vm->stack), 0);
  assert_persistence_error("sys.backup", failing_path,
                           "persistence.caller");
  ITEM_t *backup_failure_message = find_item(config.itemroot, "error.msg");
  ASSERT_NOT_NULL(backup_failure_message);
  const char *backup_target = strstr(backup_failure_message->value.s,
                                     failing_path);
  ASSERT_NOT_NULL(backup_target);
  ASSERT_EQ_INT('_', backup_target[strlen(failing_path)]);

  ctx.itemstore_filename = NULL;
  (void)lc_sys_save(&ctx, NULL, caller);
  assert_bool_return(pop_stack(config.vm->stack), 0);
  assert_persistence_error("sys.save", "<unconfigured>",
                           "persistence.caller");
  (void)lc_sys_backup(&ctx, NULL, caller);
  assert_bool_return(pop_stack(config.vm->stack), 0);
  assert_persistence_error("sys.backup", "<unconfigured>",
                           "persistence.caller");

  ctx.itemstore_filename = store_path;
  ctx.itemroot = NULL;
  (void)lc_sys_save(&ctx, NULL, caller);
  assert_bool_return(pop_stack(config.vm->stack), 0);
  (void)lc_sys_backup(&ctx, NULL, caller);
  assert_bool_return(pop_stack(config.vm->stack), 0);

  ctx.itemroot = config.itemroot;
  char invalid_runtime_path[128];
  ASSERT_EQ_INT(0, test_make_temp_path("sin-sys-save-invalid-runtime",
                                      invalid_runtime_path,
                                      sizeof(invalid_runtime_path)));
  ctx.itemstore_filename = invalid_runtime_path;
  uint8_t nextop_marker = 0;
  ctx.vm = NULL;
  ASSERT_TRUE(lc_sys_save(&ctx, &nextop_marker, caller) == &nextop_marker);
  assert_persistence_error("sys.save", invalid_runtime_path,
                           "persistence.caller");
  ASSERT_TRUE(lc_sys_backup(&ctx, &nextop_marker, caller) == &nextop_marker);
  assert_persistence_error("sys.backup", invalid_runtime_path,
                           "persistence.caller");
  ASSERT_TRUE(access(invalid_runtime_path, F_OK) != 0);

  VM_t stackless_vm = {0};
  ctx.vm = &stackless_vm;
  ASSERT_TRUE(lc_sys_save(&ctx, &nextop_marker, caller) == &nextop_marker);
  ASSERT_TRUE(lc_sys_backup(&ctx, &nextop_marker, caller) == &nextop_marker);
  ASSERT_TRUE(access(invalid_runtime_path, F_OK) != 0);

  ASSERT_TRUE(lc_sys_save(NULL, &nextop_marker, caller) == &nextop_marker);
  ASSERT_TRUE(lc_sys_backup(NULL, &nextop_marker, caller) == &nextop_marker);
  char invalid_backup_pattern[sizeof(invalid_runtime_path) + 4u];
  written = snprintf(invalid_backup_pattern, sizeof(invalid_backup_pattern),
                     "%s_*", invalid_runtime_path);
  ASSERT_TRUE(written > 0 && (size_t)written < sizeof(invalid_backup_pattern));
  glob_t invalid_backups = {0};
  ASSERT_EQ_INT(GLOB_NOMATCH,
                glob(invalid_backup_pattern, 0, NULL, &invalid_backups));
  globfree(&invalid_backups);

  itemstore_set_sync_hook_for_tests(NULL);
  itemstore_set_directory_sync_hook_for_tests(NULL);
  ASSERT_EQ_INT(0, unlink(store_path));
  teardown_libcall_runtime();
}

void test_sys_introspection_libcalls(void) {
  setup_libcall_runtime();

  ITEM_t *top = insert_halt_code(config.itemroot, "topcode");
  ITEM_t *nested = insert_halt_code(config.itemroot, "scope.runner");
  ASSERT_NOT_NULL(insert_item(config.itemroot, "scope.runner.relative",
                              (VALUE_t){VALUE_bool, {.i = 1}}));
  insert_halt_code(config.itemroot, "types.code");
  ASSERT_NOT_NULL(insert_item(config.itemroot, "types.nil", VALUE_NIL));
  ASSERT_NOT_NULL(insert_item(config.itemroot, "types.bool",
                              (VALUE_t){VALUE_bool, {.i = 1}}));
  ASSERT_NOT_NULL(insert_item(config.itemroot, "types.int",
                              (VALUE_t){VALUE_int, {.i = 42}}));
  ASSERT_NOT_NULL(insert_item(config.itemroot, "types.float",
                              (VALUE_t){VALUE_float, {.f = 1.25}}));
  ASSERT_NOT_NULL(insert_item(config.itemroot, "types.string",
                              (VALUE_t){VALUE_str, {.s = strdup("value")}}));

  RuntimeContext ctx;
  runtime_context_init(&ctx, config.vm);
  ctx.itemroot = config.itemroot;
  ctx.current_item = top;

  assert_string_return(call_sys_noarg(lc_sys_thisitem, &ctx), "topcode");
  VALUE_t result = call_sys_noarg(lc_sys_parentitem, &ctx);
  ASSERT_EQ_INT(VALUE_nil, result.type);

  ctx.current_item = nested;
  assert_string_return(call_sys_noarg(lc_sys_thisitem, &ctx), "scope.runner");
  assert_string_return(call_sys_noarg(lc_sys_parentitem, &ctx), "scope");

  ctx.current_item = NULL;
  result = call_sys_noarg(lc_sys_thisitem, &ctx);
  ASSERT_EQ_INT(VALUE_nil, result.type);
  result = call_sys_noarg(lc_sys_parentitem, &ctx);
  ASSERT_EQ_INT(VALUE_nil, result.type);
  ctx.current_item = nested;

  set_error_item(config.itemroot, ERR_RUNTIME_NOSUCHITEM,
                 "unrelated prior error", nested);
  assert_string_return(call_sys_noarg(lc_sys_thisitem, &ctx), "scope.runner");
  assert_string_return(call_sys_noarg(lc_sys_parentitem, &ctx), "scope");
  static const struct {
    const char *name;
    const char *type;
  } type_cases[] = {
    {"types.code", "code"},
    {"types.nil", "nil"},
    {"types.bool", "bool"},
    {"types.int", "int"},
    {"types.float", "float"},
    {"types.string", "string"},
    {".relative", "bool"},
  };
  for (size_t i = 0; i < sizeof(type_cases) / sizeof(type_cases[0]); i++) {
    result = call_sys_name(lc_sys_itemtype, &ctx,
        (VALUE_t){VALUE_str, {.s = strdup(type_cases[i].name)}});
    assert_string_return(result, type_cases[i].type);
  }

  result = call_sys_name(lc_sys_itemtype, &ctx,
      (VALUE_t){VALUE_str, {.s = strdup("types.missing")}});
  ASSERT_EQ_INT(VALUE_nil, result.type);
  result = call_sys_name(lc_sys_itemtype, &ctx,
      (VALUE_t){VALUE_str, {.s = strdup("invalid-name!")}});
  ASSERT_EQ_INT(VALUE_nil, result.type);
  ITEM_t *error = find_item(config.itemroot, "error");
  ASSERT_NOT_NULL(error);
  ASSERT_EQ_INT(ERR_RUNTIME_NOSUCHITEM, error->value.i);

  result = call_sys_name(lc_sys_childcount, &ctx,
      (VALUE_t){VALUE_str, {.s = strdup("types")}});
  ASSERT_EQ_INT(VALUE_int, result.type);
  ASSERT_EQ_INT(6, result.i);
  int64_t type_child_count = result.i;
  result = call_sys_name(lc_sys_childcount, &ctx,
      (VALUE_t){VALUE_str, {.s = strdup("types.nil")}});
  ASSERT_EQ_INT(VALUE_int, result.type);
  ASSERT_EQ_INT(0, result.i);
  result = call_sys_name(lc_sys_childcount, &ctx,
      (VALUE_t){VALUE_str, {.s = strdup(".relative")}});
  ASSERT_EQ_INT(VALUE_int, result.type);
  ASSERT_EQ_INT(0, result.i);
  result = call_sys_name(lc_sys_childcount, &ctx,
      (VALUE_t){VALUE_str, {.s = strdup("types.missing")}});
  ASSERT_EQ_INT(VALUE_nil, result.type);
  result = call_sys_name(lc_sys_childcount, &ctx,
      (VALUE_t){VALUE_str, {.s = strdup("invalid-name!")}});
  ASSERT_EQ_INT(VALUE_nil, result.type);
  error = find_item(config.itemroot, "error");
  ASSERT_NOT_NULL(error);
  ASSERT_EQ_INT(ERR_RUNTIME_NOSUCHITEM, error->value.i);

  int64_t enumerated_children = 0;
  for (int64_t i = 0; i < type_child_count; i++) {
    push_stack(config.vm->stack,
               (VALUE_t){VALUE_str, {.s = strdup("types")}});
    push_stack(config.vm->stack, (VALUE_t){VALUE_int, {.i = i}});
    (void)lc_sys_nthname(&ctx, NULL, NULL);
    result = pop_stack(config.vm->stack);
    ASSERT_EQ_INT(VALUE_str, result.type);
    enumerated_children++;
    value_free(&result);
  }
  ASSERT_EQ_INT(type_child_count, enumerated_children);
  push_stack(config.vm->stack,
             (VALUE_t){VALUE_str, {.s = strdup("types")}});
  push_stack(config.vm->stack,
             (VALUE_t){VALUE_int, {.i = type_child_count}});
  (void)lc_sys_nthname(&ctx, NULL, NULL);
  result = pop_stack(config.vm->stack);
  ASSERT_EQ_INT(VALUE_nil, result.type);

  result = call_sys_noarg(lc_sys_rootcount, &ctx);
  ASSERT_EQ_INT(VALUE_int, result.type);
  int64_t root_count = result.i;
  ASSERT_TRUE(root_count > 0);
  int64_t enumerated_roots = 0;
  for (int64_t i = 0; i < root_count; i++) {
    push_stack(config.vm->stack, (VALUE_t){VALUE_int, {.i = i}});
    (void)lc_sys_rootname(&ctx, NULL, NULL);
    result = pop_stack(config.vm->stack);
    ASSERT_EQ_INT(VALUE_str, result.type);
    enumerated_roots++;
    value_free(&result);
  }
  ASSERT_EQ_INT(root_count, enumerated_roots);
  push_stack(config.vm->stack, (VALUE_t){VALUE_int, {.i = root_count}});
  (void)lc_sys_rootname(&ctx, NULL, NULL);
  result = pop_stack(config.vm->stack);
  ASSERT_EQ_INT(VALUE_nil, result.type);

  assert_string_return(call_sys_noarg(lc_sys_version, &ctx), SINVERSION);

  uv_timeval64_t wall_before = {0};
  uv_timeval64_t wall_after = {0};
  ASSERT_EQ_INT(0, uv_gettimeofday(&wall_before));
  result = call_sys_noarg(lc_sys_now, &ctx);
  ASSERT_EQ_INT(0, uv_gettimeofday(&wall_after));
  ASSERT_EQ_INT(VALUE_int, result.type);
  int64_t wall_before_ms = wall_before.tv_sec * INT64_C(1000) +
                           wall_before.tv_usec / INT64_C(1000);
  int64_t wall_after_ms = wall_after.tv_sec * INT64_C(1000) +
                          wall_after.tv_usec / INT64_C(1000);
  int64_t wall_low = wall_before_ms < wall_after_ms
      ? wall_before_ms : wall_after_ms;
  int64_t wall_high = wall_before_ms > wall_after_ms
      ? wall_before_ms : wall_after_ms;
  ASSERT_TRUE(result.i >= wall_low - INT64_C(1000));
  ASSERT_TRUE(result.i <= wall_high + INT64_C(1000));

  uint64_t mono_before = uv_hrtime() / UINT64_C(1000000);
  VALUE_t monotime_first = call_sys_noarg(lc_sys_monotime, &ctx);
  VALUE_t monotime_second = call_sys_noarg(lc_sys_monotime, &ctx);
  uint64_t mono_after = uv_hrtime() / UINT64_C(1000000);
  ASSERT_EQ_INT(VALUE_int, monotime_first.type);
  ASSERT_EQ_INT(VALUE_int, monotime_second.type);
  ASSERT_TRUE(monotime_first.i >= 0);
  ASSERT_TRUE(monotime_second.i >= monotime_first.i);
  ASSERT_TRUE((uint64_t)monotime_first.i >= mono_before);
  ASSERT_TRUE((uint64_t)monotime_second.i <= mono_after);

  error = find_item(config.itemroot, "error");
  ASSERT_NOT_NULL(error);
  ASSERT_EQ_INT(ERR_RUNTIME_NOSUCHITEM, error->value.i);

  result = call_sys_name(lc_sys_itemtype, &ctx,
                         (VALUE_t){VALUE_int, {.i = 1}});
  ASSERT_EQ_INT(VALUE_nil, result.type);
  error = find_item(config.itemroot, "error");
  ASSERT_NOT_NULL(error);
  ASSERT_EQ_INT(ERR_RUNTIME_INVALIDARGS, error->value.i);
  ITEM_t *message = find_item(config.itemroot, "error.msg");
  ASSERT_NOT_NULL(message);
  ASSERT_EQ_INT(VALUE_str, message->value.type);
  ASSERT_TRUE(strstr(message->value.s, "sys.itemtype") != NULL);
  ITEM_t *provenance = find_item(config.itemroot, "error.item");
  ASSERT_NOT_NULL(provenance);
  ASSERT_EQ_INT(VALUE_str, provenance->value.type);
  ASSERT_TRUE(strcmp(provenance->value.s, "scope.runner") == 0);

  result = call_sys_name(lc_sys_childcount, &ctx,
                         (VALUE_t){VALUE_bool, {.i = 1}});
  ASSERT_EQ_INT(VALUE_nil, result.type);
  error = find_item(config.itemroot, "error");
  ASSERT_NOT_NULL(error);
  ASSERT_EQ_INT(ERR_RUNTIME_INVALIDARGS, error->value.i);
  message = find_item(config.itemroot, "error.msg");
  ASSERT_NOT_NULL(message);
  ASSERT_TRUE(strstr(message->value.s, "sys.childcount") != NULL);
  provenance = find_item(config.itemroot, "error.item");
  ASSERT_NOT_NULL(provenance);
  ASSERT_TRUE(strcmp(provenance->value.s, "scope.runner") == 0);

  ASSERT_EQ_INT(-1, config.vm->stack->current);
  teardown_libcall_runtime();
}

void test_sys_wall_milliseconds_boundaries(void) {
  const int64_t lower_second = INT64_C(-9223372036854775) - INT64_C(1);
  ASSERT_EQ_INT(INT64_MIN,
                lc_sys_wall_milliseconds(lower_second, INT64_C(191000)));
  ASSERT_EQ_INT(INT64_MIN,
                lc_sys_wall_milliseconds(lower_second, INT64_C(192000)));
  ASSERT_EQ_INT(INT64_MIN + INT64_C(1),
                lc_sys_wall_milliseconds(lower_second, INT64_C(193000)));
  ASSERT_EQ_INT(INT64_C(-9223372036854775001),
                lc_sys_wall_milliseconds(lower_second, INT64_C(999000)));
  ASSERT_EQ_INT(INT64_MIN,
                lc_sys_wall_milliseconds(lower_second - INT64_C(1),
                                         INT64_C(999000)));

  const int64_t upper_second = INT64_C(9223372036854775);
  ASSERT_EQ_INT(INT64_MAX - INT64_C(1),
                lc_sys_wall_milliseconds(upper_second, INT64_C(806000)));
  ASSERT_EQ_INT(INT64_MAX,
                lc_sys_wall_milliseconds(upper_second, INT64_C(807000)));
  ASSERT_EQ_INT(INT64_MAX,
                lc_sys_wall_milliseconds(upper_second, INT64_C(808000)));
  ASSERT_EQ_INT(INT64_MAX,
                lc_sys_wall_milliseconds(upper_second + INT64_C(1), 0));
}

void test_sys_caller_paramcount_libcalls(void) {
  setup_libcall_runtime();

  ITEM_t *caller_a = insert_halt_code(config.itemroot, "calls.a");
  ITEM_t *caller_b = insert_halt_code(config.itemroot, "calls.b");
  ITEM_t *callee_c = insert_halt_code(config.itemroot, "calls.c");
  ITEM_t *direct_invoker = insert_halt_code(config.itemroot, "calls.invoker");
  ITEM_t *param_context = insert_halt_code(config.itemroot,
                                           "params.scope.runner");
  ITEM_t *zero_params = insert_halt_code(config.itemroot,
                                         "params.scope.runner.zero");
  uint8_t *multiple_bytecode = malloc(3u);
  ASSERT_NOT_NULL(multiple_bytecode);
  multiple_bytecode[0] = 3;
  multiple_bytecode[1] = 3;
  multiple_bytecode[2] = (uint8_t)'h';
  ITEM_t *multiple_params = insert_code_item(
      config.itemroot, "params.scope.runner.multiple", 3u,
      multiple_bytecode);
  ASSERT_NOT_NULL(multiple_params);
  ASSERT_NOT_NULL(insert_item(config.itemroot, "params.value", VALUE_TRUE));
  ITEM_t *missing_bytecode = insert_code_item(
      config.itemroot, "params.missing_bytecode", 0, NULL);
  ASSERT_NOT_NULL(missing_bytecode);
  uint8_t *short_bytecode = malloc(1u);
  ASSERT_NOT_NULL(short_bytecode);
  short_bytecode[0] = 0;
  ITEM_t *short_header = insert_code_item(
      config.itemroot, "params.short_header", 1u, short_bytecode);
  ASSERT_NOT_NULL(short_header);

  RuntimeContext ctx;
  runtime_context_init(&ctx, config.vm);
  ctx.itemroot = config.itemroot;
  ctx.current_item = callee_c;
  ctx.invocation_callstack_floor = 0;

  VALUE_t result = call_sys_noarg(lc_sys_calleritem, &ctx);
  ASSERT_EQ_INT(VALUE_nil, result.type);
  uint8_t nextop_marker = 0;
  ASSERT_TRUE(lc_sys_calleritem(NULL, &nextop_marker, NULL) ==
              &nextop_marker);

  set_error_item(config.itemroot, ERR_RUNTIME_NOSUCHITEM,
                 "unrelated prior error", callee_c);
  config.vm->callstack->current = 0;
  config.vm->callstack->entry[0].item = caller_a;
  VALUE_t owned_first = call_sys_noarg(lc_sys_calleritem, &ctx);
  VALUE_t owned_second = call_sys_noarg(lc_sys_calleritem, &ctx);
  ASSERT_EQ_INT(VALUE_str, owned_first.type);
  ASSERT_EQ_INT(VALUE_str, owned_second.type);
  ASSERT_NOT_NULL(owned_first.s);
  ASSERT_NOT_NULL(owned_second.s);
  ASSERT_TRUE(strcmp(owned_first.s, "calls.a") == 0);
  ASSERT_TRUE(strcmp(owned_second.s, "calls.a") == 0);
  ASSERT_TRUE(owned_first.s != owned_second.s);
  ASSERT_TRUE(owned_first.s != caller_a->name);
  value_free(&owned_first);
  value_free(&owned_second);

  config.vm->callstack->current = 1;
  config.vm->callstack->entry[1].item = caller_b;
  assert_string_return(call_sys_noarg(lc_sys_calleritem, &ctx), "calls.b");
  config.vm->callstack->current = 0;
  assert_string_return(call_sys_noarg(lc_sys_calleritem, &ctx), "calls.a");
  ctx.invocation_caller_item = direct_invoker;
  ctx.invocation_callstack_floor = 1;
  assert_string_return(call_sys_noarg(lc_sys_calleritem, &ctx),
                       "calls.invoker");
  config.vm->callstack->current = -1;
  ITEM_t *error = find_item(config.itemroot, "error");
  ASSERT_NOT_NULL(error);
  ASSERT_EQ_INT(ERR_RUNTIME_NOSUCHITEM, error->value.i);

  ctx.current_item = param_context;
  result = call_sys_name(lc_sys_paramcount, &ctx,
      (VALUE_t){VALUE_str, {.s = strdup(".zero")}});
  ASSERT_EQ_INT(VALUE_int, result.type);
  ASSERT_EQ_INT(0, result.i);
  result = call_sys_name(lc_sys_paramcount, &ctx,
      (VALUE_t){VALUE_str, {.s = strdup("params.scope.runner.multiple")}});
  ASSERT_EQ_INT(VALUE_int, result.type);
  ASSERT_EQ_INT(3, result.i);
  ASSERT_EQ_INT(0, zero_params->bytecode[1]);

  static const char *const nil_names[] = {
    "invalid-name!", "params.missing", "params.value",
    "params.missing_bytecode", "params.short_header"
  };
  for (size_t i = 0; i < sizeof(nil_names) / sizeof(nil_names[0]); i++) {
    result = call_sys_name(lc_sys_paramcount, &ctx,
        (VALUE_t){VALUE_str, {.s = strdup(nil_names[i])}});
    ASSERT_EQ_INT(VALUE_nil, result.type);
    error = find_item(config.itemroot, "error");
    ASSERT_NOT_NULL(error);
    ASSERT_EQ_INT(ERR_RUNTIME_NOSUCHITEM, error->value.i);
  }

  result = call_sys_name(lc_sys_paramcount, &ctx,
                         (VALUE_t){VALUE_int, {.i = 1}});
  ASSERT_EQ_INT(VALUE_nil, result.type);
  error = find_item(config.itemroot, "error");
  ASSERT_NOT_NULL(error);
  ASSERT_EQ_INT(ERR_RUNTIME_INVALIDARGS, error->value.i);
  ITEM_t *message = find_item(config.itemroot, "error.msg");
  ASSERT_NOT_NULL(message);
  ASSERT_EQ_INT(VALUE_str, message->value.type);
  ASSERT_TRUE(strstr(message->value.s, "sys.paramcount") != NULL);
  ITEM_t *provenance = find_item(config.itemroot, "error.item");
  ASSERT_NOT_NULL(provenance);
  ASSERT_EQ_INT(VALUE_str, provenance->value.type);
  ASSERT_TRUE(strcmp(provenance->value.s, "params.scope.runner") == 0);

  ASSERT_EQ_INT(-1, config.vm->stack->current);
  ASSERT_EQ_INT(-1, config.vm->callstack->current);
  teardown_libcall_runtime();
}

static void overwrite_source_file(const char *filename, const void *bytes,
                                  size_t length) {
  FILE *file = fopen(filename, "wb");
  ASSERT_NOT_NULL(file);
  ASSERT_EQ_INT(length, fwrite(bytes, 1, length, file));
  ASSERT_EQ_INT(0, fclose(file));
}

void test_sys_source_libcall(void) {
  setup_libcall_runtime();

  char srcroot[] = "/tmp/sin-sys-source-XXXXXX";
  ASSERT_NOT_NULL(mkdtemp(srcroot));
  ITEM_t *runner = insert_halt_code(config.itemroot, "source.scope.runner");
  ITEM_t *target = insert_halt_code(config.itemroot,
                                    "source.scope.runner.target");
  ITEM_t *empty = insert_halt_code(config.itemroot,
                                   "source.scope.runner.empty");
  insert_halt_code(config.itemroot, "source.scope.runner.missing");
  ITEM_t *oversized = insert_halt_code(config.itemroot,
                                       "source.scope.runner.oversized");
  ITEM_t *embedded_nul = insert_halt_code(config.itemroot,
                                          "source.scope.runner.nul");
  ASSERT_NOT_NULL(insert_item(config.itemroot, "source.value", VALUE_TRUE));

  char exact_source[] = "target = code (\n  42;\n);\n";
  ASSERT_TRUE(save_itemsource_in_srcroot(target, exact_source, srcroot));
  char empty_source[] = "";
  ASSERT_TRUE(save_itemsource_in_srcroot(empty, empty_source, srcroot));
  char seed_source[] = "seed";
  ASSERT_TRUE(save_itemsource_in_srcroot(oversized, seed_source, srcroot));
  ASSERT_TRUE(save_itemsource_in_srcroot(embedded_nul, seed_source, srcroot));

  char *target_filename = get_itemfilename_in_srcroot(target, srcroot);
  char *empty_filename = get_itemfilename_in_srcroot(empty, srcroot);
  char *oversized_filename = get_itemfilename_in_srcroot(oversized, srcroot);
  char *nul_filename = get_itemfilename_in_srcroot(embedded_nul, srcroot);
  ASSERT_NOT_NULL(target_filename);
  ASSERT_NOT_NULL(empty_filename);
  ASSERT_NOT_NULL(oversized_filename);
  ASSERT_NOT_NULL(nul_filename);

  char *too_large = malloc(SIN_MAX_STRING_BYTES + 1u);
  ASSERT_NOT_NULL(too_large);
  memset(too_large, 'x', SIN_MAX_STRING_BYTES + 1u);
  overwrite_source_file(oversized_filename, too_large,
                        SIN_MAX_STRING_BYTES + 1u);
  free(too_large);
  const unsigned char nul_bytes[] = {'a', 0, 'b'};
  overwrite_source_file(nul_filename, nul_bytes, sizeof(nul_bytes));

  RuntimeContext ctx;
  runtime_context_init(&ctx, config.vm);
  ctx.itemroot = config.itemroot;
  ctx.current_item = runner;
  ctx.srcroot = srcroot;

  set_error_item(config.itemroot, ERR_RUNTIME_NOSUCHITEM,
                 "unrelated prior error", runner);
  VALUE_t owned_first = call_sys_name(lc_sys_source, &ctx,
      (VALUE_t){VALUE_str, {.s = strdup(".target")}});
  VALUE_t owned_second = call_sys_name(lc_sys_source, &ctx,
      (VALUE_t){VALUE_str,
                {.s = strdup("source.scope.runner.target")}});
  ASSERT_EQ_INT(VALUE_str, owned_first.type);
  ASSERT_EQ_INT(VALUE_str, owned_second.type);
  ASSERT_NOT_NULL(owned_first.s);
  ASSERT_NOT_NULL(owned_second.s);
  ASSERT_TRUE(strcmp(owned_first.s, exact_source) == 0);
  ASSERT_TRUE(strcmp(owned_second.s, exact_source) == 0);
  ASSERT_TRUE(owned_first.s != owned_second.s);
  value_free(&owned_first);
  value_free(&owned_second);

  VALUE_t result = call_sys_name(lc_sys_source, &ctx,
      (VALUE_t){VALUE_str, {.s = strdup(".empty")}});
  ASSERT_EQ_INT(VALUE_str, result.type);
  ASSERT_NOT_NULL(result.s);
  ASSERT_TRUE(result.s[0] == '\0');
  value_free(&result);

  static const char *const nil_names[] = {
    "invalid-name!", "source.absent", "source.value"
  };
  for (size_t i = 0; i < sizeof(nil_names) / sizeof(nil_names[0]); i++) {
    result = call_sys_name(lc_sys_source, &ctx,
        (VALUE_t){VALUE_str, {.s = strdup(nil_names[i])}});
    ASSERT_EQ_INT(VALUE_nil, result.type);
  }
  ITEM_t *error = find_item(config.itemroot, "error");
  ASSERT_NOT_NULL(error);
  ASSERT_EQ_INT(ERR_RUNTIME_NOSUCHITEM, error->value.i);

  result = call_sys_name(lc_sys_source, &ctx,
                         (VALUE_t){VALUE_int, {.i = 1}});
  ASSERT_EQ_INT(VALUE_nil, result.type);
  error = find_item(config.itemroot, "error");
  ASSERT_NOT_NULL(error);
  ASSERT_EQ_INT(ERR_RUNTIME_INVALIDARGS, error->value.i);
  ITEM_t *message = find_item(config.itemroot, "error.msg");
  ASSERT_NOT_NULL(message);
  ASSERT_EQ_INT(VALUE_str, message->value.type);
  ASSERT_TRUE(strstr(message->value.s, "sys.source") != NULL);
  ITEM_t *provenance = find_item(config.itemroot, "error.item");
  ASSERT_NOT_NULL(provenance);
  ASSERT_EQ_INT(VALUE_str, provenance->value.type);
  ASSERT_TRUE(strcmp(provenance->value.s, "source.scope.runner") == 0);

  result = call_sys_name(lc_sys_source, &ctx,
      (VALUE_t){VALUE_str, {.s = strdup(".missing")}});
  assert_string_return(result, "");
  error = find_item(config.itemroot, "error");
  ASSERT_NOT_NULL(error);
  ASSERT_EQ_INT(ERR_RUNTIME_SOURCE, error->value.i);
  message = find_item(config.itemroot, "error.msg");
  ASSERT_NOT_NULL(message);
  ASSERT_TRUE(strstr(message->value.s, "sys.source") != NULL);
  ASSERT_TRUE(strstr(message->value.s, "source.sin") != NULL);
  provenance = find_item(config.itemroot, "error.item");
  ASSERT_NOT_NULL(provenance);
  ASSERT_TRUE(strcmp(provenance->value.s, "source.scope.runner") == 0);

  ctx.srcroot = NULL;
  result = call_sys_name(lc_sys_source, &ctx,
      (VALUE_t){VALUE_str, {.s = strdup(".target")}});
  assert_string_return(result, "");
  error = find_item(config.itemroot, "error");
  ASSERT_NOT_NULL(error);
  ASSERT_EQ_INT(ERR_RUNTIME_SOURCE, error->value.i);

  ctx.srcroot = srcroot;
  result = call_sys_name(lc_sys_source, &ctx,
      (VALUE_t){VALUE_str, {.s = strdup(".oversized")}});
  assert_string_return(result, "");
  error = find_item(config.itemroot, "error");
  ASSERT_NOT_NULL(error);
  ASSERT_EQ_INT(ERR_RUNTIME_SOURCE, error->value.i);
  message = find_item(config.itemroot, "error.msg");
  ASSERT_NOT_NULL(message);
  ASSERT_TRUE(strstr(message->value.s, "exceeds") != NULL);

  result = call_sys_name(lc_sys_source, &ctx,
      (VALUE_t){VALUE_str, {.s = strdup(".nul")}});
  assert_string_return(result, "");
  error = find_item(config.itemroot, "error");
  ASSERT_NOT_NULL(error);
  ASSERT_EQ_INT(ERR_RUNTIME_SOURCE, error->value.i);
  message = find_item(config.itemroot, "error.msg");
  ASSERT_NOT_NULL(message);
  ASSERT_TRUE(strstr(message->value.s, "NUL") != NULL);

  ASSERT_EQ_INT(-1, config.vm->stack->current);
  ASSERT_EQ_INT(-1, config.vm->callstack->current);

  ASSERT_EQ_INT(0, unlink(target_filename));
  ASSERT_EQ_INT(0, unlink(empty_filename));
  ASSERT_EQ_INT(0, unlink(oversized_filename));
  ASSERT_EQ_INT(0, unlink(nul_filename));
  free(target_filename);
  free(empty_filename);
  free(oversized_filename);
  free(nul_filename);
  static const char *const leaf_dirs[] = {"target", "empty", "oversized",
                                          "nul"};
  char cleanup_path[512];
  for (size_t i = 0; i < sizeof(leaf_dirs) / sizeof(leaf_dirs[0]); i++) {
    int written = snprintf(cleanup_path, sizeof(cleanup_path),
        "%s/source/scope/runner/%s", srcroot, leaf_dirs[i]);
    ASSERT_TRUE(written > 0 && (size_t)written < sizeof(cleanup_path));
    ASSERT_EQ_INT(0, rmdir(cleanup_path));
  }
  int written = snprintf(cleanup_path, sizeof(cleanup_path),
                         "%s/source/scope/runner", srcroot);
  ASSERT_TRUE(written > 0 && (size_t)written < sizeof(cleanup_path));
  ASSERT_EQ_INT(0, rmdir(cleanup_path));
  written = snprintf(cleanup_path, sizeof(cleanup_path), "%s/source/scope",
                     srcroot);
  ASSERT_TRUE(written > 0 && (size_t)written < sizeof(cleanup_path));
  ASSERT_EQ_INT(0, rmdir(cleanup_path));
  written = snprintf(cleanup_path, sizeof(cleanup_path), "%s/source",
                     srcroot);
  ASSERT_TRUE(written > 0 && (size_t)written < sizeof(cleanup_path));
  ASSERT_EQ_INT(0, rmdir(cleanup_path));
  ASSERT_EQ_INT(0, rmdir(srcroot));

  teardown_libcall_runtime();
}
