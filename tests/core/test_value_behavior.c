#include <stdint.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "config.h"
#include "error.h"
#include "interpret.h"
#include "item.h"
#include "compiler/compdiag.h"
#include "test_assert.h"
#include "value.h"
#include "vm.h"

extern CONFIG_t config;

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
  memset(&config, 0, sizeof(config));
}

static void emit_i64(uint8_t *code, size_t *pos, int64_t value) {
  memcpy(code + *pos, &value, sizeof(value));
  *pos += sizeof(value);
}

static void emit_u64(uint8_t *code, size_t *pos, uint64_t value) {
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

static VALUE_t run_interpret(ITEM_t *item) {
  RuntimeContext ctx;
  runtime_context_init(&ctx, config.vm);
  ctx.itemroot = config.itemroot;
  ctx.strict_validation = config.strict_validation;
  return interpret(&ctx, item);
}

static VALUE_t run_code(const char *name, const uint8_t *template_code, size_t len) {
  uint8_t *bytecode = malloc(len);
  ASSERT_NOT_NULL(bytecode);
  memcpy(bytecode, template_code, len);
  ITEM_t *code = insert_code_item(config.itemroot, name, (uint32_t)len, bytecode);
  ASSERT_NOT_NULL(code);
  return run_interpret(code);
}


static void assert_truncated_bytecode_for_opcode(const char *name, uint8_t opcode, const char *opname) {
  uint8_t code[] = {0, 0, opcode};
  VALUE_t result = run_code(name, code, sizeof(code));
  ASSERT_EQ_INT(VALUE_nil, result.type);

  ITEM_t *err = find_item(config.itemroot, "error");
  ASSERT_NOT_NULL(err);
  ASSERT_EQ_INT(ERR_RUNTIME_TRUNCATED, err->value.i);

  ITEM_t *msg = find_item(config.itemroot, "error.msg");
  ASSERT_NOT_NULL(msg);
  ASSERT_EQ_INT(VALUE_str, msg->value.type);
  ASSERT_TRUE(strstr(msg->value.s, opname) != NULL);
  ASSERT_TRUE(strstr(msg->value.s, "truncated bytecode read") != NULL);
}

static VALUE_t run_float_unary(uint64_t bits, uint8_t op, const char *name) {
  uint8_t code[32] = {0};
  size_t pos = 0;
  code[pos++] = 0;
  code[pos++] = 0;
  code[pos++] = 'P'; emit_u64(code, &pos, bits);
  code[pos++] = op;
  code[pos++] = 'h';
  return run_code(name, code, pos);
}

static VALUE_t run_float_binary(uint64_t lhs, uint64_t rhs, uint8_t op, const char *name) {
  uint8_t code[64] = {0};
  size_t pos = 0;
  code[pos++] = 0;
  code[pos++] = 0;
  code[pos++] = 'P'; emit_u64(code, &pos, lhs);
  code[pos++] = 'P'; emit_u64(code, &pos, rhs);
  code[pos++] = op;
  code[pos++] = 'h';
  return run_code(name, code, pos);
}



void test_error_item_preserves_compiler_diagnostic_fields(void) {
  setup_runtime();

  CompilerDiagnostic diag;
  compiler_diag_init(&diag);
  compiler_diag_set(&diag, ERR_COMP_UNKNOWNCHAR, DIAG_PHASE_PARSE,
                    "parse: unexpected character");
  compiler_diag_set_source_name(&diag, "example.sin");
  compiler_diag_set_location(&diag, 7, 3, 1);
  compiler_diag_set_excerpt(&diag, "bad ^ token");

  set_compiler_error_item(&diag);

  ITEM_t *err = find_item(config.itemroot, "error");
  ASSERT_NOT_NULL(err);
  ASSERT_EQ_INT(VALUE_int, err->value.type);
  ASSERT_EQ_INT(ERR_COMP_UNKNOWNCHAR, err->value.i);

  ITEM_t *msg = find_item(config.itemroot, "error.msg");
  ASSERT_NOT_NULL(msg);
  ASSERT_EQ_INT(VALUE_str, msg->value.type);
  ASSERT_TRUE(strstr(msg->value.s, "SIN-PARSE-0005") != NULL);
  ASSERT_TRUE(strstr(msg->value.s, "stage=PARSE") != NULL);
  ASSERT_TRUE(strstr(msg->value.s, "file=example.sin") != NULL);
  ASSERT_TRUE(strstr(msg->value.s, "line=7") != NULL);
  ASSERT_TRUE(strstr(msg->value.s, "column=3") != NULL);
  ASSERT_TRUE(strstr(msg->value.s, "message=parse: unexpected character") != NULL);
  ASSERT_TRUE(strstr(msg->value.s, "excerpt=bad ^ token") != NULL);

  ITEM_t *code = find_item(config.itemroot, "error.code");
  ASSERT_NOT_NULL(code);
  ASSERT_EQ_INT(VALUE_str, code->value.type);
  ASSERT_TRUE(strcmp(code->value.s, "SIN-PARSE-0005") == 0);

  ITEM_t *stage = find_item(config.itemroot, "error.stage");
  ASSERT_NOT_NULL(stage);
  ASSERT_EQ_INT(VALUE_str, stage->value.type);
  ASSERT_TRUE(strcmp(stage->value.s, "PARSE") == 0);

  ITEM_t *file = find_item(config.itemroot, "error.file");
  ASSERT_NOT_NULL(file);
  ASSERT_EQ_INT(VALUE_str, file->value.type);
  ASSERT_TRUE(strcmp(file->value.s, "example.sin") == 0);

  ITEM_t *line = find_item(config.itemroot, "error.line");
  ASSERT_NOT_NULL(line);
  ASSERT_EQ_INT(VALUE_int, line->value.type);
  ASSERT_EQ_INT(7, line->value.i);

  ITEM_t *column = find_item(config.itemroot, "error.column");
  ASSERT_NOT_NULL(column);
  ASSERT_EQ_INT(VALUE_int, column->value.type);
  ASSERT_EQ_INT(3, column->value.i);

  ITEM_t *excerpt = find_item(config.itemroot, "error.excerpt");
  ASSERT_NOT_NULL(excerpt);
  ASSERT_EQ_INT(VALUE_str, excerpt->value.type);
  ASSERT_TRUE(strcmp(excerpt->value.s, "bad ^ token") == 0);

  compiler_diag_reset(&diag);
  teardown_runtime();
}

void test_value_ieee754_environment_contract(void) {
  volatile double one = 1.0;
  volatile double positive_zero = 0.0;
  volatile double negative_zero = -0.0;

  double infinity = one / positive_zero;
  double nan = positive_zero / positive_zero;

  ASSERT_TRUE(isinf(infinity));
  ASSERT_TRUE(!signbit(infinity));
  ASSERT_TRUE(isnan(nan));
  ASSERT_TRUE(signbit(negative_zero));
  ASSERT_TRUE(!signbit(positive_zero));
  ASSERT_TRUE(positive_zero == negative_zero);
  ASSERT_TRUE(value_float_to_bits(positive_zero) == UINT64_C(0x0000000000000000));
  ASSERT_TRUE(value_float_to_bits(negative_zero) == UINT64_C(0x8000000000000000));
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



void test_value_integer_overflow_contract(void) {
  VALUE_t max = {VALUE_int, {.i = INT64_MAX}};
  VALUE_t min = {VALUE_int, {.i = INT64_MIN}};
  VALUE_t one = {VALUE_int, {.i = 1}};
  VALUE_t minus_one = {VALUE_int, {.i = -1}};
  VALUE_t large = {VALUE_int, {.i = INT64_C(3037000500)}};
  VALUE_t zero = {VALUE_int, {.i = 0}};
  VALUE_t result = VALUE_ZERO;

  ASSERT_TRUE(!value_add(&max, &one, &result));
  ASSERT_EQ_INT(VALUE_nil, result.type);
  ASSERT_TRUE(!value_sub(&min, &one, &result));
  ASSERT_EQ_INT(VALUE_nil, result.type);
  ASSERT_TRUE(!value_mul(&large, &large, &result));
  ASSERT_EQ_INT(VALUE_nil, result.type);
  ASSERT_TRUE(!value_div(&min, &minus_one, &result));
  ASSERT_EQ_INT(VALUE_nil, result.type);
  ASSERT_TRUE(value_div(&one, &zero, &result));
  ASSERT_EQ_INT(VALUE_int, result.type);
  ASSERT_EQ_INT(0, result.i);

  VALUE_t neg = min;
  ASSERT_TRUE(!value_neg(&neg));
  ASSERT_EQ_INT(VALUE_nil, neg.type);

  setup_runtime();
  uint8_t code[96] = {0};
  size_t pos = 0;
  code[pos++] = 0;
  code[pos++] = 0;
  code[pos++] = 'p'; emit_i64(code, &pos, INT64_MAX);
  code[pos++] = 'p'; emit_i64(code, &pos, 1);
  code[pos++] = 'a';
  code[pos++] = 'h';
  result = run_code("test.int_overflow_add", code, pos);
  ASSERT_EQ_INT(VALUE_nil, result.type);
  value_free(&result);

  pos = 0;
  code[pos++] = 0;
  code[pos++] = 0;
  code[pos++] = 'p'; emit_i64(code, &pos, INT64_MIN);
  code[pos++] = 'n';
  code[pos++] = 'h';
  result = run_code("test.int_overflow_neg", code, pos);
  ASSERT_EQ_INT(VALUE_nil, result.type);
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

void test_value_push_float_interprets_binary64_payloads(void) {
  setup_runtime();

  const uint64_t expected_bits[] = {
      UINT64_C(0x3ff0000000000000), /* 1.0 */
      UINT64_C(0x8000000000000000), /* -0.0 */
      UINT64_C(0x7ff0000000000000), /* +inf */
      UINT64_C(0x7ff8000000000042), /* quiet NaN */
      UINT64_C(0xc006000000000000), /* -2.75 */
  };

  for (size_t i = 0; i < sizeof(expected_bits) / sizeof(expected_bits[0]); i++) {
    uint8_t code[16] = {0};
    size_t pos = 0;
    code[pos++] = 0;
    code[pos++] = 0;
    code[pos++] = 'P';
    emit_u64(code, &pos, expected_bits[i]);
    code[pos++] = 'h';

    VALUE_t result = run_code("test.value_push_float_binary64", code, pos);
    ASSERT_EQ_INT(VALUE_float, result.type);
    ASSERT_TRUE(value_float_to_bits(result.f) == expected_bits[i]);
    value_free(&result);
  }

  teardown_runtime();
}


void test_value_float_arithmetic_helpers(void) {
  VALUE_t int_value = {VALUE_int, {.i = 2}};
  VALUE_t float_value = {VALUE_float, {.f = 0.5}};
  VALUE_t huge = {VALUE_float, {.f = value_float_from_bits(UINT64_C(0x7fefffffffffffff))}};
  VALUE_t tiny = {VALUE_float, {.f = value_float_from_bits(UINT64_C(0x0010000000000000))}};
  VALUE_t nan = {VALUE_float, {.f = value_float_from_bits(UINT64_C(0x7ff8000000000042))}};
  VALUE_t one = {VALUE_float, {.f = 1.0}};
  VALUE_t zero = {VALUE_float, {.f = 0.0}};
  VALUE_t negative_zero = {VALUE_float, {.f = -0.0}};
  VALUE_t result = VALUE_NIL;

  ASSERT_TRUE(value_is_numeric(&float_value));
  ASSERT_EQ_INT(VALUE_NUMERIC_FLOAT, value_numeric_kind(&float_value));

  ASSERT_TRUE(value_add(&int_value, &float_value, &result));
  ASSERT_EQ_INT(VALUE_float, result.type);
  ASSERT_TRUE(result.f == 2.5);
  ASSERT_TRUE(value_add(&float_value, &int_value, &result));
  ASSERT_EQ_INT(VALUE_float, result.type);
  ASSERT_TRUE(result.f == 2.5);
  ASSERT_TRUE(value_sub(&float_value, &int_value, &result));
  ASSERT_EQ_INT(VALUE_float, result.type);
  ASSERT_TRUE(result.f == -1.5);
  ASSERT_TRUE(value_mul(&int_value, &float_value, &result));
  ASSERT_EQ_INT(VALUE_float, result.type);
  ASSERT_TRUE(result.f == 1.0);

  ASSERT_TRUE(value_div(&one, &zero, &result));
  ASSERT_EQ_INT(VALUE_float, result.type);
  ASSERT_TRUE(value_float_to_bits(result.f) == UINT64_C(0x7ff0000000000000));
  ASSERT_TRUE(value_div(&one, &negative_zero, &result));
  ASSERT_EQ_INT(VALUE_float, result.type);
  ASSERT_TRUE(value_float_to_bits(result.f) == UINT64_C(0xfff0000000000000));
  ASSERT_TRUE(value_div(&zero, &zero, &result));
  ASSERT_EQ_INT(VALUE_float, result.type);
  ASSERT_TRUE((value_float_to_bits(result.f) & UINT64_C(0x7ff0000000000000)) == UINT64_C(0x7ff0000000000000));
  ASSERT_TRUE((value_float_to_bits(result.f) & UINT64_C(0x000fffffffffffff)) != 0);
  ASSERT_TRUE(value_add(&huge, &huge, &result));
  ASSERT_EQ_INT(VALUE_float, result.type);
  ASSERT_TRUE(value_float_to_bits(result.f) == UINT64_C(0x7ff0000000000000));
  ASSERT_TRUE(value_mul(&tiny, &tiny, &result));
  ASSERT_EQ_INT(VALUE_float, result.type);
  ASSERT_TRUE(value_float_to_bits(result.f) == UINT64_C(0x0000000000000000));
  ASSERT_TRUE(value_add(&nan, &one, &result));
  ASSERT_EQ_INT(VALUE_float, result.type);
  ASSERT_TRUE((value_float_to_bits(result.f) & UINT64_C(0x7ff0000000000000)) == UINT64_C(0x7ff0000000000000));
  ASSERT_TRUE((value_float_to_bits(result.f) & UINT64_C(0x000fffffffffffff)) != 0);

  ASSERT_TRUE(value_neg(&zero));
  ASSERT_EQ_INT(VALUE_float, zero.type);
  ASSERT_TRUE(value_float_to_bits(zero.f) == UINT64_C(0x8000000000000000));
}

void test_value_float_arithmetic_interpreter_bytecode(void) {
  setup_runtime();

  uint8_t code[96] = {0};
  size_t pos = 0;
  code[pos++] = 0;
  code[pos++] = 0;
  code[pos++] = 'p'; emit_i64(code, &pos, 2);
  code[pos++] = 'P'; emit_u64(code, &pos, UINT64_C(0x3fe0000000000000)); /* 0.5 */
  code[pos++] = 'a';
  code[pos++] = 'P'; emit_u64(code, &pos, UINT64_C(0x4008000000000000)); /* 3.0 */
  code[pos++] = 'm';
  code[pos++] = 'P'; emit_u64(code, &pos, UINT64_C(0x3ff0000000000000)); /* 1.0 */
  code[pos++] = 's';
  code[pos++] = 'P'; emit_u64(code, &pos, UINT64_C(0x0000000000000000)); /* +0.0 */
  code[pos++] = 'd';
  code[pos++] = 'h';

  VALUE_t result = run_code("test.value_float_arithmetic_bytecode", code, pos);
  ASSERT_EQ_INT(VALUE_float, result.type);
  ASSERT_TRUE(value_float_to_bits(result.f) == UINT64_C(0x7ff0000000000000));
  value_free(&result);

  pos = 0;
  code[pos++] = 0;
  code[pos++] = 0;
  code[pos++] = 'P'; emit_u64(code, &pos, UINT64_C(0x0000000000000000)); /* +0.0 */
  code[pos++] = 'n';
  code[pos++] = 'h';

  result = run_code("test.value_float_neg_zero_bytecode", code, pos);
  ASSERT_EQ_INT(VALUE_float, result.type);
  ASSERT_TRUE(value_float_to_bits(result.f) == UINT64_C(0x8000000000000000));
  value_free(&result);

  result = run_float_binary(UINT64_C(0x0010000000000000), UINT64_C(0x0000000000000001), 'a',
                            "test.float_subnorm_add");
  ASSERT_EQ_INT(VALUE_float, result.type);
  ASSERT_TRUE(value_float_to_bits(result.f) == UINT64_C(0x0010000000000001));
  value_free(&result);

  result = run_float_binary(UINT64_C(0x7ff8000000000042), UINT64_C(0x3ff0000000000000), 'a',
                            "test.float_nan_prop");
  ASSERT_EQ_INT(VALUE_float, result.type);
  ASSERT_TRUE((value_float_to_bits(result.f) & UINT64_C(0x7ff0000000000000)) == UINT64_C(0x7ff0000000000000));
  ASSERT_TRUE((value_float_to_bits(result.f) & UINT64_C(0x000fffffffffffff)) != 0);
  value_free(&result);

  pos = 0;
  code[pos++] = 1;
  code[pos++] = 0;
  code[pos++] = 'P'; emit_u64(code, &pos, UINT64_C(0x8000000000000000)); /* -0.0 */
  code[pos++] = 'c'; code[pos++] = 0;
  code[pos++] = 'e'; code[pos++] = 0;
  code[pos++] = 'h';

  result = run_code("test.float_store_neg0", code, pos);
  ASSERT_EQ_INT(VALUE_float, result.type);
  ASSERT_TRUE(value_float_to_bits(result.f) == UINT64_C(0x8000000000000000));
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

void test_value_float_item_fetch_preserves_bits(void) {
  setup_runtime();

  const uint64_t expected_bits[] = {
      UINT64_C(0x8000000000000000),
      UINT64_C(0x7ff8000000000042),
  };

  for (size_t i = 0; i < sizeof(expected_bits) / sizeof(expected_bits[0]); i++) {
    char item_name[32];
    snprintf(item_name, sizeof(item_name), "float.fetch.%c", (char)('a' + i));
    VALUE_t original = {VALUE_float, {.f = value_float_from_bits(expected_bits[i])}};
    ASSERT_NOT_NULL(insert_item(config.itemroot, item_name, original));

    uint8_t code[64] = {0};
    size_t pos = 0;
    code[pos++] = 0;
    code[pos++] = 0;
    code[pos++] = 'l'; emit_str(code, &pos, item_name);
    code[pos++] = 'F'; code[pos++] = 0; code[pos++] = 0;
    code[pos++] = 'h';

    char code_name[32];
    snprintf(code_name, sizeof(code_name), "float.runner.%c", (char)('a' + i));
    VALUE_t fetched = run_code(code_name, code, pos);
    ASSERT_EQ_INT(VALUE_float, fetched.type);
    ASSERT_TRUE(value_float_to_bits(fetched.f) == expected_bits[i]);
    value_free(&fetched);
  }

  teardown_runtime();
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


void test_value_comparison_float_ieee754_helpers(void) {
  VALUE_t one = {VALUE_int, {.i = 1}};
  VALUE_t one_point_zero = {VALUE_float, {.f = value_float_from_bits(UINT64_C(0x3ff0000000000000))}};
  VALUE_t one_point_five = {VALUE_float, {.f = value_float_from_bits(UINT64_C(0x3ff8000000000000))}};
  VALUE_t positive_zero = {VALUE_float, {.f = value_float_from_bits(UINT64_C(0x0000000000000000))}};
  VALUE_t negative_zero = {VALUE_float, {.f = value_float_from_bits(UINT64_C(0x8000000000000000))}};
  VALUE_t quiet_nan = {VALUE_float, {.f = value_float_from_bits(UINT64_C(0x7ff8000000000042))}};
  VALUE_t negative_nan = {VALUE_float, {.f = value_float_from_bits(UINT64_C(0xfff8000000000042))}};

  ASSERT_TRUE(value_equal(&one, &one_point_zero));
  ASSERT_TRUE(!value_not_equal(&one, &one_point_zero));
  ASSERT_TRUE(value_less_than(&one, &one_point_five));
  ASSERT_TRUE(value_less_equal(&one, &one_point_zero));
  ASSERT_TRUE(value_greater_than(&one_point_five, &one));
  ASSERT_TRUE(value_greater_equal(&one_point_zero, &one));

  ASSERT_TRUE(value_equal(&positive_zero, &negative_zero));
  ASSERT_TRUE(value_less_equal(&positive_zero, &negative_zero));
  ASSERT_TRUE(value_greater_equal(&positive_zero, &negative_zero));
  ASSERT_TRUE(value_float_to_bits(positive_zero.f) == UINT64_C(0x0000000000000000));
  ASSERT_TRUE(value_float_to_bits(negative_zero.f) == UINT64_C(0x8000000000000000));

  ASSERT_TRUE(!value_equal(&quiet_nan, &quiet_nan));
  ASSERT_TRUE(!value_equal(&quiet_nan, &negative_nan));
  ASSERT_TRUE(!value_equal(&quiet_nan, &one));
  ASSERT_TRUE(value_not_equal(&quiet_nan, &quiet_nan));
  ASSERT_TRUE(value_not_equal(&quiet_nan, &negative_nan));
  ASSERT_TRUE(value_not_equal(&quiet_nan, &one));
  ASSERT_TRUE(!value_less_than(&quiet_nan, &one));
  ASSERT_TRUE(!value_less_than(&one, &quiet_nan));
  ASSERT_TRUE(!value_less_equal(&quiet_nan, &quiet_nan));
  ASSERT_TRUE(!value_greater_than(&quiet_nan, &one));
  ASSERT_TRUE(!value_greater_than(&one, &quiet_nan));
  ASSERT_TRUE(!value_greater_equal(&quiet_nan, &negative_nan));

  setup_runtime();
  VALUE_t cmp = run_float_binary(UINT64_C(0x7ff8000000000042), UINT64_C(0x7ff8000000000042), 'o',
                                 "test.float_nan_eq");
  ASSERT_EQ_INT(VALUE_bool, cmp.type);
  ASSERT_TRUE(cmp.i == 0);
  value_free(&cmp);
  cmp = run_float_binary(UINT64_C(0x7ff8000000000042), UINT64_C(0x7ff8000000000042), 'q',
                         "test.float_nan_neq");
  ASSERT_EQ_INT(VALUE_bool, cmp.type);
  ASSERT_TRUE(cmp.i == 1);
  value_free(&cmp);
  cmp = run_float_binary(UINT64_C(0x0000000000000000), UINT64_C(0x8000000000000000), 'o',
                         "test.float_zero_eq");
  ASSERT_EQ_INT(VALUE_bool, cmp.type);
  ASSERT_TRUE(cmp.i == 1);
  value_free(&cmp);
  VALUE_t neg = run_float_unary(UINT64_C(0x8000000000000000), 'n',
                                "test.float_neg_neg0");
  ASSERT_EQ_INT(VALUE_float, neg.type);
  ASSERT_TRUE(value_float_to_bits(neg.f) == UINT64_C(0x0000000000000000));
  value_free(&neg);
  teardown_runtime();
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

void test_interpreter_truncated_single_byte_operands(void) {
  setup_runtime();
  assert_truncated_bytecode_for_opcode("test.truncated_getlocal", 'e', "OP_GETLOCAL");
  assert_truncated_bytecode_for_opcode("test.truncated_libcall_token", 'M', "OP_LIBCALL");
  teardown_runtime();
}

void test_strict_validation_runtime_opt_in(void) {
  setup_runtime();
  uint8_t code[] = {0, 0, 'e', 1, 'h'};
  uint8_t *bytecode = malloc(sizeof(code));
  ASSERT_NOT_NULL(bytecode);
  memcpy(bytecode, code, sizeof(code));
  ITEM_t *item = insert_code_item(config.itemroot, "test.strict_invalid_local", sizeof(code), bytecode);
  ASSERT_NOT_NULL(item);

  config.strict_validation = false;
  VALUE_t result = run_interpret(item);
  (void)result;
  ITEM_t *err = find_item(config.itemroot, "error");
  ASSERT_NOT_NULL(err);
  ASSERT_EQ_INT(VALUE_nil, err->value.type);

  teardown_runtime();
  setup_runtime();
  bytecode = malloc(sizeof(code));
  ASSERT_NOT_NULL(bytecode);
  memcpy(bytecode, code, sizeof(code));
  item = insert_code_item(config.itemroot, "test.strict_invalid_local", sizeof(code), bytecode);
  ASSERT_NOT_NULL(item);
  config.strict_validation = true;
  int32_t before_current = config.vm->stack->current;
  uint8_t before_locals = config.vm->stack->locals;
  uint8_t before_params = config.vm->stack->params;
  result = run_interpret(item);
  ASSERT_EQ_INT(VALUE_nil, result.type);
  ASSERT_TRUE(!item->inuse);
  ASSERT_EQ_INT(before_current, config.vm->stack->current);
  ASSERT_EQ_INT(before_locals, config.vm->stack->locals);
  ASSERT_EQ_INT(before_params, config.vm->stack->params);

  err = find_item(config.itemroot, "error");
  ASSERT_NOT_NULL(err);
  ASSERT_EQ_INT(ERR_RUNTIME_BYTECODE, err->value.i);
  ITEM_t *msg = find_item(config.itemroot, "error.msg");
  ASSERT_NOT_NULL(msg);
  ASSERT_EQ_INT(VALUE_str, msg->value.type);
  ASSERT_TRUE(strstr(msg->value.s, "test.strict_invalid_local") != NULL);
  ASSERT_TRUE(strstr(msg->value.s, "offset") != NULL);
  ASSERT_TRUE(strstr(msg->value.s, "local index") != NULL);
  teardown_runtime();
}

void test_strict_validation_rejects_null_bytecode(void) {
  setup_runtime();
  ITEM_t *item = make_item("nullcode", config.itemroot, ITEM_code, VALUE_NIL, NULL, 0);
  ASSERT_NOT_NULL(item);
  config.strict_validation = true;
  VALUE_t result = run_interpret(item);
  ASSERT_EQ_INT(VALUE_nil, result.type);
  ASSERT_TRUE(!item->inuse);
  ITEM_t *err = find_item(config.itemroot, "error");
  ASSERT_NOT_NULL(err);
  ASSERT_EQ_INT(ERR_RUNTIME_BYTECODE, err->value.i);
  ITEM_t *msg = find_item(config.itemroot, "error.msg");
  ASSERT_NOT_NULL(msg);
  ASSERT_TRUE(strstr(msg->value.s, "null bytecode") != NULL);
  teardown_runtime();
}
