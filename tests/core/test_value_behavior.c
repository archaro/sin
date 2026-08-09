#include "item.h"
#include <stdint.h>
#include "test_helpers.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "config.h"
#include "error.h"
#include "interpret.h"
#include "item.h"
#include "compiler/compdiag.h"
#include "runtime_decode.h"
#include "runtime_value.h"
#include "stack.h"
#include "string_limits.h"
#include "test_assert.h"
#include "value.h"
#include "itemref.h"
#include "memory.h"
#include "vm.h"

extern CONFIG_t config;
extern uint8_t *op_assigncodeitem(RuntimeContext *ctx, uint8_t *nextop,
                                  ITEM_t *item);
extern uint8_t *op_assignitem(RuntimeContext *ctx, uint8_t *nextop,
                              ITEM_t *item);
extern uint8_t *op_fetchitem(RuntimeContext *ctx, uint8_t *nextop,
                              ITEM_t *item);

static void setup_runtime(void) {
  memset(&config, 0, sizeof(config));
  init_errmsg();
  config.itemstore_ctx = itemstore_owner(make_root_item("root"));
  ASSERT_NOT_NULL(itemstore_root(config.itemstore_ctx));
  config.vm = make_vm();
  ASSERT_NOT_NULL(config.vm);
}

static void teardown_runtime(void) {
  destroy_vm(config.vm);
  destroy_item(itemstore_root(config.itemstore_ctx));
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
  ctx.itemstore = config.itemstore_ctx;
  ctx.strict_runtime_contracts = config.strict_runtime_contracts;
  return interpret(&ctx, item);
}

static VALUE_t run_code(const char *name, const uint8_t *template_code, size_t len) {
  uint8_t *bytecode = malloc(len);
  ASSERT_NOT_NULL(bytecode);
  memcpy(bytecode, template_code, len);
  ITEM_t *code = test_item_set_code(itemstore_root(config.itemstore_ctx), name, (uint32_t)len, bytecode);
  ASSERT_NOT_NULL(code);
  return run_interpret(code);
}

static void assert_error_code_and_detail(int expected_code, const char *needle) {
  ITEM_t *err = find_item(itemstore_root(config.itemstore_ctx), "error");
  ASSERT_NOT_NULL(err);
  ASSERT_EQ_INT(expected_code, item_value(err)->i);

  ITEM_t *msg = find_item(itemstore_root(config.itemstore_ctx), "error.msg");
  ASSERT_NOT_NULL(msg);
  ASSERT_EQ_INT(VALUE_str, item_value(msg)->type);
  ASSERT_TRUE(strstr(item_value(msg)->s, needle) != NULL);
}

void test_error_message_table_defines_active_errors(void) {
  init_errmsg();
  ASSERT_EQ_INT(28, ERR_RUNTIME_PERSISTENCE);
  ASSERT_EQ_INT(29, ERR_RUNTIME_SOURCE);

  int active_errors[] = {
      ERR_NOERROR,
      ERR_COMP_SYNTAX,
      ERR_COMP_TOOMANYLOCALS,
      ERR_COMP_LOCALBEFOREDEF,
      ERR_COMP_UNKNOWNCHAR,
      ERR_COMP_INUSE,
      ERR_COMP_TOOMANYPARAMS,
      ERR_COMP_TOOMANYARGS,
      ERR_COMP_UNKNOWN,
      ERR_RUNTIME_SIGUSR1,
      ERR_RUNTIME_INVALIDARGS,
      ERR_RUNTIME_NOSUCHITEM,
      ERR_RUNTIME_TRUNCATED,
      ERR_RUNTIME_INVLIB,
      ERR_RUNTIME_BYTECODE,
      ERR_RUNTIME_INVALIDITEM,
      ERR_RUNTIME_INTERNAL,
      ERR_RUNTIME_PERSISTENCE,
      ERR_RUNTIME_SOURCE,
      ERR_NETWORK_ERROR,
  };

  for (size_t i = 0; i < sizeof(active_errors) / sizeof(active_errors[0]); i++) {
    int code = active_errors[i];
    ASSERT_TRUE(code >= 0 && code < MAXERRORS);
    ASSERT_NOT_NULL(errmsg[code]);
    ASSERT_TRUE(errmsg[code][0] != '\0');
  }
}

static void assert_truncated_bytecode_for_opcode(const char *name, uint8_t opcode, const char *opname) {
  uint8_t code[] = {0, 0, opcode};
  VALUE_t result = run_code(name, code, sizeof(code));
  ASSERT_EQ_INT(VALUE_nil, result.type);

  ITEM_t *err = find_item(itemstore_root(config.itemstore_ctx), "error");
  ASSERT_NOT_NULL(err);
  ASSERT_EQ_INT(ERR_RUNTIME_BYTECODE, item_value(err)->i);

  ITEM_t *msg = find_item(itemstore_root(config.itemstore_ctx), "error.msg");
  ASSERT_NOT_NULL(msg);
  ASSERT_EQ_INT(VALUE_str, item_value(msg)->type);
  char expected[96];
  int written = snprintf(expected, sizeof(expected), "truncated %s", opname);
  ASSERT_TRUE(written > 0 && (size_t)written < sizeof(expected));
  ASSERT_TRUE(strstr(item_value(msg)->s, expected) != NULL);
}

void test_runtime_decode_requires_frame_bounds(void) {
  uint8_t code[] = {1, 2, 3};
  RuntimeDecodeStatus status = require_bytes(NULL, code, 1, "TEST");
  ASSERT_EQ_INT(RUNTIME_DECODE_TRUNCATED, status.code);
  ASSERT_TRUE(status.next == NULL);
  ASSERT_TRUE(strstr(status.detail, "TEST") != NULL);

  RuntimeDecoder decoder;
  runtime_decoder_init(&decoder, code, code + sizeof(code));
  status = require_bytes(&decoder, code, sizeof(code), "TEST");
  ASSERT_EQ_INT(RUNTIME_DECODE_OK, status.code);
  ASSERT_TRUE(status.next == code);

  status = require_bytes(&decoder, code + sizeof(code), 1, "TEST");
  ASSERT_EQ_INT(RUNTIME_DECODE_TRUNCATED, status.code);
  ASSERT_TRUE(status.next == NULL);
}

