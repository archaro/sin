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
  VALUE_t left = {VALUE_int, {.i = 9}};
  VALUE_t right = {VALUE_int, {.i = 4}};
  VALUE_t result = VALUE_NIL;

  ASSERT_TRUE(value_is_numeric(&left));
  ASSERT_EQ_INT(VALUE_NUMERIC_INT, value_numeric_kind(&left));
  ASSERT_TRUE(value_add(&left, &right, &result));
  ASSERT_EQ_INT(VALUE_int, result.type);
  ASSERT_EQ_INT(13, result.i);
  ASSERT_TRUE(value_sub(&left, &right, &result));
  ASSERT_EQ_INT(5, result.i);
  ASSERT_TRUE(value_mul(&left, &right, &result));
  ASSERT_EQ_INT(36, result.i);
  ASSERT_TRUE(value_div(&left, &right, &result));
  ASSERT_EQ_INT(2, result.i);
  ASSERT_TRUE(value_neg(&result));
  ASSERT_EQ_INT(-2, result.i);

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

  result = run_code("test.value_int_arithmetic", code, pos);
  ASSERT_EQ_INT(VALUE_int, result.type);
  ASSERT_EQ_INT(3, result.i);
  value_free(&result);
  teardown_runtime();
}


void test_value_push_int_interprets_i64_immediates(void) {
  setup_runtime();

  const int64_t expected = INT64_C(-0x010203040506070);
  uint8_t code[16] = {0};
  size_t pos = 0;
  code[pos++] = 0;
  code[pos++] = 0;
  code[pos++] = 'p';
  emit_i64(code, &pos, expected);
  code[pos++] = 'h';

  VALUE_t result = run_code("test.value_push_int_i64", code, pos);
  ASSERT_EQ_INT(VALUE_int, result.type);
  ASSERT_TRUE(result.i == expected);
  value_free(&result);
  teardown_runtime();
}

