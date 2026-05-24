#include <string.h>
#include <stdlib.h>

#include "libcall.h"
#include "config.h"
#include "error.h"
#include "interpret.h"
#include "item.h"
#include "test_assert.h"
#include "vm.h"
#include "memory.h"

#include "network.h"

uint8_t *lc_task_newgametask(uint8_t *nextop, ITEM_t *item);
uint8_t *lc_task_killtask(uint8_t *nextop, ITEM_t *item);
uint8_t *lc_net_write(uint8_t *nextop, ITEM_t *item);

extern LINE_t *line;
extern CONFIG_t config;

static void setup_libcall_runtime(void) {
  memset(&config, 0, sizeof(config));
  init_errmsg();
  config.itemroot = make_root_item("root");
  ASSERT_NOT_NULL(config.itemroot);
  config.vm = make_vm();
  ASSERT_NOT_NULL(config.vm);
}

static void teardown_libcall_runtime(void) {
  if (line) {
    free(line);
    line = NULL;
  }
  destroy_vm(config.vm);
  destroy_item(config.itemroot);
}


static uint8_t *test_noop_libcall(uint8_t *nextop, ITEM_t *item) {
  (void)item;
  return nextop;
}

void test_libcall_registry_roundtrip(void) {
  libcall_reset_registry_for_tests();
  ASSERT_TRUE(libcall_init_registry());
  ASSERT_TRUE(libcall_validate_registry());

  uint8_t token = 0;
  uint8_t args = 0;
  ASSERT_TRUE(libcall_lookup_token("sys", "log", &token, &args));
  ASSERT_EQ_INT(1, args);

  ASSERT_NOT_NULL(libcall_func_token(token));
  ASSERT_TRUE(libcall_func_token(255) == NULL);
}

void test_libcall_registry_init_failure_has_no_partial_state(void) {
  libcall_reset_registry_for_tests();

  alloc_test_fail_after(1);
  ASSERT_TRUE(!libcall_init_registry());

  uint8_t token = 0;
  uint8_t args = 0;
  ASSERT_TRUE(!libcall_lookup_token("sys", "log", &token, &args));
  ASSERT_TRUE(libcall_func_token(1) == NULL);

  alloc_test_fail_after(-1);
  ASSERT_TRUE(libcall_init_registry());
  ASSERT_TRUE(libcall_lookup_token("sys", "log", &token, &args));
  ASSERT_EQ_INT(1, args);
}

void test_libcall_registry_lifecycle_reinit_sequence(void) {
  uint8_t token = 0;
  uint8_t args = 0;

  libcall_reset_registry_for_tests();
  ASSERT_TRUE(libcall_init_registry());
  ASSERT_TRUE(libcall_lookup_token("sys", "log", &token, &args));
  ASSERT_EQ_INT(1, args);

  libcall_free_registry();
  ASSERT_TRUE(!libcall_lookup_token("doesnot", "exist", &token, &args));

  ASSERT_TRUE(libcall_init_registry());
  ASSERT_TRUE(libcall_lookup_token("sys", "log", &token, &args));
  ASSERT_EQ_INT(1, args);
}

void test_libcall_registry_repeated_teardown_is_safe(void) {
  libcall_reset_registry_for_tests();
  libcall_free_registry();
  libcall_free_registry();
  libcall_free_registry();

  ASSERT_TRUE(libcall_init_registry());
  ASSERT_TRUE(libcall_validate_registry());
}

void test_libcall_name_duplicate_detection(void) {
  const LIBCALL_t dup_calls[] = {
    {"sys", "log", 1, 1, 1, NULL},
    {"sys", "log", 1, 9, 1, NULL},
    {NULL, NULL, -1, -1, 0, NULL}
  };

  ASSERT_TRUE(!libcall_names_unique(dup_calls));
}

void test_missing_libcall_is_null_and_interpret_deterministic(void) {
  ASSERT_TRUE(libcall_func_token(255) == NULL);

  memset(&config, 0, sizeof(config));
  init_errmsg();
  config.itemroot = make_root_item("root");
  ASSERT_NOT_NULL(config.itemroot);
  config.vm = make_vm();
  ASSERT_NOT_NULL(config.vm);

  uint8_t template_bytecode[] = {
    0x00, 0x00,
    'M', 255,
    'h'
  };
  uint8_t *bytecode = malloc(sizeof(template_bytecode));
  ASSERT_NOT_NULL(bytecode);
  memcpy(bytecode, template_bytecode, sizeof(template_bytecode));

  ITEM_t *code = insert_code_item(config.itemroot, "test.missinglibcall",
                                  sizeof(template_bytecode), bytecode);
  ASSERT_NOT_NULL(code);

  VALUE_t v1 = interpret(code);
  VALUE_t v2 = interpret(code);
  ASSERT_EQ_INT(VALUE_nil, v1.type);
  ASSERT_EQ_INT(VALUE_nil, v2.type);
  ITEM_t *err_item = find_item(config.itemroot, "error");
  ASSERT_NOT_NULL(err_item);
  ASSERT_EQ_INT(VALUE_int, err_item->value.type);
  ASSERT_EQ_INT(ERR_RUNTIME_INVLIB, err_item->value.i);
  ITEM_t *err_msg = find_item(config.itemroot, "error.msg");
  ASSERT_NOT_NULL(err_msg);
  ASSERT_EQ_INT(VALUE_str, err_msg->value.type);
  ASSERT_TRUE(strstr(err_msg->value.s, "Unknown libcall token 255") != NULL);
  ASSERT_EQ_INT(-1, config.vm->stack->current);
  ASSERT_EQ_INT(-1, config.vm->callstack->current);

  destroy_vm(config.vm);
  destroy_item(config.itemroot);
}

