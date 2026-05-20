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
  ASSERT_EQ_INT(-1, config.vm->stack->current);
  ASSERT_EQ_INT(-1, config.vm->callstack->current);

  destroy_vm(config.vm);
  destroy_item(config.itemroot);
}