void test_value_arithmetic_invalid_and_nil_operands(void) {
  VALUE_t int_value = {VALUE_int, {.i = 7}};
  VALUE_t nil_value = VALUE_NIL;
  VALUE_t str_value = {VALUE_str, {.s = strdup("x")}};
  VALUE_t bool_value = VALUE_TRUE;
  VALUE_t result = VALUE_NIL;

  ASSERT_TRUE(!value_is_numeric(&nil_value));
  ASSERT_EQ_INT(VALUE_NUMERIC_NONE, value_numeric_kind(&str_value));

  ASSERT_TRUE(value_add(&nil_value, &int_value, &result));
  ASSERT_EQ_INT(VALUE_int, result.type);
  ASSERT_EQ_INT(7, result.i);
  ASSERT_TRUE(value_add(&nil_value, &nil_value, &result));
  ASSERT_EQ_INT(0, result.i);

  ASSERT_TRUE(!value_add(&str_value, &int_value, &result));
  ASSERT_EQ_INT(VALUE_nil, result.type);
  ASSERT_TRUE(!value_sub(&nil_value, &int_value, &result));
  ASSERT_EQ_INT(VALUE_nil, result.type);
  ASSERT_TRUE(!value_mul(&str_value, &int_value, &result));
  ASSERT_EQ_INT(VALUE_nil, result.type);
  ASSERT_TRUE(!value_div(&str_value, &int_value, &result));
  ASSERT_EQ_INT(VALUE_int, result.type);
  ASSERT_EQ_INT(0, result.i);
  ASSERT_TRUE(!value_neg(&bool_value));
  ASSERT_EQ_INT(VALUE_bool, bool_value.type);
  ASSERT_EQ_INT(1, bool_value.i);

  value_free(&str_value);
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


void test_value_float_construction_copy_truthiness_cleanup(void) {
  VALUE_t zero = {.type = VALUE_float, .f = 0.0};
  VALUE_t negative_zero = {.type = VALUE_float, .f = -0.0};
  VALUE_t finite = {.type = VALUE_float, .f = 3.5};
  VALUE_t negative = {.type = VALUE_float, .f = -2.25};
  VALUE_t infinity = {.type = VALUE_float, .f = value_float_from_bits(0x7ff0000000000000ULL)};
  VALUE_t nan = {.type = VALUE_float, .f = value_float_from_bits(0x7ff8000000000000ULL)};

  ASSERT_EQ_INT(VALUE_float, finite.type);
  ASSERT_TRUE(value_float_to_bits(value_float_from_bits(0x400c000000000000ULL)) == 0x400c000000000000ULL);
  ASSERT_TRUE(!value_is_truthy(&zero));
  ASSERT_TRUE(!value_is_truthy(&negative_zero));
  ASSERT_TRUE(value_is_truthy(&finite));
  ASSERT_TRUE(value_is_truthy(&negative));
  ASSERT_TRUE(value_is_truthy(&infinity));
  ASSERT_TRUE(value_is_truthy(&nan));

  VALUE_t clone = value_clone(&finite);
  ASSERT_EQ_INT(VALUE_float, clone.type);
  ASSERT_TRUE(clone.f == finite.f);

  VALUE_t dst = VALUE_NIL;
  value_move(&dst, &clone);
  ASSERT_EQ_INT(VALUE_float, dst.type);
  ASSERT_TRUE(dst.f == finite.f);
  ASSERT_EQ_INT(VALUE_nil, clone.type);

  value_replace(&dst, negative);
  ASSERT_EQ_INT(VALUE_float, dst.type);
  ASSERT_TRUE(dst.f == negative.f);

  value_free(&dst);
  ASSERT_EQ_INT(VALUE_nil, dst.type);
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

void test_value_comparison_int_helpers(void) {
  VALUE_t four = {VALUE_int, {.i = 4}};
  VALUE_t also_four = {VALUE_int, {.i = 4}};
  VALUE_t nine = {VALUE_int, {.i = 9}};

  ASSERT_TRUE(value_equal(&four, &also_four));
  ASSERT_TRUE(!value_not_equal(&four, &also_four));
  ASSERT_TRUE(value_not_equal(&four, &nine));
  ASSERT_TRUE(value_less_than(&four, &nine));
  ASSERT_TRUE(value_less_equal(&four, &also_four));
  ASSERT_TRUE(value_greater_than(&nine, &four));
  ASSERT_TRUE(value_greater_equal(&also_four, &four));
  ASSERT_TRUE(!value_less_than(&nine, &four));
}

void test_value_comparison_bool_helpers(void) {
  VALUE_t false_value = VALUE_FALSE;
  VALUE_t true_value = VALUE_TRUE;
  VALUE_t another_true = VALUE_TRUE;

  ASSERT_TRUE(value_equal(&true_value, &another_true));
  ASSERT_TRUE(value_not_equal(&false_value, &true_value));
  ASSERT_TRUE(value_less_than(&false_value, &true_value));
  ASSERT_TRUE(value_less_equal(&true_value, &another_true));
  ASSERT_TRUE(value_greater_than(&true_value, &false_value));
  ASSERT_TRUE(value_greater_equal(&another_true, &true_value));
}

void test_value_comparison_string_helpers(void) {
  VALUE_t left = {VALUE_str, {.s = strdup("same")}};
  VALUE_t same = {VALUE_str, {.s = strdup("same")}};
  VALUE_t different = {VALUE_str, {.s = strdup("different")}};

  ASSERT_TRUE(value_equal(&left, &same));
  ASSERT_TRUE(!value_not_equal(&left, &same));
  ASSERT_TRUE(value_not_equal(&left, &different));

  value_free(&left);
  value_free(&same);
  value_free(&different);
}

void test_value_comparison_mismatched_type_equality_quirk(void) {
  VALUE_t int_value = {VALUE_int, {.i = 1}};
  VALUE_t bool_value = VALUE_TRUE;
  VALUE_t nil_value = VALUE_NIL;
  VALUE_t str_value = {VALUE_str, {.s = strdup("1")}};

  ASSERT_TRUE(!value_equal(&int_value, &bool_value));
  ASSERT_TRUE(value_not_equal(&int_value, &bool_value));
  ASSERT_TRUE(!value_equal(&nil_value, &str_value));
  ASSERT_TRUE(value_not_equal(&nil_value, &str_value));

  value_free(&str_value);
}

void test_value_comparison_unsupported_ordering_is_false(void) {
  VALUE_t left = {VALUE_str, {.s = strdup("a")}};
  VALUE_t right = {VALUE_str, {.s = strdup("b")}};
  VALUE_t int_value = {VALUE_int, {.i = 1}};
  VALUE_t nil_value = VALUE_NIL;

  ASSERT_TRUE(!value_less_than(&left, &right));
  ASSERT_TRUE(!value_less_equal(&left, &right));
  ASSERT_TRUE(!value_greater_than(&left, &right));
  ASSERT_TRUE(!value_greater_equal(&left, &right));
  ASSERT_TRUE(!value_less_than(&int_value, &nil_value));
  ASSERT_TRUE(!value_greater_equal(&nil_value, &int_value));

  value_free(&left);
  value_free(&right);
}