static VALUE_t run_float_unary(uint64_t bits, uint8_t op, const char *name) {
  uint8_t code[32] = {0};
  size_t pos = 0;
  code[pos++] = 0;
  code[pos++] = 0;
  code[pos++] = 'P'; emit_u64(code, &pos, bits);
  code[pos++] = op;
  code[pos++] = 'Q';
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
  code[pos++] = 'Q';
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

  set_compiler_error_item(itemstore_root(config.itemstore_ctx), &diag);

  ITEM_t *err = find_item(itemstore_root(config.itemstore_ctx), "error");
  ASSERT_NOT_NULL(err);
  ASSERT_EQ_INT(VALUE_int, item_value(err)->type);
  ASSERT_EQ_INT(ERR_COMP_UNKNOWNCHAR, item_value(err)->i);

  ITEM_t *msg = find_item(itemstore_root(config.itemstore_ctx), "error.msg");
  ASSERT_NOT_NULL(msg);
  ASSERT_EQ_INT(VALUE_str, item_value(msg)->type);
  ASSERT_TRUE(strstr(item_value(msg)->s, "SIN-PARSE-0005") != NULL);
  ASSERT_TRUE(strstr(item_value(msg)->s, "stage=PARSE") != NULL);
  ASSERT_TRUE(strstr(item_value(msg)->s, "file=example.sin") != NULL);
  ASSERT_TRUE(strstr(item_value(msg)->s, "line=7") != NULL);
  ASSERT_TRUE(strstr(item_value(msg)->s, "column=3") != NULL);
  ASSERT_TRUE(strstr(item_value(msg)->s, "message=parse: unexpected character") != NULL);
  ASSERT_TRUE(strstr(item_value(msg)->s, "excerpt=bad ^ token") != NULL);

  ITEM_t *error_item = find_item(itemstore_root(config.itemstore_ctx), "error.item");
  ASSERT_NOT_NULL(error_item);
  ASSERT_EQ_INT(VALUE_nil, item_value(error_item)->type);

  ITEM_t *code = find_item(itemstore_root(config.itemstore_ctx), "error.code");
  ASSERT_NOT_NULL(code);
  ASSERT_EQ_INT(VALUE_str, item_value(code)->type);
  ASSERT_TRUE(strcmp(item_value(code)->s, "SIN-PARSE-0005") == 0);

  ITEM_t *stage = find_item(itemstore_root(config.itemstore_ctx), "error.stage");
  ASSERT_NOT_NULL(stage);
  ASSERT_EQ_INT(VALUE_str, item_value(stage)->type);
  ASSERT_TRUE(strcmp(item_value(stage)->s, "PARSE") == 0);

  ITEM_t *file = find_item(itemstore_root(config.itemstore_ctx), "error.file");
  ASSERT_NOT_NULL(file);
  ASSERT_EQ_INT(VALUE_str, item_value(file)->type);
  ASSERT_TRUE(strcmp(item_value(file)->s, "example.sin") == 0);

  ITEM_t *line_item = find_item(itemstore_root(config.itemstore_ctx), "error.line");
  ASSERT_NOT_NULL(line_item);
  ASSERT_EQ_INT(VALUE_int, item_value(line_item)->type);
  ASSERT_EQ_INT(7, item_value(line_item)->i);

  ITEM_t *column = find_item(itemstore_root(config.itemstore_ctx), "error.column");
  ASSERT_NOT_NULL(column);
  ASSERT_EQ_INT(VALUE_int, item_value(column)->type);
  ASSERT_EQ_INT(3, item_value(column)->i);

  ITEM_t *excerpt = find_item(itemstore_root(config.itemstore_ctx), "error.excerpt");
  ASSERT_NOT_NULL(excerpt);
  ASSERT_EQ_INT(VALUE_str, item_value(excerpt)->type);
  ASSERT_TRUE(strcmp(item_value(excerpt)->s, "bad ^ token") == 0);

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
  ASSERT_TRUE(value_mod(&left, &right, &result));
  ASSERT_EQ_INT(1, result.i);
  VALUE_t neg_left = {VALUE_int, {.i = -9}};
  ASSERT_TRUE(value_mod(&neg_left, &right, &result));
  ASSERT_EQ_INT(-1, result.i);
  VALUE_t zero = {VALUE_int, {.i = 0}};
  ASSERT_TRUE(!value_mod(&left, &zero, &result));
  ASSERT_EQ_INT(VALUE_nil, result.type);

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
  code[pos++] = 'Q';
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
  ASSERT_TRUE(value_mod(&min, &minus_one, &result));
  ASSERT_EQ_INT(VALUE_int, result.type);
  ASSERT_EQ_INT(0, result.i);
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

static VALUE_t run_local_step(int64_t initial, uint8_t opcode, const char *name) {
  uint8_t code[32] = {0};
  size_t pos = 0;
  code[pos++] = 1;
  code[pos++] = 0;
  code[pos++] = 'p'; emit_i64(code, &pos, initial);
  code[pos++] = 'c'; code[pos++] = 0;
  code[pos++] = opcode; code[pos++] = 0;
  code[pos++] = 'e'; code[pos++] = 0;
  code[pos++] = 'Q';
  code[pos++] = 'h';
  return run_code(name, code, pos);
}

void test_value_local_increment_decrement_boundaries(void) {
  setup_runtime();

  VALUE_t result = run_local_step(INT64_MAX - 1, 'f', "test.local_increment_boundary");
  ASSERT_EQ_INT(VALUE_int, result.type);
  ASSERT_EQ_INT(INT64_MAX, result.i);
  value_free(&result);

  result = run_local_step(INT64_MAX, 'f', "test.local_increment_overflow");
  ASSERT_EQ_INT(VALUE_nil, result.type);
  value_free(&result);

  result = run_local_step(INT64_MIN + 1, 'g', "test.local_decrement_boundary");
  ASSERT_EQ_INT(VALUE_int, result.type);
  ASSERT_EQ_INT(INT64_MIN, result.i);
  value_free(&result);

  result = run_local_step(INT64_MIN, 'g', "test.local_decrement_overflow");
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
  code[pos++] = 'Q';
  code[pos++] = 'h';

  VALUE_t result = run_code("test.value_push_int_i64", code, pos);
  ASSERT_EQ_INT(VALUE_int, result.type);
  ASSERT_TRUE(result.i == expected);
  value_free(&result);

  pos = 0;
  code[pos++] = 0;
  code[pos++] = 0;
  code[pos++] = 'p';
  emit_i64(code, &pos, expected);
  code[pos++] = 'h';
  result = run_code("test.value_push_int_halt_nil", code, pos);
  ASSERT_EQ_INT(VALUE_nil, result.type);
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
    code[pos++] = 'Q';
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
  ASSERT_TRUE(value_mod(&int_value, &float_value, &result));
  ASSERT_EQ_INT(VALUE_float, result.type);
  ASSERT_TRUE(result.f == 0.0);
  VALUE_t float_five_half = {VALUE_float, {.f = 5.5}};
  VALUE_t float_two = {VALUE_float, {.f = 2.0}};
  ASSERT_TRUE(value_mod(&float_five_half, &float_two, &result));
  ASSERT_EQ_INT(VALUE_float, result.type);
  ASSERT_TRUE(result.f == 1.5);
  ASSERT_TRUE(value_mod(&one, &zero, &result));
  ASSERT_EQ_INT(VALUE_float, result.type);
  ASSERT_TRUE(isnan(result.f));

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
  code[pos++] = 'Q';
  code[pos++] = 'h';

  VALUE_t result = run_code("test.value_float_arithmetic_bytecode", code, pos);
  ASSERT_EQ_INT(VALUE_float, result.type);
  ASSERT_TRUE(value_float_to_bits(result.f) == UINT64_C(0x7ff0000000000000));
  value_free(&result);

  pos = 0;
  code[pos++] = 0; code[pos++] = 0;
  code[pos++] = 'p'; emit_i64(code, &pos, 5);
  code[pos++] = 'p'; emit_i64(code, &pos, 2);
  code[pos++] = '%'; code[pos++] = 'Q'; code[pos++] = 'h';
  result = run_code("test.value_modulo_bytecode", code, pos);
  ASSERT_EQ_INT(VALUE_int, result.type);
  ASSERT_EQ_INT(1, result.i);
  value_free(&result);

  pos = 0;
  code[pos++] = 0;
  code[pos++] = 0;
  code[pos++] = 'P'; emit_u64(code, &pos, UINT64_C(0x0000000000000000)); /* +0.0 */
  code[pos++] = 'n';
  code[pos++] = 'Q';
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
  code[pos++] = 'Q';
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
  ASSERT_TRUE(!value_mod(&nil_value, &int_value, &result));
  ASSERT_EQ_INT(VALUE_nil, result.type);
  ASSERT_TRUE(!value_mod(&bool_value, &int_value, &result));
  ASSERT_EQ_INT(VALUE_nil, result.type);
  ASSERT_TRUE(!value_mod(&str_value, &int_value, &result));
  ASSERT_EQ_INT(VALUE_nil, result.type);
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
  code[pos++] = 'Q';
  code[pos++] = 'h';

  VALUE_t result = run_code("test.value_string_concat", code, pos);
  ASSERT_EQ_INT(VALUE_str, result.type);
  ASSERT_TRUE(strcmp(result.s, "hello world") == 0);
  value_free(&result);
  teardown_runtime();
}

void test_value_string_tracker_releases_through_value_free(void) {
  size_t baseline = strbuf_tracked_count_for_tests();

  for (int iteration = 0; iteration < 100; iteration++) {
    VALUE_t left = {VALUE_str, {.s = strdup("0123456789abcdef")}};
    VALUE_t right = {VALUE_str, {.s = strdup("x")}};
    VALUE_t result = concat_two_strings(left, right);
    ASSERT_EQ_INT(VALUE_str, result.type);
    ASSERT_EQ_INT((long long)baseline + 1,
                  (long long)strbuf_tracked_count_for_tests());
    value_free(&result);
    ASSERT_EQ_INT((long long)baseline,
                  (long long)strbuf_tracked_count_for_tests());
  }
}

void test_value_string_tracker_releases_through_stack_discard(void) {
  size_t baseline = strbuf_tracked_count_for_tests();
  STACK_t *stack = make_stack();
  ASSERT_NOT_NULL(stack);

  VALUE_t left = {VALUE_str, {.s = strdup("left")}};
  VALUE_t right = {VALUE_str, {.s = strdup(" right")}};
  VALUE_t result = concat_two_strings(left, right);
  ASSERT_EQ_INT((long long)baseline + 1,
                (long long)strbuf_tracked_count_for_tests());
  push_stack(stack, result);
  throwaway_stack(stack);
  ASSERT_EQ_INT((long long)baseline,
                (long long)strbuf_tracked_count_for_tests());
  destroy_stack(stack);
}

void test_value_string_tracker_forgets_before_reallocation(void) {
  size_t baseline = strbuf_tracked_count_for_tests();
  VALUE_t left = {VALUE_str, {.s = strdup("0123456789abcdef")}};
  VALUE_t right = {VALUE_str, {.s = strdup("x")}};
  VALUE_t tracked = concat_two_strings(left, right);
  ASSERT_EQ_INT(VALUE_str, tracked.type);
  tracked = concat_two_strings(tracked,
                               (VALUE_t){VALUE_str, {.s = strdup("y")}});
  ASSERT_EQ_INT(VALUE_str, tracked.type);

  value_free(&tracked);
  ASSERT_EQ_INT((long long)baseline,
                (long long)strbuf_tracked_count_for_tests());

  char *unrelated = malloc(2);
  ASSERT_NOT_NULL(unrelated);
  unrelated[0] = 'z';
  unrelated[1] = '\0';
  char *suffix = malloc(20);
  ASSERT_NOT_NULL(suffix);
  memset(suffix, 'y', 19);
  suffix[19] = '\0';
  VALUE_t result = concat_two_strings((VALUE_t){VALUE_str, {.s = unrelated}},
                                      (VALUE_t){VALUE_str, {.s = suffix}});
  ASSERT_EQ_INT(VALUE_str, result.type);
  ASSERT_EQ_INT(20, (long long)strlen(result.s));
  value_free(&result);
}

void test_value_plain_text_formats_nonowning(void) {
  typedef struct {
    VALUE_e type;
    int64_t integer;
    uint64_t float_bits;
    VALUE_text_nil_policy_e nil_policy;
    VALUE_text_result_e result;
    const char *expected;
  } plain_text_case_t;

  const plain_text_case_t cases[] = {
    {VALUE_int, INT64_MIN, 0, VALUE_TEXT_NIL_LITERAL, VALUE_TEXT_OK,
     "-9223372036854775808"},
    {VALUE_int, INT64_MAX, 0, VALUE_TEXT_NIL_LITERAL, VALUE_TEXT_OK,
     "9223372036854775807"},
    {VALUE_float, 0, UINT64_C(0x0000000000000000), VALUE_TEXT_NIL_LITERAL,
     VALUE_TEXT_OK, "0.0"},
    {VALUE_float, 0, UINT64_C(0x8000000000000000), VALUE_TEXT_NIL_LITERAL,
     VALUE_TEXT_OK, "-0.0"},
    {VALUE_float, 0, UINT64_C(0x7ff0000000000000), VALUE_TEXT_NIL_LITERAL,
     VALUE_TEXT_OK, "inf"},
    {VALUE_float, 0, UINT64_C(0xfff0000000000000), VALUE_TEXT_NIL_LITERAL,
     VALUE_TEXT_OK, "-inf"},
    {VALUE_float, 0, UINT64_C(0x7ff8000000000042), VALUE_TEXT_NIL_LITERAL,
     VALUE_TEXT_OK, "nan"},
    {VALUE_bool, 1, 0, VALUE_TEXT_NIL_LITERAL, VALUE_TEXT_OK, "true"},
    {VALUE_bool, 0, 0, VALUE_TEXT_NIL_LITERAL, VALUE_TEXT_OK, "false"},
    {VALUE_nil, 0, 0, VALUE_TEXT_NIL_OMIT, VALUE_TEXT_NIL, NULL},
    {VALUE_nil, 0, 0, VALUE_TEXT_NIL_LITERAL, VALUE_TEXT_OK, "nil"},
  };

  for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
    VALUE_t value = {.type = cases[i].type, .i = cases[i].integer};
    if (value.type == VALUE_float) {
      value.f = value_float_from_bits(cases[i].float_bits);
    }
    VALUE_t before = value;
    char buffer[VALUE_PLAIN_TEXT_BUFFER_SIZE];
    const char *text = NULL;
    size_t text_length = 0;
    VALUE_text_result_e result = value_plain_text(
        &value, cases[i].nil_policy, buffer, sizeof(buffer), &text,
        &text_length);
    ASSERT_EQ_INT(cases[i].result, result);
    ASSERT_EQ_INT(before.type, value.type);
    ASSERT_EQ_INT(before.i, value.i);
    if (result == VALUE_TEXT_OK) {
      ASSERT_NOT_NULL(text);
      ASSERT_TRUE(strcmp(text, cases[i].expected) == 0);
      ASSERT_EQ_INT(strlen(cases[i].expected), text_length);
    } else {
      ASSERT_TRUE(text == NULL);
      ASSERT_EQ_INT(0, text_length);
    }
  }

  char *string_storage = strdup("borrowed text");
  ASSERT_NOT_NULL(string_storage);
  VALUE_t string_value = {VALUE_str, {.s = string_storage}};
  char untouched[] = "unchanged";
  const char *text = NULL;
  size_t text_length = 0;
  VALUE_text_result_e result = value_plain_text(
      &string_value, VALUE_TEXT_NIL_LITERAL, untouched, sizeof(untouched),
      &text, &text_length);
  ASSERT_EQ_INT(VALUE_TEXT_OK, result);
  ASSERT_TRUE(text == string_storage);
  ASSERT_EQ_INT(strlen(string_storage), text_length);
  ASSERT_TRUE(strcmp(untouched, "unchanged") == 0);
  ASSERT_TRUE(string_value.s == string_storage);
  ASSERT_TRUE(strcmp(string_value.s, "borrowed text") == 0);
  value_free(&string_value);

  VALUE_t null_string = {VALUE_str, {.s = NULL}};
  result = value_plain_text(&null_string, VALUE_TEXT_NIL_LITERAL, NULL, 0,
                            &text, &text_length);
  ASSERT_EQ_INT(VALUE_TEXT_OK, result);
  ASSERT_TRUE(text != NULL && text[0] == '\0');
  ASSERT_EQ_INT(0, text_length);

  const plain_text_case_t undersized[] = {
    {VALUE_int, 42, 0, VALUE_TEXT_NIL_LITERAL, VALUE_TEXT_BUFFER_TOO_SMALL,
     NULL},
    {VALUE_float, 0, UINT64_C(0x3ff8000000000000), VALUE_TEXT_NIL_LITERAL,
     VALUE_TEXT_BUFFER_TOO_SMALL, NULL},
    {VALUE_bool, 1, 0, VALUE_TEXT_NIL_LITERAL, VALUE_TEXT_BUFFER_TOO_SMALL,
     NULL},
    {VALUE_nil, 0, 0, VALUE_TEXT_NIL_LITERAL, VALUE_TEXT_BUFFER_TOO_SMALL,
     NULL},
  };
  for (size_t i = 0; i < sizeof(undersized) / sizeof(undersized[0]); i++) {
    VALUE_t value = {.type = undersized[i].type, .i = undersized[i].integer};
    if (value.type == VALUE_float) value.f = value_float_from_bits(undersized[i].float_bits);
    char buffer[1] = {'x'};
    result = value_plain_text(&value, undersized[i].nil_policy, buffer,
                              sizeof(buffer), &text, &text_length);
    ASSERT_EQ_INT(VALUE_TEXT_BUFFER_TOO_SMALL, result);
    ASSERT_TRUE(text == NULL);
    ASSERT_EQ_INT(0, text_length);
    ASSERT_EQ_INT('x', buffer[0]);
  }

  VALUE_t unknown = {.type = (VALUE_e)99, .i = 7};
  char buffer[VALUE_PLAIN_TEXT_BUFFER_SIZE];
  result = value_plain_text(&unknown, VALUE_TEXT_NIL_LITERAL, buffer,
                            sizeof(buffer), &text, &text_length);
  ASSERT_EQ_INT(VALUE_TEXT_UNKNOWN_TYPE, result);
  ASSERT_TRUE(text == NULL);
  ASSERT_EQ_INT(0, text_length);
  result = value_plain_text(NULL, VALUE_TEXT_NIL_LITERAL, buffer,
                            sizeof(buffer), &text, &text_length);
  ASSERT_EQ_INT(VALUE_TEXT_FORMAT_ERROR, result);

  SIN_ITEMREF_t *ref = sin_itemref_create("root.child");
  ASSERT_NOT_NULL(ref);
  VALUE_t itemref = {VALUE_itemref, {.itemref = ref}};
  char itembuf[32];
  result = value_plain_text(&itemref, VALUE_TEXT_NIL_LITERAL, itembuf,
                            sizeof(itembuf), &text, &text_length);
  ASSERT_EQ_INT(VALUE_TEXT_OK, result);
  ASSERT_TRUE(strcmp(itembuf, "&root.child") == 0);
  char keep[] = "keep";
  result = value_plain_text(&itemref, VALUE_TEXT_NIL_LITERAL, keep, 4,
                            &text, &text_length);
  ASSERT_EQ_INT(VALUE_TEXT_BUFFER_TOO_SMALL, result);
  ASSERT_TRUE(strcmp(keep, "keep") == 0);
  value_free(&itemref);
}

