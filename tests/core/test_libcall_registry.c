#include <string.h>
#include <stdlib.h>

#include "libcall.h"
#include "config.h"
#include "error.h"
#include "interpret.h"
#include "item.h"
#include "test_assert.h"
#include "vm.h"

extern CONFIG_t config;

static uint8_t *test_noop_libcall(uint8_t *nextop, ITEM_t *item) {
  (void)item;
  return nextop;
}

void test_libcall_registry_roundtrip(void) {
  ASSERT_TRUE(libcall_init_registry());
  ASSERT_TRUE(libcall_validate_registry());

  uint8_t lib = 0;
  uint8_t call = 0;
  uint8_t args = 0;
  ASSERT_TRUE(libcall_lookup("sys", "log", &lib, &call, &args));
  ASSERT_EQ_INT(1, lib);
  ASSERT_EQ_INT(1, call);
  ASSERT_EQ_INT(1, args);

  ASSERT_NOT_NULL(libcall_func(lib, call));
  ASSERT_TRUE(libcall_func(99, 99) == NULL);
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
  ASSERT_TRUE(libcall_func(99, 99) == NULL);

  memset(&config, 0, sizeof(config));
  init_errmsg();
  config.itemroot = make_root_item("root");
  ASSERT_NOT_NULL(config.itemroot);
  config.vm = make_vm();
  ASSERT_NOT_NULL(config.vm);

  uint8_t template_bytecode[] = {
    0x00, 0x00,
    'A', 99, 99,
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
  ASSERT_TRUE(strstr(err_msg->value.s, "Unknown libcall 99:99") != NULL);
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

  const LIBCALL_t gap_lib[] = {{"sys","a",1,0,0,test_noop_libcall},{"net","b",3,0,0,test_noop_libcall},{NULL,NULL,-1,-1,0,NULL}};
  ASSERT_TRUE(!libcall_registry_self_check(gap_lib, false));
}
