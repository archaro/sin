#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "config.h"
#include "interpret.h"
#include "item.h"
#include "test_assert.h"
#include "value.h"
#include "vm.h"

extern CONFIG_t config;

static void setup_runtime(void) {
  memset(&config, 0, sizeof(config));
  config.itemroot = make_root_item("root");
  ASSERT_NOT_NULL(config.itemroot);
  config.vm = make_vm();
  ASSERT_NOT_NULL(config.vm);
}

static void teardown_runtime(void) {
  destroy_vm(config.vm);
  destroy_item(config.itemroot);
  memset(&config, 0, sizeof(config));
}

static void emit_i64(uint8_t *code, size_t *pos, int64_t value) {
  memcpy(code + *pos, &value, sizeof(value));
  *pos += sizeof(value);
}

static void emit_str(uint8_t *code, size_t *pos, const char *value) {
  uint16_t len = (uint16_t)strlen(value);
  memcpy(code + *pos, &len, sizeof(len));
  *pos += sizeof(len);
  memcpy(code + *pos, value, len);
  *pos += len;
}

static VALUE_t run_code(const char *name, const uint8_t *template_code, size_t len) {
  uint8_t *bytecode = malloc(len);
  ASSERT_NOT_NULL(bytecode);
  memcpy(bytecode, template_code, len);
  ITEM_t *code = insert_code_item(config.itemroot, name, (uint32_t)len, bytecode);
  ASSERT_NOT_NULL(code);
  return interpret(code);
}

void test_value_integer_arithmetic_helpers(void) {
  setup_runtime();
  uint8_t code[64] = {0};
  size_t pos = 0;
  code[pos++] = 0;
  code[pos++] = 0;
  code[pos++] = 'p'; emit_i64(code, &pos, 9);
  code[pos++] = 'p'; emit_i64(code, &pos, 4);
  code[pos++] = 's';
  code[pos++] = 'p'; emit_i64(code, &pos, 3);
  code[pos++] = 'm';
  code[pos++] = 'p'; emit_i64(code, &pos, 5);
  code[pos++] = 'd';
  code[pos++] = 'h';

  VALUE_t result = run_code("test.value_int_arithmetic", code, pos);
  ASSERT_EQ_INT(VALUE_int, result.type);
  ASSERT_EQ_INT(3, result.i);
  value_free(&result);
  teardown_runtime();
}

void test_value_string_concat_helpers(void) {
  setup_runtime();
  uint8_t code[64] = {0};
  size_t pos = 0;
  code[pos++] = 0;
  code[pos++] = 0;
  code[pos++] = 'l'; emit_str(code, &pos, "hello");
  code[pos++] = 'l'; emit_str(code, &pos, " world");
  code[pos++] = 'a';
  code[pos++] = 'h';

  VALUE_t result = run_code("test.value_string_concat", code, pos);
  ASSERT_EQ_INT(VALUE_str, result.type);
  ASSERT_TRUE(strcmp(result.s, "hello world") == 0);
  value_free(&result);
  teardown_runtime();
}

void test_value_bool_nil_truthiness_helpers(void) {
  VALUE_t nil = VALUE_NIL;
  VALUE_t t = VALUE_TRUE;
  VALUE_t f = VALUE_FALSE;
  VALUE_t empty = {VALUE_str, {.s = strdup("")}};
  VALUE_t text = {VALUE_str, {.s = strdup("x")}};

  ASSERT_TRUE(!value_is_truthy(&nil));
  ASSERT_TRUE(value_is_truthy(&t));
  ASSERT_TRUE(!value_is_truthy(&f));
  ASSERT_TRUE(!value_is_truthy(&empty));
  ASSERT_TRUE(value_is_truthy(&text));

  value_free(&empty);
  value_free(&text);
}

void test_value_string_local_load_store_clones(void) {
  setup_runtime();
  uint8_t code[96] = {0};
  size_t pos = 0;
  code[pos++] = 1;
  code[pos++] = 0;
  code[pos++] = 'l'; emit_str(code, &pos, "foo");
  code[pos++] = 'c'; code[pos++] = 0;
  code[pos++] = 'e'; code[pos++] = 0;
  code[pos++] = 'l'; emit_str(code, &pos, "bar");
  code[pos++] = 'a';
  code[pos++] = 'h';

  VALUE_t result = run_code("test.value_local_string_clone", code, pos);
  ASSERT_EQ_INT(VALUE_str, result.type);
  ASSERT_TRUE(strcmp(result.s, "foobar") == 0);
  value_free(&result);
  teardown_runtime();
}