void test_value_itemref_lifecycle_and_contract(void) {
  SIN_ITEMREF_t *ref = sin_itemref_create("root.child");
  ASSERT_NOT_NULL(ref);
  ASSERT_EQ_INT(strlen("root.child"), sin_itemref_path_length(ref));
  ASSERT_TRUE(strcmp(sin_itemref_path(ref), "root.child") == 0);
  ASSERT_TRUE(strcmp(value_type_name(VALUE_itemref), "itemref") == 0);
  VALUE_t value = {VALUE_itemref, {.itemref = ref}};
  ASSERT_TRUE(value_is_truthy(&value));
  char debug[32];
  ASSERT_TRUE(strcmp(value_debug_string(&value, debug, sizeof(debug)),
                     "&root.child") == 0);
  VALUE_t same = {VALUE_itemref, {.itemref = sin_itemref_create("root.child")}};
  VALUE_t other = {VALUE_itemref, {.itemref = sin_itemref_create("root.other")}};
  VALUE_t str = {VALUE_str, {.s = strdup("root.child")}};
  ASSERT_TRUE(value_equal(&value, &same));
  ASSERT_TRUE(value_not_equal(&value, &other));
  ASSERT_TRUE(value_not_equal(&value, &str));
  VALUE_t clone = value_clone(&value);
  ASSERT_EQ_INT(VALUE_itemref, clone.type);
  ASSERT_TRUE(clone.itemref == ref);
  value_free(&value);
  ASSERT_TRUE(strcmp(sin_itemref_path(clone.itemref), "root.child") == 0);
  VALUE_t moved = VALUE_NIL;
  value_move(&moved, &clone);
  ASSERT_EQ_INT(VALUE_nil, clone.type);
  ASSERT_EQ_INT(VALUE_itemref, moved.type);
  ASSERT_TRUE(strcmp(sin_itemref_path(moved.itemref), "root.child") == 0);
  value_replace(&same, moved);
  ASSERT_EQ_INT(VALUE_itemref, same.type);
  ASSERT_TRUE(strcmp(sin_itemref_path(same.itemref), "root.child") == 0);
  VALUE_t boolean = convert_to_bool(same);
  ASSERT_EQ_INT(VALUE_bool, boolean.type);
  ASSERT_TRUE(boolean.i);
  value_free(&boolean);
  value_free(&other);
  value_free(&str);
  alloc_test_fail_after(1);
  ASSERT_TRUE(sin_itemref_create("x") == NULL);
  alloc_test_fail_after(-1);
  ASSERT_TRUE(sin_itemref_create(NULL) == NULL);
  ASSERT_TRUE(sin_itemref_create("") == NULL);
  char overlong[MAX_ITEM_NAME + 1u];
  memset(overlong, 'x', sizeof(overlong));
  overlong[sizeof(overlong) - 1u] = '\0';
  ASSERT_TRUE(sin_itemref_create(overlong) == NULL);
  VALUE_t malformed = {VALUE_itemref, {.itemref = NULL}};
  ASSERT_TRUE(!value_equal(&malformed, &malformed));
}