void test_libcall_registry_self_check_invalid_entries(void) {
  const LIBCALL_t null_name[] = {{NULL, "x", 1, 0, 0, test_noop_libcall}, {NULL,NULL,-1,-1,0,NULL}};
  ASSERT_TRUE(!libcall_registry_self_check(null_name, false));

  const LIBCALL_t bad_args[] = {{"sys", "x", 1, 0, 255, test_noop_libcall}, {NULL,NULL,-1,-1,0,NULL}};
  ASSERT_TRUE(!libcall_registry_self_check(bad_args, false));

  const LIBCALL_t dup_num[] = {{"sys","a",1,1,0,test_noop_libcall},{"sys","b",1,1,0,test_noop_libcall},{NULL,NULL,-1,-1,0,NULL}};
  ASSERT_TRUE(!libcall_registry_self_check(dup_num, false));

  const LIBCALL_t dup_text[] = {{"sys","a",1,1,0,test_noop_libcall},{"sys","a",1,2,0,test_noop_libcall},{NULL,NULL,-1,-1,0,NULL}};
  ASSERT_TRUE(!libcall_registry_self_check(dup_text, false));

  const LIBCALL_t gap_lib[] = {{"sys","a",1,0,0,test_noop_libcall},{"net","b",3,0,0,test_noop_libcall},{NULL,NULL,-1,-1,0,NULL}};
  ASSERT_TRUE(!libcall_registry_self_check(gap_lib, false));
}

void test_libcall_invalid_arg_branches_return_contracts(void) {
  setup_libcall_runtime();

  VALUE_t bad_name = {VALUE_int, {.i = 7}};
  VALUE_t start = {VALUE_int, {.i = 1}};
  VALUE_t repeat = {VALUE_int, {.i = 1}};
  push_stack(config.vm->stack, bad_name);
  push_stack(config.vm->stack, start);
  push_stack(config.vm->stack, repeat);
  (void)lc_task_newgametask(NULL, config.itemroot);
  VALUE_t ret = pop_stack(config.vm->stack);
  ASSERT_EQ_INT(VALUE_nil, ret.type);
  ITEM_t *err = find_item(config.itemroot, "error");
  ASSERT_NOT_NULL(err);
  ASSERT_EQ_INT(ERR_RUNTIME_INVALIDARGS, err->value.i);

  VALUE_t bad_taskid = {VALUE_str, {.s = strdup("x")}};
  push_stack(config.vm->stack, bad_taskid);
  (void)lc_task_killtask(NULL, config.itemroot);
  ret = pop_stack(config.vm->stack);
  ASSERT_EQ_INT(VALUE_nil, ret.type);
  err = find_item(config.itemroot, "error");
  ASSERT_NOT_NULL(err);
  ASSERT_EQ_INT(ERR_RUNTIME_INVALIDARGS, err->value.i);

  config.maxconns = 1;
  line = calloc((size_t)config.maxconns, sizeof(LINE_t));
  ASSERT_NOT_NULL(line);
  VALUE_t bad_line = {VALUE_int, {.i = -1}};
  VALUE_t out = {VALUE_str, {.s = strdup("hello")}};
  push_stack(config.vm->stack, bad_line);
  push_stack(config.vm->stack, out);
  (void)lc_net_write(NULL, config.itemroot);
  ret = pop_stack(config.vm->stack);
  ASSERT_EQ_INT(VALUE_nil, ret.type);
  err = find_item(config.itemroot, "error");
  ASSERT_NOT_NULL(err);
  ASSERT_EQ_INT(ERR_RUNTIME_INVALIDARGS, err->value.i);

  teardown_libcall_runtime();
}

void test_net_write_ignores_disconnected_lines(void) {
  setup_libcall_runtime();

  config.maxconns = 2;
  line = calloc((size_t)config.maxconns, sizeof(LINE_t));
  ASSERT_NOT_NULL(line);
  line[1].status = LINE_empty;
  line[1].telnet = NULL;

  VALUE_t target_line = {VALUE_int, {.i = 1}};
  VALUE_t out = {VALUE_str, {.s = strdup("hello")}};
  push_stack(config.vm->stack, target_line);
  push_stack(config.vm->stack, out);

  (void)lc_net_write(NULL, config.itemroot);
  VALUE_t ret = pop_stack(config.vm->stack);
  ASSERT_EQ_INT(VALUE_nil, ret.type);

  teardown_libcall_runtime();
}

void test_net_write_ignores_non_writable_line_states(void) {
  setup_libcall_runtime();

  config.maxconns = 1;
  line = calloc((size_t)config.maxconns, sizeof(LINE_t));
  ASSERT_NOT_NULL(line);
  line[0].status = LINE_connecting;
  line[0].telnet = NULL;

  VALUE_t target_line = {VALUE_int, {.i = 0}};
  VALUE_t out = {VALUE_str, {.s = strdup("hello")}};
  push_stack(config.vm->stack, target_line);
  push_stack(config.vm->stack, out);

  (void)lc_net_write(NULL, config.itemroot);
  VALUE_t ret = pop_stack(config.vm->stack);
  ASSERT_EQ_INT(VALUE_nil, ret.type);

  teardown_libcall_runtime();
}
