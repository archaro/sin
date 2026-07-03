#include <stdio.h>
#include <string.h>

#include "config.h"
#include "error.h"
#include "item.h"
#include "libcall.h"
#include "memory.h"
#include "test_assert.h"
#include "value.h"
#include "vm.h"

CONFIG_t config;
uint8_t *lc_sys_compile(uint8_t *nextop, ITEM_t *item);

static VALUE_t vstr(const char *s) {
  VALUE_t v = {VALUE_str, {.s = strdup(s)}};
  return v;
}

static void setup_runtime(void) {
  memset(&config, 0, sizeof(config));
  init_errmsg();
  config.itemroot = make_root_item("root");
  ASSERT_NOT_NULL(config.itemroot);
  config.vm = make_vm();
  ASSERT_NOT_NULL(config.vm);
}

static void teardown_runtime(void) {
  destroy_vm(config.vm);
  destroy_item(config.itemroot);
}

static void assert_bool(VALUE_t v, int expected) {
  ASSERT_EQ_INT(VALUE_bool, v.type);
  ASSERT_EQ_INT(expected, v.i);
}

static ITEM_t *assert_string_item(const char *name, const char *contains) {
  ITEM_t *item = find_item(config.itemroot, name);
  ASSERT_NOT_NULL(item);
  ASSERT_EQ_INT(VALUE_str, item->value.type);
  ASSERT_NOT_NULL(item->value.s);
  if (contains) {
    ASSERT_TRUE(strstr(item->value.s, contains) != NULL);
  }
  return item;
}

static ITEM_t *assert_int_item(const char *name, int64_t expected) {
  ITEM_t *item = find_item(config.itemroot, name);
  ASSERT_NOT_NULL(item);
  ASSERT_EQ_INT(VALUE_int, item->value.type);
  ASSERT_EQ_INT(expected, item->value.i);
  return item;
}

static void assert_compile_success_bool(const char *source) {
  push_stack(config.vm->stack, vstr(source));
  (void)lc_sys_compile(NULL, config.itemroot);
  assert_bool(pop_stack(config.vm->stack), 1);
}

void test_sys_compile_libcall_runtime(void) {
  setup_runtime();

  assert_compile_success_bool("sys.log{\"ok\\n\"};");
  assert_compile_success_bool("@l = true;");
  assert_compile_success_bool("foo.bar = false;");
  assert_compile_success_bool("@l = false; @l == false;");
  assert_compile_success_bool("@l = false; if @l == false then sys.log{\"False\"}; endif;");
  assert_compile_success_bool("if 1 then sys.log{\"int truthy\"}; endif;");
  assert_compile_success_bool("if \"\" then sys.log{\"empty string truthy\"}; endif;");

  push_stack(config.vm->stack, vstr("sys.log{;"));
  (void)lc_sys_compile(NULL, config.itemroot);
  assert_bool(pop_stack(config.vm->stack), 0);
  ITEM_t *err_item = find_item(config.itemroot, "error");
  ITEM_t *msg_item = find_item(config.itemroot, "error.msg");
  ASSERT_NOT_NULL(err_item);
  ASSERT_NOT_NULL(msg_item);
  ASSERT_EQ_INT(VALUE_int, err_item->value.type);
  ASSERT_TRUE(err_item->value.i != ERR_NOERROR);
  ASSERT_EQ_INT(VALUE_str, msg_item->value.type);
  ASSERT_TRUE(msg_item->value.s != NULL && strlen(msg_item->value.s) > 0);
  ASSERT_TRUE(strstr(msg_item->value.s, "SIN-PARSE-") != NULL);
  ASSERT_TRUE(strstr(msg_item->value.s, "stage=PARSE") != NULL);
  ASSERT_TRUE(strstr(msg_item->value.s, "file=<memory>") != NULL);
  ASSERT_TRUE(strstr(msg_item->value.s, "line=") != NULL);
  ASSERT_TRUE(strstr(msg_item->value.s, "column=") != NULL);
  ASSERT_TRUE(strstr(msg_item->value.s, "message=") != NULL);
  ASSERT_TRUE(strstr(msg_item->value.s, "excerpt=sys.log{;") != NULL);
  assert_string_item("error.code", "SIN-PARSE-");
  assert_string_item("error.stage", "PARSE");
  assert_string_item("error.file", "<memory>");
  assert_int_item("error.line", 1);
  assert_int_item("error.column", 9);
  assert_string_item("error.excerpt", "sys.log{;");

  VALUE_t intarg = {VALUE_int, {.i = 42}};
  push_stack(config.vm->stack, intarg);
  (void)lc_sys_compile(NULL, config.itemroot);
  assert_bool(pop_stack(config.vm->stack), 0);
  err_item = find_item(config.itemroot, "error");
  ASSERT_NOT_NULL(err_item);
  ASSERT_EQ_INT(VALUE_int, err_item->value.type);
  ASSERT_EQ_INT(ERR_RUNTIME_INVALIDARGS, err_item->value.i);

  int32_t baseline = config.vm->stack->current;
  ASSERT_EQ_INT(-1, config.vm->callstack->current);
  for (int i = 0; i < 50; i++) {
    push_stack(config.vm->stack, vstr("sys.log{\"x\\n\"};"));
    (void)lc_sys_compile(NULL, config.itemroot);
    assert_bool(pop_stack(config.vm->stack), 1);
    ASSERT_EQ_INT(baseline, config.vm->stack->current);
    ASSERT_EQ_INT(-1, config.vm->callstack->current);
    ASSERT_TRUE(find_item(config.itemroot, "__sys_compile_tmp__") == NULL);
    char tmpname[64];
    snprintf(tmpname, sizeof(tmpname), "__sys_compile_tmp__%d", i + 1);
    ASSERT_TRUE(find_item(config.itemroot, tmpname) == NULL);
  }

  teardown_runtime();
}