void test_value_string_tracker_itemname_cleanup(void) {
  setup_runtime();
  size_t baseline = strbuf_tracked_count_for_tests();
  VALUE_t name_left = {VALUE_str, {.s = strdup("tracked")}};
  VALUE_t name_right = {VALUE_str, {.s = strdup(".item")}};
  VALUE_t itemname = concat_two_strings(name_left, name_right);
  ASSERT_EQ_INT(VALUE_str, itemname.type);
  push_stack(config.vm->stack, itemname);
  push_stack(config.vm->stack, (VALUE_t){VALUE_int, {.i = 7}});

  RuntimeContext ctx;
  runtime_context_init(&ctx, config.vm);
  ctx.itemstore = config.itemstore_ctx;
  op_assignitem(&ctx, NULL, itemstore_root(config.itemstore_ctx));
  ASSERT_NOT_NULL(find_item(itemstore_root(config.itemstore_ctx), "tracked.item"));
  ASSERT_EQ_INT((long long)baseline,
                (long long)strbuf_tracked_count_for_tests());
  teardown_runtime();
  ASSERT_EQ_INT((long long)baseline,
                (long long)strbuf_tracked_count_for_tests());
}

void test_value_string_concat_enforces_string_limit(void) {
  char *left_s = malloc(SIN_MAX_STRING_BYTES + 1);
  ASSERT_NOT_NULL(left_s);
  memset(left_s, 'x', SIN_MAX_STRING_BYTES);
  left_s[SIN_MAX_STRING_BYTES] = '\0';
  VALUE_t left = {VALUE_str, {.s = left_s}};
  VALUE_t right = {VALUE_str, {.s = strdup("y")}};

  VALUE_t result = concat_two_strings(left, right);
  ASSERT_EQ_INT(VALUE_nil, result.type);
}

void test_value_string_boundaries_enforce_string_limit(void) {
  char *too_long = malloc(SIN_MAX_STRING_BYTES + 2);
  ASSERT_NOT_NULL(too_long);
  memset(too_long, 'x', SIN_MAX_STRING_BYTES + 1);
  too_long[SIN_MAX_STRING_BYTES + 1] = '\0';

  VALUE_t source = {VALUE_str, {.s = too_long}};
  VALUE_t clone = value_clone(&source);
  ASSERT_EQ_INT(VALUE_nil, clone.type);
  value_free(&source);

  setup_runtime();
  too_long = malloc(SIN_MAX_STRING_BYTES + 2);
  ASSERT_NOT_NULL(too_long);
  memset(too_long, 'x', SIN_MAX_STRING_BYTES + 1);
  too_long[SIN_MAX_STRING_BYTES + 1] = '\0';
  VALUE_t stored = {VALUE_str, {.s = too_long}};
  ASSERT_TRUE(test_item_set_value(itemstore_root(config.itemstore_ctx), "oversized.value", stored) == NULL);
  ASSERT_TRUE(find_item(itemstore_root(config.itemstore_ctx), "oversized.value") == NULL);
  value_free(&stored);
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

void test_stack_peek_returns_top_pointer_without_popping(void) {
  STACK_t *stack = make_stack();
  ASSERT_NOT_NULL(stack);

  ASSERT_TRUE(peek_stack(stack) == NULL);
  ASSERT_EQ_INT(0, size_stack(stack));

  VALUE_t first = {VALUE_int, {.i = 10}};
  VALUE_t second = {VALUE_int, {.i = 20}};
  push_stack(stack, first);
  push_stack(stack, second);

  int32_t current_before = stack->current;
  VALUE_t *top = peek_stack(stack);
  ASSERT_NOT_NULL(top);
  ASSERT_TRUE(top == &stack->stack[current_before]);
  ASSERT_EQ_INT(VALUE_int, top->type);
  ASSERT_EQ_INT(20, top->i);
  ASSERT_EQ_INT(current_before, stack->current);
  ASSERT_EQ_INT(2, size_stack(stack));

  top->i = 99;
  VALUE_t popped = pop_stack(stack);
  ASSERT_EQ_INT(VALUE_int, popped.type);
  ASSERT_EQ_INT(99, popped.i);
  ASSERT_EQ_INT(current_before - 1, stack->current);

  popped = pop_stack(stack);
  ASSERT_EQ_INT(VALUE_int, popped.type);
  ASSERT_EQ_INT(10, popped.i);
  ASSERT_TRUE(peek_stack(stack) == NULL);

  destroy_stack(stack);
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
    ASSERT_NOT_NULL(test_item_set_value(itemstore_root(config.itemstore_ctx), item_name, original));

    uint8_t code[64] = {0};
    size_t pos = 0;
    code[pos++] = 0;
    code[pos++] = 0;
    code[pos++] = 'l'; emit_str(code, &pos, item_name);
    code[pos++] = 'F'; code[pos++] = 0; code[pos++] = 0;
    code[pos++] = 'Q';
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
  code[pos++] = 'Q';
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
  assert_truncated_bytecode_for_opcode("test.truncated_getlocal", 'e', "LOAD_LOCAL");
  assert_truncated_bytecode_for_opcode("test.truncated_libcall", 'M', "LIBCALL");
  teardown_runtime();
}

void test_interpreter_truncated_libcall_pair_preserves_vm_frames(void) {
  setup_runtime();
  const uint8_t code[] = {0, 0, 'M', 1};
  VALUE_t result = run_code("test.truncated_libcall_pair", code, sizeof(code));
  ASSERT_EQ_INT(VALUE_nil, result.type);
  assert_error_code_and_detail(ERR_RUNTIME_BYTECODE, "truncated LIBCALL");
  ASSERT_EQ_INT(-1, config.vm->stack->current);
  ASSERT_EQ_INT(-1, config.vm->callstack->current);
  teardown_runtime();
}

void test_assigncodeitem_rejects_malformed_source_block_with_runtime_bytecode_error(void) {
  setup_runtime();
  RuntimeContext ctx;
  runtime_context_init(&ctx, config.vm);
  ctx.itemstore = config.itemstore_ctx;
  ITEM_t *current = test_item_set_value(itemstore_root(config.itemstore_ctx), "test.assigncode_bad_source",
                                VALUE_NIL);
  ASSERT_NOT_NULL(current);
  ctx.current_item = current;

  VALUE_t target = {VALUE_str, {.s = strdup("test.bad_code_source")}};
  ASSERT_NOT_NULL(target.s);
  push_stack(config.vm->stack, target);

  uint8_t code[] = {'B', 'P', 0, 0};
  runtime_decoder_init(&ctx.decoder, code + 1, code + sizeof(code));
  uint8_t *next = op_assigncodeitem(&ctx, code + 1, current);
  ASSERT_TRUE(next == NULL);
  assert_error_code_and_detail(ERR_RUNTIME_BYTECODE,
                               "Invalid source block in code assignment bytecode.");
  teardown_runtime();
}

void test_assigncodeitem_rejects_invalid_target_name_type_with_runtime_item_error(void) {
  setup_runtime();
  uint8_t code[64] = {0};
  size_t pos = 0;
  uint16_t source_len = 0;
  code[pos++] = 0;
  code[pos++] = 0;
  code[pos++] = 'p';
  emit_i64(code, &pos, 1);
  code[pos++] = 'B';
  code[pos++] = 'P';
  code[pos++] = 0;
  code[pos++] = 0;
  memcpy(code + pos, &source_len, sizeof(source_len));
  pos += sizeof(source_len);
  code[pos++] = 'h';

  VALUE_t result = run_code("test.assigncode_bad_target_type", code, pos);
  ASSERT_EQ_INT(VALUE_nil, result.type);
  assert_error_code_and_detail(ERR_RUNTIME_INVALIDITEM,
                               "Invalid item name type for code assignment.");
  teardown_runtime();
}

static void assert_error_nil(void) {
  ITEM_t *err = find_item(itemstore_root(config.itemstore_ctx), "error");
  ASSERT_NOT_NULL(err);
  ASSERT_EQ_INT(VALUE_nil, item_value(err)->type);
}

static void assert_strict_runtime_contract_detail(const char *needle) {
  ITEM_t *err = find_item(itemstore_root(config.itemstore_ctx), "error");
  ASSERT_NOT_NULL(err);
  ASSERT_EQ_INT(ERR_RUNTIME_INVALIDARGS, item_value(err)->i);
  ITEM_t *msg = find_item(itemstore_root(config.itemstore_ctx), "error.msg");
  ASSERT_NOT_NULL(msg);
  ASSERT_EQ_INT(VALUE_str, item_value(msg)->type);
  ASSERT_TRUE(strstr(item_value(msg)->s, needle) != NULL);
}

static VALUE_t run_fetch_with_one_int_arg(const char *runner_name, VALUE_t fetch_name) {
  uint8_t code[256] = {0};
  size_t pos = 0;
  code[pos++] = 0;
  code[pos++] = 0;
  code[pos++] = 'p'; emit_i64(code, &pos, 42);
  if (fetch_name.type == VALUE_str) {
    code[pos++] = 'l'; emit_str(code, &pos, fetch_name.s);
  } else if (fetch_name.type == VALUE_int) {
    code[pos++] = 'p'; emit_i64(code, &pos, fetch_name.i);
  } else {
    ASSERT_TRUE(false);
  }
  code[pos++] = 'F'; code[pos++] = 1; code[pos++] = 0;
  code[pos++] = 'h';
  return run_code(runner_name, code, pos);
}

void test_strict_runtime_contracts_default_preserves_fetch_argument_drops(void) {
  setup_runtime();
  config.strict_runtime_contracts = false;
  VALUE_t name = {VALUE_str, {.s = "missing.default"}};
  VALUE_t result = run_fetch_with_one_int_arg("strict_runtime.default_runner", name);
  ASSERT_EQ_INT(VALUE_nil, result.type);
  assert_error_nil();
  teardown_runtime();
}

void test_value_item_fetch_discards_arguments_and_reports_strict_contract(void) {
  for (int strict = 0; strict <= 1; ++strict) {
    setup_runtime();
    config.strict_runtime_contracts = strict != 0;
    ITEM_t *target = test_item_set_value(
        itemstore_root(config.itemstore_ctx), "value_target",
        (VALUE_t){VALUE_str, {.s = strdup("stored")}});
    ASSERT_NOT_NULL(target);

    RuntimeContext ctx;
    runtime_context_init(&ctx, config.vm);
    ctx.itemstore = config.itemstore_ctx;
    ctx.strict_runtime_contracts = config.strict_runtime_contracts;
    uint8_t code[] = {1, 0};
    runtime_decoder_init(&ctx.decoder, code, code + sizeof(code));
    push_stack(config.vm->stack,
               (VALUE_t){VALUE_str, {.s = strdup("argument")}});
    push_stack(config.vm->stack,
               (VALUE_t){VALUE_str, {.s = strdup("value_target")}});
    uint8_t *next = op_fetchitem(&ctx, code, target);
    ASSERT_TRUE(next == code + sizeof(code));
    ASSERT_EQ_INT(0, config.vm->stack->current);
    VALUE_t *result = peek_stack(config.vm->stack);
    ASSERT_NOT_NULL(result);
    ASSERT_EQ_INT(VALUE_str, result->type);
    ASSERT_TRUE(strcmp(result->s, "stored") == 0);
    if (strict) {
      assert_strict_runtime_contract_detail("value target item");
    } else {
      ITEM_t *err = find_item(itemstore_root(config.itemstore_ctx), "error");
      if (err != NULL) ASSERT_EQ_INT(VALUE_nil, item_value(err)->type);
    }
    teardown_runtime();
  }
}

void test_strict_validation_alone_preserves_fetch_argument_drops(void) {
  setup_runtime();
  config.strict_validation = true;
  config.strict_runtime_contracts = false;
  VALUE_t name = {VALUE_str, {.s = "missing.strict_validation_only"}};
  VALUE_t result = run_fetch_with_one_int_arg("strict_runtime.strict_validation_only_runner", name);
  ASSERT_EQ_INT(VALUE_nil, result.type);
  assert_error_nil();
  teardown_runtime();
}

void test_strict_runtime_contracts_reports_too_many_item_arguments(void) {
  setup_runtime();
  uint8_t target_code[] = {0, 0, 'h'};
  uint8_t *target = malloc(sizeof(target_code));
  ASSERT_NOT_NULL(target);
  memcpy(target, target_code, sizeof(target_code));
  ASSERT_NOT_NULL(test_item_set_code(itemstore_root(config.itemstore_ctx), "strict_runtime.target", sizeof(target_code), target));
  config.strict_runtime_contracts = true;
  VALUE_t name = {VALUE_str, {.s = "strict_runtime.target"}};
  VALUE_t result = run_fetch_with_one_int_arg("strict_runtime.too_many_runner", name);
  ASSERT_EQ_INT(VALUE_nil, result.type);
  assert_strict_runtime_contract_detail("extra argument for target item");
  teardown_runtime();
}

void test_strict_runtime_contracts_reports_invalid_item_name_arguments(void) {
  setup_runtime();
  config.strict_runtime_contracts = true;
  VALUE_t name = {VALUE_int, {.i = 7}};
  VALUE_t result = run_fetch_with_one_int_arg("strict_runtime.invalid_name_runner", name);
  ASSERT_EQ_INT(VALUE_nil, result.type);
  assert_strict_runtime_contract_detail("invalid item fetch name type");
  teardown_runtime();
}

void test_strict_runtime_contracts_reports_missing_item_arguments(void) {
  setup_runtime();
  config.strict_runtime_contracts = true;
  VALUE_t name = {VALUE_str, {.s = "strict_runtime.missing"}};
  VALUE_t result = run_fetch_with_one_int_arg("strict_runtime.missing_runner", name);
  ASSERT_EQ_INT(VALUE_nil, result.type);
  assert_strict_runtime_contract_detail("missing target item");
  teardown_runtime();
}

void test_strict_runtime_contracts_uses_context_itemroot(void) {
  memset(&config, 0, sizeof(config));
  init_errmsg();
  ITEM_t *root = make_root_item("root");
  ASSERT_NOT_NULL(root);
  VM_t *vm = make_vm();
  ASSERT_NOT_NULL(vm);

  uint8_t code[64] = {0};
  size_t pos = 0;
  code[pos++] = 0;
  code[pos++] = 0;
  code[pos++] = 'p'; emit_i64(code, &pos, 42);
  code[pos++] = 'l'; emit_str(code, &pos, "strict_runtime.context_missing");
  code[pos++] = 'F'; code[pos++] = 1; code[pos++] = 0;
  code[pos++] = 'h';
  uint8_t *bytecode = malloc(pos);
  ASSERT_NOT_NULL(bytecode);
  memcpy(bytecode, code, pos);
  ITEM_t *runner = test_item_set_code(root, "strict_runtime.context_runner",
                                    (uint32_t)pos, bytecode);
  ASSERT_NOT_NULL(runner);

  RuntimeContext ctx;
  runtime_context_init(&ctx, vm);
  ctx.itemstore = itemstore_owner(root);
  ctx.strict_runtime_contracts = true;
  VALUE_t result = interpret(&ctx, runner);
  ASSERT_EQ_INT(VALUE_nil, result.type);

  ITEM_t *err = find_item(root, "error");
  ASSERT_NOT_NULL(err);
  ASSERT_EQ_INT(ERR_RUNTIME_INVALIDARGS, item_value(err)->i);
  ITEM_t *msg = find_item(root, "error.msg");
  ASSERT_NOT_NULL(msg);
  ASSERT_EQ_INT(VALUE_str, item_value(msg)->type);
  ASSERT_TRUE(strstr(item_value(msg)->s, "missing target item") != NULL);

  destroy_vm(vm);
  destroy_item(root);
  memset(&config, 0, sizeof(config));
}

void test_runtime_bytecode_safety_is_mandatory(void) {
  setup_runtime();
  uint8_t code[] = {0, 0, 'e', 1, 'h'};
  uint8_t *bytecode = malloc(sizeof(code));
  ASSERT_NOT_NULL(bytecode);
  memcpy(bytecode, code, sizeof(code));
  ITEM_t *item = test_item_set_code(itemstore_root(config.itemstore_ctx), "test.strict_invalid_local", sizeof(code), bytecode);
  ASSERT_NOT_NULL(item);

  config.strict_validation = false;
  VALUE_t result = run_interpret(item);
  ASSERT_EQ_INT(VALUE_nil, result.type);
  ITEM_t *err = find_item(itemstore_root(config.itemstore_ctx), "error");
  ASSERT_NOT_NULL(err);
  ASSERT_EQ_INT(ERR_RUNTIME_BYTECODE, item_value(err)->i);
  ITEM_t *msg = find_item(itemstore_root(config.itemstore_ctx), "error.msg");
  ASSERT_NOT_NULL(msg);
  ASSERT_TRUE(strstr(item_value(msg)->s, "local index") != NULL);

  teardown_runtime();
  setup_runtime();
  bytecode = malloc(sizeof(code));
  ASSERT_NOT_NULL(bytecode);
  memcpy(bytecode, code, sizeof(code));
  item = test_item_set_code(itemstore_root(config.itemstore_ctx), "test.strict_invalid_local", sizeof(code), bytecode);
  ASSERT_NOT_NULL(item);
  config.strict_validation = true;
  int32_t before_current = config.vm->stack->current;
  uint8_t before_locals = config.vm->stack->locals;
  uint8_t before_params = config.vm->stack->params;
  result = run_interpret(item);
  ASSERT_EQ_INT(VALUE_nil, result.type);
  ASSERT_TRUE(!item_is_in_use(item));
  ASSERT_EQ_INT(before_current, config.vm->stack->current);
  ASSERT_EQ_INT(before_locals, config.vm->stack->locals);
  ASSERT_EQ_INT(before_params, config.vm->stack->params);

  err = find_item(itemstore_root(config.itemstore_ctx), "error");
  ASSERT_NOT_NULL(err);
  ASSERT_EQ_INT(ERR_RUNTIME_BYTECODE, item_value(err)->i);
  msg = find_item(itemstore_root(config.itemstore_ctx), "error.msg");
  ASSERT_NOT_NULL(msg);
  ASSERT_EQ_INT(VALUE_str, item_value(msg)->type);
  ASSERT_TRUE(strstr(item_value(msg)->s, "test.strict_invalid_local") != NULL);
  ASSERT_TRUE(strstr(item_value(msg)->s, "offset") != NULL);
  ASSERT_TRUE(strstr(item_value(msg)->s, "local index") != NULL);
  ITEM_t *error_item = find_item(itemstore_root(config.itemstore_ctx), "error.item");
  ASSERT_NOT_NULL(error_item);
  ASSERT_EQ_INT(VALUE_str, item_value(error_item)->type);
  ASSERT_TRUE(strcmp(item_value(error_item)->s, "test.strict_invalid_local") == 0);
  teardown_runtime();
}

void test_runtime_bytecode_safety_rejects_null_bytecode(void) {
  setup_runtime();
  ITEM_t *item = test_item_set_code(itemstore_root(config.itemstore_ctx), "nullcode", 0, NULL);
  ASSERT_NOT_NULL(item);
  VALUE_t result = run_interpret(item);
  ASSERT_EQ_INT(VALUE_nil, result.type);
  ASSERT_TRUE(!item_is_in_use(item));
  ITEM_t *err = find_item(itemstore_root(config.itemstore_ctx), "error");
  ASSERT_NOT_NULL(err);
  ASSERT_EQ_INT(ERR_RUNTIME_BYTECODE, item_value(err)->i);
  ITEM_t *msg = find_item(itemstore_root(config.itemstore_ctx), "error.msg");
  ASSERT_NOT_NULL(msg);
  ASSERT_TRUE(strstr(item_value(msg)->s, "null bytecode") != NULL);
  teardown_runtime();
}

enum {
  ERROR_TEST_FIELD_ERROR,
  ERROR_TEST_FIELD_MESSAGE,
  ERROR_TEST_FIELD_ITEM,
  ERROR_TEST_FIELD_CODE,
  ERROR_TEST_FIELD_STAGE,
  ERROR_TEST_FIELD_FILE,
  ERROR_TEST_FIELD_LINE,
  ERROR_TEST_FIELD_COLUMN,
  ERROR_TEST_FIELD_EXCERPT,
  ERROR_TEST_FIELD_COUNT
};

#define ERROR_TEST_TEXT_CAPACITY 512u
#define ERROR_OOM_SWEEP_BOUND 256L

static const char *const error_test_field_names[ERROR_TEST_FIELD_COUNT] = {
  "error",
  "error.msg",
  "error.item",
  "error.code",
  "error.stage",
  "error.file",
  "error.line",
  "error.column",
  "error.excerpt"
};

typedef struct {
  VALUE_e types[ERROR_TEST_FIELD_COUNT];
  int64_t integers[ERROR_TEST_FIELD_COUNT];
  char strings[ERROR_TEST_FIELD_COUNT][ERROR_TEST_TEXT_CAPACITY];
} ErrorItemSnapshot;

static CompilerDiagnostic old_compiler_diagnostic(void) {
  return (CompilerDiagnostic){
    .code = ERR_COMP_UNKNOWNCHAR,
    .phase = DIAG_PHASE_PARSE,
    .message = "old compiler diagnostic",
    .stable_code = "SIN-PARSE-OLD",
    .source_name = "old.sin",
    .excerpt = "old excerpt",
    .line = 4,
    .column = 8,
    .span = 2,
    .has_loc = true
  };
}

static CompilerDiagnostic new_compiler_diagnostic(void) {
  return (CompilerDiagnostic){
    .code = ERR_COMP_TOOMANYLOCALS,
    .phase = DIAG_PHASE_SEMANT,
    .message = "new compiler diagnostic",
    .stable_code = "SIN-SEMANT-NEW",
    .source_name = "new.sin",
    .excerpt = "new excerpt",
    .line = 12,
    .column = 6,
    .span = 3,
    .has_loc = true
  };
}

static void init_error_snapshot(ErrorItemSnapshot *snapshot) {
  memset(snapshot, 0, sizeof *snapshot);
  for (size_t i = 0; i < ERROR_TEST_FIELD_COUNT; i++) {
    snapshot->types[i] = VALUE_nil;
  }
}

static void set_snapshot_int(ErrorItemSnapshot *snapshot, size_t field,
                             int64_t value) {
  snapshot->types[field] = VALUE_int;
  snapshot->integers[field] = value;
}

static void set_snapshot_string(ErrorItemSnapshot *snapshot, size_t field,
                                const char *value) {
  snapshot->types[field] = VALUE_str;
  int written = snprintf(snapshot->strings[field],
                         sizeof snapshot->strings[field], "%s", value);
  ASSERT_TRUE(written >= 0 &&
              (size_t)written < sizeof snapshot->strings[field]);
}

static void snapshot_error_item(ITEM_t *root, ErrorItemSnapshot *snapshot) {
  init_error_snapshot(snapshot);
  for (size_t i = 0; i < ERROR_TEST_FIELD_COUNT; i++) {
    ITEM_t *field = find_item(root, error_test_field_names[i]);
    ASSERT_NOT_NULL(field);
    const VALUE_t *value = item_value(field);
    ASSERT_NOT_NULL(value);
    snapshot->types[i] = value->type;
    if (value->type == VALUE_int) {
      snapshot->integers[i] = value->i;
    } else if (value->type == VALUE_str) {
      ASSERT_NOT_NULL(value->s);
      int written = snprintf(snapshot->strings[i],
                             sizeof snapshot->strings[i], "%s", value->s);
      ASSERT_TRUE(written >= 0 &&
                  (size_t)written < sizeof snapshot->strings[i]);
    } else {
      ASSERT_EQ_INT(VALUE_nil, value->type);
    }
  }
}

static bool error_item_matches_snapshot(ITEM_t *root,
                                        const ErrorItemSnapshot *snapshot) {
  for (size_t i = 0; i < ERROR_TEST_FIELD_COUNT; i++) {
    ITEM_t *field = find_item(root, error_test_field_names[i]);
    const VALUE_t *value = item_value(field);
    if (!value || value->type != snapshot->types[i]) return false;
    if (value->type == VALUE_int &&
        value->i != snapshot->integers[i]) {
      return false;
    }
    if (value->type == VALUE_str &&
        (!value->s || strcmp(value->s, snapshot->strings[i]) != 0)) {
      return false;
    }
  }
  return true;
}

static void assert_error_snapshot(ITEM_t *root,
                                  const ErrorItemSnapshot *snapshot) {
  ASSERT_TRUE(error_item_matches_snapshot(root, snapshot));
}

static void expected_simple_error(ErrorItemSnapshot *snapshot, int errnum,
                                  const char *detail,
                                  const char *current_item_name) {
  init_error_snapshot(snapshot);
  set_snapshot_int(snapshot, ERROR_TEST_FIELD_ERROR, errnum);

  char message[ERROR_TEST_TEXT_CAPACITY];
  int written = detail
      ? snprintf(message, sizeof message, "%s (%s)", errmsg[errnum], detail)
      : snprintf(message, sizeof message, "%s", errmsg[errnum]);
  ASSERT_TRUE(written >= 0 && (size_t)written < sizeof message);
  set_snapshot_string(snapshot, ERROR_TEST_FIELD_MESSAGE, message);
  if (current_item_name) {
    set_snapshot_string(snapshot, ERROR_TEST_FIELD_ITEM, current_item_name);
  }
}

static void expected_compiler_error(ErrorItemSnapshot *snapshot,
                                    const CompilerDiagnostic *diag) {
  init_error_snapshot(snapshot);
  const char *code = diag->stable_code
      ? diag->stable_code
      : compiler_diag_stable_code(diag->code, diag->phase);
  const char *stage = compiler_diag_phase_name(diag->phase);
  const char *file = diag->source_name ? diag->source_name : "";
  const char *message = diag->message ? diag->message : "";
  const char *excerpt = diag->excerpt ? diag->excerpt : "";
  int line_number = diag->has_loc ? diag->line : 0;
  int column = diag->has_loc ? diag->column : 0;

  set_snapshot_int(snapshot, ERROR_TEST_FIELD_ERROR, diag->code);
  char formatted[ERROR_TEST_TEXT_CAPACITY];
  int written = snprintf(formatted, sizeof formatted,
      "%s stage=%s file=%s line=%d column=%d message=%s excerpt=%s",
      code, stage, file, line_number, column, message, excerpt);
  ASSERT_TRUE(written >= 0 && (size_t)written < sizeof formatted);
  set_snapshot_string(snapshot, ERROR_TEST_FIELD_MESSAGE, formatted);
  set_snapshot_string(snapshot, ERROR_TEST_FIELD_CODE, code);
  set_snapshot_string(snapshot, ERROR_TEST_FIELD_STAGE, stage);
  set_snapshot_string(snapshot, ERROR_TEST_FIELD_FILE, file);
  set_snapshot_int(snapshot, ERROR_TEST_FIELD_LINE, line_number);
  set_snapshot_int(snapshot, ERROR_TEST_FIELD_COLUMN, column);
  set_snapshot_string(snapshot, ERROR_TEST_FIELD_EXCERPT, excerpt);
}

static void capture_error_field_pointers(
    ITEM_t *root, ITEM_t *pointers[ERROR_TEST_FIELD_COUNT]) {
  for (size_t i = 0; i < ERROR_TEST_FIELD_COUNT; i++) {
    bool found = false;
    pointers[i] = find_item_cached(root, error_test_field_names[i], &found);
    ASSERT_TRUE(found);
    ASSERT_NOT_NULL(pointers[i]);
  }
}

static void assert_error_field_pointers(
    ITEM_t *root, ITEM_t *const pointers[ERROR_TEST_FIELD_COUNT]) {
  for (size_t i = 0; i < ERROR_TEST_FIELD_COUNT; i++) {
    ASSERT_TRUE(find_item(root, error_test_field_names[i]) == pointers[i]);
    ASSERT_TRUE(find_item_cached(root, error_test_field_names[i], NULL) ==
                pointers[i]);
  }
}

static void assert_existing_error_fields_are_nil(ITEM_t *root) {
  for (size_t i = 0; i < ERROR_TEST_FIELD_COUNT; i++) {
    ITEM_t *field = find_item(root, error_test_field_names[i]);
    if (field) {
      const VALUE_t *value = item_value(field);
      ASSERT_NOT_NULL(value);
      ASSERT_EQ_INT(VALUE_nil, value->type);
    }
  }
}

static ITEM_t *set_test_error_string(ITEM_t *root, const char *name,
                                     const char *string) {
  char *copy = strdup(string);
  ASSERT_NOT_NULL(copy);
  ITEM_MUTATION_RESULT_t result = item_set_value(
      root, name, (VALUE_t){.type = VALUE_str, .s = copy});
  if (!item_mutation_succeeded(result)) free(copy);
  ASSERT_TRUE(item_mutation_succeeded(result));
  ASSERT_NOT_NULL(result.item);
  return result.item;
}

static void populate_incomplete_error(
    ITEM_t *root, ITEM_t *pointers[ERROR_TEST_FIELD_COUNT]) {
  memset(pointers, 0, ERROR_TEST_FIELD_COUNT * sizeof *pointers);
  pointers[ERROR_TEST_FIELD_ERROR] = test_item_set_value(
      root, "error", (VALUE_t){.type = VALUE_int, .i = ERR_NETWORK_ERROR});
  ASSERT_NOT_NULL(pointers[ERROR_TEST_FIELD_ERROR]);
  pointers[ERROR_TEST_FIELD_MESSAGE] = set_test_error_string(
      root, "error.msg", "incomplete old message");
  pointers[ERROR_TEST_FIELD_CODE] = set_test_error_string(
      root, "error.code", "INCOMPLETE-OLD");
  pointers[ERROR_TEST_FIELD_LINE] = test_item_set_value(
      root, "error.line", (VALUE_t){.type = VALUE_int, .i = 77});
  ASSERT_NOT_NULL(pointers[ERROR_TEST_FIELD_LINE]);

  for (size_t i = 0; i < ERROR_TEST_FIELD_COUNT; i++) {
    if (pointers[i]) {
      ASSERT_TRUE(find_item_cached(root, error_test_field_names[i], NULL) ==
                  pointers[i]);
    } else {
      ASSERT_TRUE(find_item(root, error_test_field_names[i]) == NULL);
    }
  }
}

static void assert_incomplete_error_pointers(
    ITEM_t *root, ITEM_t *const pointers[ERROR_TEST_FIELD_COUNT]) {
  for (size_t i = 0; i < ERROR_TEST_FIELD_COUNT; i++) {
    if (pointers[i]) {
      ASSERT_TRUE(find_item(root, error_test_field_names[i]) == pointers[i]);
      ASSERT_TRUE(find_item_cached(root, error_test_field_names[i], NULL) ==
                  pointers[i]);
    }
  }
}

static bool all_error_fields_exist(ITEM_t *root) {
  for (size_t i = 0; i < ERROR_TEST_FIELD_COUNT; i++) {
    if (!find_item(root, error_test_field_names[i])) return false;
  }
  return true;
}

void test_error_item_oom_preserves_existing_diagnostic(void) {
  init_errmsg();
  alloc_test_fail_after(-1);
  bool succeeded = false;
  for (long fail_at = 0; fail_at <= ERROR_OOM_SWEEP_BOUND; fail_at++) {
    ITEMSTORE_t *store = itemstore_create("root");
    ASSERT_NOT_NULL(store);
    ITEM_t *root = itemstore_root(store);
    CompilerDiagnostic old_diag = old_compiler_diagnostic();
    set_compiler_error_item(root, &old_diag);

    ErrorItemSnapshot old_snapshot;
    ErrorItemSnapshot new_snapshot;
    snapshot_error_item(root, &old_snapshot);
    expected_simple_error(&new_snapshot, ERR_RUNTIME_INVALIDARGS,
                          "new detail", NULL);
    ITEM_t *pointers[ERROR_TEST_FIELD_COUNT];
    capture_error_field_pointers(root, pointers);
    uint64_t payload_revision = itemstore_payload_revision(store);
    uint64_t topology_revision = itemstore_topology_revision(store);

    alloc_test_fail_after(fail_at);
    set_error_item(root, ERR_RUNTIME_INVALIDARGS, "new detail", NULL);
    alloc_test_fail_after(-1);

    if (error_item_matches_snapshot(root, &new_snapshot)) {
      assert_error_snapshot(root, &new_snapshot);
      assert_error_field_pointers(root, pointers);
      ASSERT_EQ_INT(payload_revision + 1u,
                    itemstore_payload_revision(store));
      ASSERT_EQ_INT(topology_revision, itemstore_topology_revision(store));
      succeeded = true;
    } else {
      assert_error_snapshot(root, &old_snapshot);
      assert_error_field_pointers(root, pointers);
      ASSERT_EQ_INT(payload_revision, itemstore_payload_revision(store));
      ASSERT_EQ_INT(topology_revision, itemstore_topology_revision(store));
    }
    itemstore_destroy(store);
    if (succeeded) break;
  }
  ASSERT_TRUE(succeeded);
}

void test_compiler_error_item_oom_preserves_existing_diagnostic(void) {
  init_errmsg();
  alloc_test_fail_after(-1);
  bool succeeded = false;
  for (long fail_at = 0; fail_at <= ERROR_OOM_SWEEP_BOUND; fail_at++) {
    ITEMSTORE_t *store = itemstore_create("root");
    ASSERT_NOT_NULL(store);
    ITEM_t *root = itemstore_root(store);
    set_error_item(root, ERR_NETWORK_ERROR, "old detail", NULL);

    CompilerDiagnostic new_diag = new_compiler_diagnostic();
    ErrorItemSnapshot old_snapshot;
    ErrorItemSnapshot new_snapshot;
    snapshot_error_item(root, &old_snapshot);
    expected_compiler_error(&new_snapshot, &new_diag);
    ITEM_t *pointers[ERROR_TEST_FIELD_COUNT];
    capture_error_field_pointers(root, pointers);
    uint64_t payload_revision = itemstore_payload_revision(store);
    uint64_t topology_revision = itemstore_topology_revision(store);

    alloc_test_fail_after(fail_at);
    set_compiler_error_item(root, &new_diag);
    alloc_test_fail_after(-1);

    if (error_item_matches_snapshot(root, &new_snapshot)) {
      assert_error_snapshot(root, &new_snapshot);
      assert_error_field_pointers(root, pointers);
      ASSERT_EQ_INT(payload_revision + 1u,
                    itemstore_payload_revision(store));
      ASSERT_EQ_INT(topology_revision, itemstore_topology_revision(store));
      succeeded = true;
    } else {
      assert_error_snapshot(root, &old_snapshot);
      assert_error_field_pointers(root, pointers);
      ASSERT_EQ_INT(payload_revision, itemstore_payload_revision(store));
      ASSERT_EQ_INT(topology_revision, itemstore_topology_revision(store));
    }
    itemstore_destroy(store);
    if (succeeded) break;
  }
  ASSERT_TRUE(succeeded);
}

void test_error_item_oom_without_previous_diagnostic(void) {
  init_errmsg();
  alloc_test_fail_after(-1);
  ErrorItemSnapshot expected;
  expected_simple_error(&expected, ERR_NETWORK_ERROR, "fresh detail", NULL);

  bool succeeded = false;
  for (long fail_at = 0; fail_at <= ERROR_OOM_SWEEP_BOUND; fail_at++) {
    ITEMSTORE_t *store = itemstore_create("root");
    ASSERT_NOT_NULL(store);
    ITEM_t *root = itemstore_root(store);
    uint64_t payload_revision = itemstore_payload_revision(store);

    alloc_test_fail_after(fail_at);
    set_error_item(root, ERR_NETWORK_ERROR, "fresh detail", NULL);
    alloc_test_fail_after(-1);

    if (error_item_matches_snapshot(root, &expected)) {
      assert_error_snapshot(root, &expected);
      ASSERT_EQ_INT(payload_revision + 1u,
                    itemstore_payload_revision(store));
      succeeded = true;
    } else {
      assert_existing_error_fields_are_nil(root);
      ASSERT_EQ_INT(payload_revision, itemstore_payload_revision(store));
    }
    itemstore_destroy(store);
    if (succeeded) break;
  }
  ASSERT_TRUE(succeeded);
}

void test_compiler_error_item_oom_without_previous_diagnostic(void) {
  init_errmsg();
  alloc_test_fail_after(-1);
  CompilerDiagnostic diag = new_compiler_diagnostic();
  ErrorItemSnapshot expected;
  expected_compiler_error(&expected, &diag);

  bool succeeded = false;
  for (long fail_at = 0; fail_at <= ERROR_OOM_SWEEP_BOUND; fail_at++) {
    ITEMSTORE_t *store = itemstore_create("root");
    ASSERT_NOT_NULL(store);
    ITEM_t *root = itemstore_root(store);
    uint64_t payload_revision = itemstore_payload_revision(store);

    alloc_test_fail_after(fail_at);
    set_compiler_error_item(root, &diag);
    alloc_test_fail_after(-1);

    if (error_item_matches_snapshot(root, &expected)) {
      assert_error_snapshot(root, &expected);
      ASSERT_EQ_INT(payload_revision + 1u,
                    itemstore_payload_revision(store));
      succeeded = true;
    } else {
      assert_existing_error_fields_are_nil(root);
      ASSERT_EQ_INT(payload_revision, itemstore_payload_revision(store));
    }
    itemstore_destroy(store);
    if (succeeded) break;
  }
  ASSERT_TRUE(succeeded);
}

void test_error_item_oom_normalizes_incomplete_diagnostic(void) {
  init_errmsg();
  alloc_test_fail_after(-1);
  ErrorItemSnapshot expected;
  expected_simple_error(&expected, ERR_RUNTIME_INVALIDARGS,
                        "replacement detail", NULL);

  bool succeeded = false;
  for (long fail_at = 0; fail_at <= ERROR_OOM_SWEEP_BOUND; fail_at++) {
    ITEMSTORE_t *store = itemstore_create("root");
    ASSERT_NOT_NULL(store);
    ITEM_t *root = itemstore_root(store);
    ITEM_t *pointers[ERROR_TEST_FIELD_COUNT];
    populate_incomplete_error(root, pointers);
    uint64_t payload_revision = itemstore_payload_revision(store);

    alloc_test_fail_after(fail_at);
    set_error_item(root, ERR_RUNTIME_INVALIDARGS, "replacement detail", NULL);
    alloc_test_fail_after(-1);

    if (error_item_matches_snapshot(root, &expected)) {
      assert_error_snapshot(root, &expected);
      succeeded = true;
    } else {
      assert_existing_error_fields_are_nil(root);
    }
    assert_incomplete_error_pointers(root, pointers);
    ASSERT_EQ_INT(payload_revision + 1u,
                  itemstore_payload_revision(store));
    itemstore_destroy(store);
    if (succeeded) break;
  }
  ASSERT_TRUE(succeeded);
}

void test_compiler_error_item_oom_normalizes_incomplete_diagnostic(void) {
  init_errmsg();
  alloc_test_fail_after(-1);
  CompilerDiagnostic diag = new_compiler_diagnostic();
  ErrorItemSnapshot expected;
  expected_compiler_error(&expected, &diag);

  bool succeeded = false;
  for (long fail_at = 0; fail_at <= ERROR_OOM_SWEEP_BOUND; fail_at++) {
    ITEMSTORE_t *store = itemstore_create("root");
    ASSERT_NOT_NULL(store);
    ITEM_t *root = itemstore_root(store);
    ITEM_t *pointers[ERROR_TEST_FIELD_COUNT];
    populate_incomplete_error(root, pointers);
    uint64_t payload_revision = itemstore_payload_revision(store);

    alloc_test_fail_after(fail_at);
    set_compiler_error_item(root, &diag);
    alloc_test_fail_after(-1);

    if (error_item_matches_snapshot(root, &expected)) {
      assert_error_snapshot(root, &expected);
      succeeded = true;
    } else {
      assert_existing_error_fields_are_nil(root);
    }
    assert_incomplete_error_pointers(root, pointers);
    ASSERT_EQ_INT(payload_revision + 1u,
                  itemstore_payload_revision(store));
    itemstore_destroy(store);
    if (succeeded) break;
  }
  ASSERT_TRUE(succeeded);
}

void test_clear_error_item_is_allocation_free_and_atomic(void) {
  init_errmsg();
  alloc_test_fail_after(-1);

  ITEMSTORE_t *store = itemstore_create("root");
  ASSERT_NOT_NULL(store);
  ITEM_t *root = itemstore_root(store);
  CompilerDiagnostic diag = new_compiler_diagnostic();
  set_compiler_error_item(root, &diag);
  ErrorItemSnapshot old_snapshot;
  snapshot_error_item(root, &old_snapshot);
  ITEM_t *pointers[ERROR_TEST_FIELD_COUNT];
  capture_error_field_pointers(root, pointers);
  uint64_t payload_revision = itemstore_payload_revision(store);
  uint64_t topology_revision = itemstore_topology_revision(store);

  item_enter_use(pointers[ERROR_TEST_FIELD_STAGE]);
  clear_error_item(root);
  assert_error_snapshot(root, &old_snapshot);
  assert_error_field_pointers(root, pointers);
  ASSERT_EQ_INT(payload_revision, itemstore_payload_revision(store));
  ASSERT_EQ_INT(topology_revision, itemstore_topology_revision(store));
  item_leave_use(pointers[ERROR_TEST_FIELD_STAGE]);

  alloc_test_fail_after(0);
  clear_error_item(root);
  alloc_test_fail_after(-1);
  ErrorItemSnapshot nil_snapshot;
  init_error_snapshot(&nil_snapshot);
  assert_error_snapshot(root, &nil_snapshot);
  assert_error_field_pointers(root, pointers);
  ASSERT_EQ_INT(payload_revision + 1u, itemstore_payload_revision(store));
  ASSERT_EQ_INT(topology_revision, itemstore_topology_revision(store));
  itemstore_destroy(store);

  bool completed_topology = false;
  for (long fail_at = 0; fail_at <= ERROR_OOM_SWEEP_BOUND; fail_at++) {
    store = itemstore_create("root");
    ASSERT_NOT_NULL(store);
    root = itemstore_root(store);
    populate_incomplete_error(root, pointers);
    payload_revision = itemstore_payload_revision(store);

    alloc_test_fail_after(fail_at);
    clear_error_item(root);
    alloc_test_fail_after(-1);

    assert_existing_error_fields_are_nil(root);
    assert_incomplete_error_pointers(root, pointers);
    ASSERT_EQ_INT(payload_revision + 1u, itemstore_payload_revision(store));
    completed_topology = all_error_fields_exist(root);
    itemstore_destroy(store);
    if (completed_topology) break;
  }
  ASSERT_TRUE(completed_topology);
}

void test_error_item_null_inputs_provenance_and_pins(void) {
  init_errmsg();
  alloc_test_fail_after(-1);
  CompilerDiagnostic diag = new_compiler_diagnostic();
  set_error_item(NULL, ERR_NETWORK_ERROR, "ignored", NULL);
  set_compiler_error_item(NULL, NULL);
  set_compiler_error_item(NULL, &diag);

  ITEMSTORE_t *store = itemstore_create("root");
  ASSERT_NOT_NULL(store);
  ITEM_t *root = itemstore_root(store);
  set_compiler_error_item(root, NULL);
  ErrorItemSnapshot expected;
  expected_simple_error(&expected, ERR_COMP_UNKNOWN, NULL, NULL);
  assert_error_snapshot(root, &expected);

  ITEM_t *current = test_item_set_value(
      root, "origin.child", (VALUE_t){.type = VALUE_int, .i = 1});
  ASSERT_NOT_NULL(current);
  ITEM_t *pointers[ERROR_TEST_FIELD_COUNT];
  capture_error_field_pointers(root, pointers);
  uint64_t payload_revision = itemstore_payload_revision(store);
  set_error_item(root, ERR_RUNTIME_INVALIDARGS, "with provenance", current);
  expected_simple_error(&expected, ERR_RUNTIME_INVALIDARGS,
                        "with provenance", "origin.child");
  assert_error_snapshot(root, &expected);
  assert_error_field_pointers(root, pointers);
  ASSERT_EQ_INT(payload_revision + 1u, itemstore_payload_revision(store));

  ErrorItemSnapshot pinned_snapshot;
  snapshot_error_item(root, &pinned_snapshot);
  payload_revision = itemstore_payload_revision(store);
  item_enter_use(pointers[ERROR_TEST_FIELD_STAGE]);
  set_compiler_error_item(root, &diag);
  assert_error_snapshot(root, &pinned_snapshot);
  assert_error_field_pointers(root, pointers);
  ASSERT_EQ_INT(payload_revision, itemstore_payload_revision(store));
  item_leave_use(pointers[ERROR_TEST_FIELD_STAGE]);

  CompilerDiagnostic empty_diag = {0};
  set_compiler_error_item(root, &empty_diag);
  expected_compiler_error(&expected, &empty_diag);
  assert_error_snapshot(root, &expected);
  assert_error_field_pointers(root, pointers);

  alloc_test_fail_after(-1);
  itemstore_destroy(store);
}
