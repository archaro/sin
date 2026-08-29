#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "error.h"
#include "compiler/emitbc.h"
#include "compiler/ir.h"
#include "compiler/compiler_pipeline.h"
#include "test_assert.h"
#include "test_helpers.h"
#include "bytecode_format.h"
#include "bytecode_verify.h"
#include "bytecode_convert.h"
#include "bytecode_wire.h"
#include "sdiss_core.h"

enum { TEST_SEED = 0x5EED1234u };

typedef struct {
  size_t embedded_count;
  uint16_t source_len;
} EmbeddedDecodeCapture;

static bool capture_embedded_source(const BC_Instruction *instruction,
                                    void *ctx) {
  EmbeddedDecodeCapture *capture = ctx;
  if (instruction->schema->op == IR_OP_ITEM_SAVE_CODE) {
    capture->embedded_count++;
    capture->source_len = instruction->operand.value.bytes.len;
  }
  return true;
}

static void assert_preflight_rejects(IR_Unit *u, int8_t expected_code,
                                     const char *expected_detail) {
  const unsigned char sentinel[] = {
      0xAA, 0xBB, 0xCC, 0xDD, 0x11, 0x22, 0x33, 0x44,
      0x55, 0x66, 0x77, 0x88, 0x99, 0x00, 0xFE, 0xEF};
  unsigned char *storage = malloc(sizeof(sentinel));
  ASSERT_NOT_NULL(storage);
  memcpy(storage, sentinel, sizeof(sentinel));
  OUTPUT_t out = {
      .bytecode = storage,
      .nextbyte = storage + 2,
      .maxsize = sizeof(sentinel)};
  OUTPUT_t before = out;
  CompilerDiagnostic diag;
  compiler_diag_init(&diag);

  ASSERT_EQ_INT(expected_code, t_emit_bytecode_diag(u, 0, 0, &out, &diag));
  ASSERT_NOT_NULL(diag.message);
  ASSERT_TRUE(strstr(diag.message, "ir:") != NULL);
  ASSERT_TRUE(strstr(diag.message, expected_detail) != NULL);
  ASSERT_EQ_INT(0, memcmp(storage, sentinel, sizeof(sentinel)));
  ASSERT_TRUE(out.bytecode == before.bytecode);
  ASSERT_TRUE(out.nextbyte == before.nextbyte);
  ASSERT_EQ_INT(before.maxsize, out.maxsize);

  compiler_diag_reset(&diag);
  free(storage);
  if (u->embedded_code.count > 0 && !u->embedded_code.entries) {
    u->embedded_code.count = 0;
  }
  ir_destroy_unit(u);
}

static void test_emitbc_preflight_rejects_malformed_ir(void) {
  IR_Unit *u = ir_create_unit();
  ASSERT_NOT_NULL(u);
  u->function.count = 1;
  u->function.capacity = 1;
  assert_preflight_rejects(u, ERR_COMP_SYNTAX,
                           "inconsistent count, capacity, or storage");

  u = ir_create_unit();
  ASSERT_NOT_NULL(u);
  u->labels.count = 1;
  u->labels.capacity = 1;
  assert_preflight_rejects(u, ERR_COMP_SYNTAX,
                           "inconsistent count, capacity, or storage");

  u = ir_create_unit();
  ASSERT_NOT_NULL(u);
  u->embedded_code.count = 1;
  u->embedded_code.capacity = 1;
  assert_preflight_rejects(u, ERR_COMP_SYNTAX,
                           "inconsistent count, capacity, or storage");

  u = ir_create_unit();
  t_emit(u, (IR_Inst){.op = (IR_Op)999});
  assert_preflight_rejects(u, ERR_COMP_SYNTAX, "unsupported IR op");

  u = ir_create_unit();
  t_emit(u, (IR_Inst){.op = IR_OP_PUSH_BOOL, .a = 2});
  assert_preflight_rejects(u, ERR_COMP_SYNTAX,
                           "invalid boolean operand");

  u = ir_create_unit();
  t_emit(u, (IR_Inst){.op = IR_OP_CALL, .a = UINT16_MAX + 1});
  assert_preflight_rejects(u, ERR_COMP_TOOMANYARGS,
                           "exceeds bytecode range");

  u = ir_create_unit();
  t_emit(u, (IR_Inst){.op = IR_OP_BUILD_LIST, .a = -1});
  assert_preflight_rejects(u, ERR_COMP_SYNTAX, "negative operand");

  u = ir_create_unit();
  t_emit(u, (IR_Inst){.op = IR_OP_LIBCALL, .a = 0, .b = 256});
  assert_preflight_rejects(u, ERR_COMP_SYNTAX, "invalid pair");

  u = ir_create_unit();
  t_emit(u, (IR_Inst){.op = IR_OP_PUSH_STRING, .imm = 0});
  assert_preflight_rejects(u, ERR_COMP_SYNTAX, "null string payload");

  u = ir_create_unit();
  t_emit(u, (IR_Inst){.op = IR_OP_ITEM_SAVE_CODE, .a = 7});
  assert_preflight_rejects(u, ERR_COMP_SYNTAX,
                           "invalid embedded code index");

  u = ir_create_unit();
  ASSERT_TRUE(ir_add_embedded_code_payload(
                  u, (IR_EmbeddedCodePayload){.source = NULL}) >= 0);
  t_emit(u, (IR_Inst){.op = IR_OP_ITEM_SAVE_CODE, .a = 0});
  assert_preflight_rejects(u, ERR_COMP_SYNTAX,
                           "inconsistent source or parameters");

  u = ir_create_unit();
  ASSERT_TRUE(ir_add_embedded_code_payload(
                  u, (IR_EmbeddedCodePayload){.source = NULL}) >= 0);
  t_emit(u, (IR_Inst){.op = IR_OP_HALT});
  assert_preflight_rejects(u, ERR_COMP_SYNTAX,
                           "inconsistent source or parameters");

  u = ir_create_unit();
  ASSERT_TRUE(ir_add_embedded_code_payload(
                  u, (IR_EmbeddedCodePayload){.source = "x",
                                              .param_count = 1}) >= 0);
  t_emit(u, (IR_Inst){.op = IR_OP_ITEM_SAVE_CODE, .a = 0});
  assert_preflight_rejects(u, ERR_COMP_SYNTAX,
                           "inconsistent source or parameters");

  u = ir_create_unit();
  const char **null_params = calloc(1, sizeof(*null_params));
  ASSERT_NOT_NULL(null_params);
  ASSERT_TRUE(ir_add_embedded_code_payload(
                  u, (IR_EmbeddedCodePayload){.source = "x",
                                              .param_count = 1,
                                              .params = null_params}) >= 0);
  t_emit(u, (IR_Inst){.op = IR_OP_ITEM_SAVE_CODE, .a = 0});
  assert_preflight_rejects(u, ERR_COMP_SYNTAX,
                           "parameter 0 is null");

  u = ir_create_unit();
  const char **empty_params = malloc(sizeof(*empty_params));
  ASSERT_NOT_NULL(empty_params);
  empty_params[0] = "";
  ASSERT_TRUE(ir_add_embedded_code_payload(
                  u, (IR_EmbeddedCodePayload){.source = "x",
                                              .param_count = 1,
                                              .params = empty_params}) >= 0);
  t_emit(u, (IR_Inst){.op = IR_OP_ITEM_SAVE_CODE, .a = 0});
  assert_preflight_rejects(u, ERR_COMP_SYNTAX,
                           "parameter 0 has invalid length 0");

  u = ir_create_unit();
  int32_t label = ir_new_label(u);
  t_emit(u, (IR_Inst){.op = IR_OP_JUMP, .a = label});
  ASSERT_TRUE(ir_bind_label(u, label));
  ASSERT_EQ_INT(u->function.count, u->labels.entries[label].position);
  assert_preflight_rejects(u, ERR_COMP_SYNTAX, "invalid position");

  char *layer = malloc(257);
  memset(layer, 'x', 256);
  layer[256] = '\0';
  u = ir_create_unit();
  t_emit(u, (IR_Inst){.op = IR_OP_ITEM_PUSH_LAYER, .imm = (int64_t)(intptr_t)layer});
  assert_preflight_rejects(u, ERR_COMP_SYNTAX,
                           "string payload is too long");
  free(layer);

  char *oversized = malloc((size_t)UINT16_MAX + 2);
  ASSERT_NOT_NULL(oversized);
  memset(oversized, 'x', (size_t)UINT16_MAX + 1);
  oversized[(size_t)UINT16_MAX + 1] = '\0';

  u = ir_create_unit();
  t_emit(u, (IR_Inst){.op = IR_OP_PUSH_STRING,
                      .imm = (int64_t)(intptr_t)oversized});
  assert_preflight_rejects(u, ERR_COMP_SYNTAX,
                           "string payload is too long");

  u = ir_create_unit();
  ASSERT_TRUE(ir_add_embedded_code_payload(
                  u, (IR_EmbeddedCodePayload){.source = oversized}) >= 0);
  t_emit(u, (IR_Inst){.op = IR_OP_ITEM_SAVE_CODE, .a = 0});
  assert_preflight_rejects(u, ERR_COMP_SYNTAX,
                           "source is too long");

  u = ir_create_unit();
  const char **oversized_params = malloc(sizeof(*oversized_params));
  ASSERT_NOT_NULL(oversized_params);
  oversized_params[0] = oversized;
  ASSERT_TRUE(ir_add_embedded_code_payload(
                  u, (IR_EmbeddedCodePayload){.source = "x",
                                              .param_count = 1,
                                              .params = oversized_params}) >=
              0);
  t_emit(u, (IR_Inst){.op = IR_OP_ITEM_SAVE_CODE, .a = 0});
  assert_preflight_rejects(u, ERR_COMP_SYNTAX,
                           "parameter 0 has invalid length");
  free(oversized);
}

static void test_emitbc_checked_size_boundaries(void) {
  size_t total = (size_t)UINT32_MAX - 1;
  ASSERT_TRUE(emitbc_checked_size_add(&total, 1));
  ASSERT_EQ_INT(UINT32_MAX, total);

  ASSERT_TRUE(!emitbc_checked_size_add(&total, 1));
  ASSERT_EQ_INT(UINT32_MAX, total);

  total = SIZE_MAX;
  ASSERT_TRUE(!emitbc_checked_size_add(&total, 1));
  ASSERT_TRUE(total == SIZE_MAX);
}

static uint32_t lcg_next(uint32_t *state) {
  *state = (*state * 1664525u) + 1013904223u;
  return *state;
}

static OUTPUT_t make_out(size_t cap) {
  OUTPUT_t out = {0};
  out.maxsize = cap;
  out.bytecode = malloc(cap);
  out.nextbyte = out.bytecode;
  ASSERT_NOT_NULL(out.bytecode);
  return out;
}

static void test_emitbc_accepts_unencoded_embedded_locals(void) {
  IR_Unit *u = t_new_unit();
  ASSERT_NOT_NULL(u);
  int32_t payload = ir_add_embedded_code_payload(
      u, (IR_EmbeddedCodePayload){.source = "return 1;", .local_count = 1,
                                  .locals = NULL});
  ASSERT_TRUE(payload >= 0);
  t_emit(u, (IR_Inst){.op = IR_OP_PUSH_STRING,
                      .imm = (int64_t)(intptr_t)"target"});
  t_emit(u, (IR_Inst){.op = IR_OP_ITEM_SAVE_CODE, .a = payload});
  t_emit(u, (IR_Inst){.op = IR_OP_HALT});

  OUTPUT_t out = make_out(32);
  CompilerDiagnostic diag;
  compiler_diag_init(&diag);
  ASSERT_EQ_INT(ERR_NOERROR, t_emit_bytecode_diag(u, 0, 0, &out, &diag));
  ASSERT_TRUE(diag.message == NULL);
  compiler_diag_reset(&diag);

  free(out.bytecode);
  ir_destroy_unit(u);
}

static void test_emitbc_accepts_unreferenced_terminal_label(void) {
  IR_Unit *u = t_new_unit();
  ASSERT_NOT_NULL(u);
  t_emit(u, (IR_Inst){.op = IR_OP_HALT});
  int32_t terminal = ir_new_label(u);
  ASSERT_TRUE(terminal >= 0);
  ASSERT_TRUE(ir_bind_label(u, terminal));
  ASSERT_EQ_INT(u->function.count, u->labels.entries[terminal].position);

  OUTPUT_t out = make_out(16);
  CompilerDiagnostic diag;
  compiler_diag_init(&diag);
  ASSERT_EQ_INT(ERR_NOERROR, t_emit_bytecode_diag(u, 0, 0, &out, &diag));
  ASSERT_TRUE(diag.message == NULL);
  compiler_diag_reset(&diag);

  free(out.bytecode);
  ir_destroy_unit(u);
}

static void assert_class_layout(IR_Inst inst, size_t expected_body_len, size_t expected_arity_bytes,
                                int expect_jump_width) {
  IR_Unit *unit = t_new_unit();
  ASSERT_NOT_NULL(unit);
  if (inst.op == IR_OP_JUMP || inst.op == IR_OP_JUMP_IF_FALSE) {
    int32_t l = ir_new_label(unit);
    inst.a = l;
    t_emit(unit, inst);
    t_bind(unit, l);
    t_emit(unit, (IR_Inst){.op = IR_OP_LABEL, .a = l});
  } else {
    t_emit(unit, inst);
  }
  if (inst.op != IR_OP_HALT) {
    t_emit(unit, (IR_Inst){.op = IR_OP_HALT});
  }

  OUTPUT_t out = make_out(16);
  CompilerDiagnostic diag;
  compiler_diag_init(&diag);
  int8_t rc = t_emit_bytecode_diag(unit, UINT8_MAX, UINT8_MAX, &out, &diag);
  ASSERT_EQ_INT(ERR_NOERROR, rc);
  ASSERT_TRUE(diag.message == NULL);

  size_t len = (size_t)(out.nextbyte - out.bytecode);
  ASSERT_EQ_INT(UINT8_MAX, out.bytecode[6]);
  ASSERT_EQ_INT(UINT8_MAX, out.bytecode[7]);
  ASSERT_EQ_INT(BC_V1_HEADER_SIZE + expected_body_len + (inst.op == IR_OP_HALT ? 0 : 1), len);

  if (expect_jump_width) {
    ASSERT_TRUE(len >= 5);
    /* jump offset is always 16-bit little-endian. */
    (void)(out.bytecode[BC_V1_HEADER_SIZE + 1] | (out.bytecode[BC_V1_HEADER_SIZE + 2] << 8));
  }
  if (expected_arity_bytes > 0) {
    ASSERT_TRUE(len >= 3 + expected_arity_bytes);
  }

  free(out.bytecode);
  ir_destroy_unit(unit);
  compiler_diag_reset(&diag);
}

static void test_emitbc_op_class_invariants(void) {
  assert_class_layout((IR_Inst){.op = IR_OP_HALT}, 1, 0, 0);
  assert_class_layout((IR_Inst){.op = IR_OP_PUSH_INT, .imm = 123}, 9, 0, 0);
  assert_class_layout((IR_Inst){.op = IR_OP_PUSH_FLOAT, .imm = (int64_t)UINT64_C(0x7ff8000000000042)}, 9, 0, 0);
  assert_class_layout((IR_Inst){.op = IR_OP_PUSH_STRING, .imm = (int64_t)(intptr_t)"abc"}, 6, 0, 0);
  assert_class_layout((IR_Inst){.op = IR_OP_ADD}, 1, 0, 0);
  assert_class_layout((IR_Inst){.op = IR_OP_LOAD_LOCAL, .a = 4}, 2, 1, 0);
  assert_class_layout((IR_Inst){.op = IR_OP_LIBCALL, .a = 1, .b = 0}, 3, 2, 1);
  assert_class_layout((IR_Inst){.op = IR_OP_CALL, .a = 2}, 3, 2, 0);
  assert_class_layout((IR_Inst){.op = IR_OP_JUMP}, 3, 2, 1);
  assert_class_layout((IR_Inst){.op = IR_OP_JUMP_IF_FALSE}, 3, 2, 1);

  IR_Unit *u = t_new_unit();
  ASSERT_NOT_NULL(u);
  IR_EmbeddedCodePayload payload = {.source = "code"};
  int32_t idx = ir_add_embedded_code_payload(u, payload);
  t_emit(u, (IR_Inst){.op = IR_OP_ITEM_SAVE_CODE, .a = idx});
  t_emit(u, (IR_Inst){.op = IR_OP_HALT});
  OUTPUT_t out = make_out(16);
  CompilerDiagnostic diag;
  compiler_diag_init(&diag);
  ASSERT_EQ_INT(ERR_NOERROR, t_emit_bytecode_diag(u, 2, 2, &out, &diag));
  ASSERT_TRUE(diag.message == NULL);
  ASSERT_EQ_INT(2, out.bytecode[6]);
  ASSERT_EQ_INT(2, out.bytecode[7]);
  ASSERT_EQ_INT(BC_V1_HEADER_SIZE + 1 + 1 + 2 + 2 + 4 + 1,
                (size_t)(out.nextbyte - out.bytecode));
  free(out.bytecode);
  ir_destroy_unit(u);
  compiler_diag_reset(&diag);
}

static void test_embedded_code_canonical_boundaries(void) {
  const size_t lengths[] = {0x50u, 0x150u, 0xff50u};
  for (size_t i = 0; i < sizeof lengths / sizeof lengths[0]; i++) {
    IR_Unit *u = t_new_unit();
    ASSERT_NOT_NULL(u);
    char *src = malloc(lengths[i] + 1u);
    ASSERT_NOT_NULL(src);
    memcpy(src, "return 7;", 9);
    memset(src + 9, ' ', lengths[i] - 9);
    src[lengths[i]] = '\0';
    int32_t idx = ir_add_embedded_code_payload(
        u, (IR_EmbeddedCodePayload){.source = src});
    ASSERT_TRUE(idx >= 0);
    t_emit(u, (IR_Inst){.op = IR_OP_PUSH_STRING,
                        .imm = (int64_t)(intptr_t)"boundary.target"});
    t_emit(u, (IR_Inst){.op = IR_OP_ITEM_SAVE_CODE, .a = idx});
    t_emit(u, (IR_Inst){.op = IR_OP_HALT});
    OUTPUT_t out = make_out(lengths[i] + 64u);
    CompilerDiagnostic diag;
    compiler_diag_init(&diag);
    ASSERT_EQ_INT(ERR_NOERROR,
                  t_emit_bytecode_diag(u, 0, 0, &out, &diag));
    ASSERT_TRUE(diag.message == NULL);
    size_t n = (size_t)(out.nextbyte - out.bytecode);
    size_t embedded = BC_V1_HEADER_SIZE + 1u + 2u + strlen("boundary.target");
    ASSERT_EQ_INT('B', out.bytecode[embedded]);
    ASSERT_EQ_INT('P', out.bytecode[embedded + 1u]);
    ASSERT_EQ_INT(0, out.bytecode[embedded + 2u]);
    ASSERT_EQ_INT(0, out.bytecode[embedded + 3u]);
    ASSERT_EQ_INT(lengths[i], bc_wire_load_u16(out.bytecode + embedded + 4u));
    ASSERT_EQ_INT(BC_VERIFY_OK,
                  bc_verify_bytecode(out.bytecode, (uint32_t)n, "boundary",
                                     NULL).status);
    SDissResult shown = sdiss_disassemble_bytes(
        out.bytecode, (uint32_t)n, &(SDissOptions){.no_header = 1}, NULL,
        NULL);
    ASSERT_EQ_INT(BC_VERIFY_OK, shown.status);
    ASSERT_EQ_INT(2, shown.instruction_count);
    BC_ConvertResult c = bc_convert_latest(out.bytecode, (uint32_t)n);
    ASSERT_EQ_INT(BC_CONVERT_SUCCESS, c.status);
    ASSERT_EQ_INT(n, c.length);
    ASSERT_EQ_INT(0, memcmp(out.bytecode, c.data, n));
    bc_convert_result_free(&c);
    free(out.bytecode);
    compiler_diag_reset(&diag);
    free(src);
    ir_destroy_unit(u);
  }
}

static void test_embedded_code_parameterized_compatibility(void) {
  IR_Unit *u = t_new_unit();
  ASSERT_NOT_NULL(u);
  const char **params = malloc(sizeof(*params));
  ASSERT_NOT_NULL(params);
  params[0] = "arg";
  int32_t idx = ir_add_embedded_code_payload(
      u, (IR_EmbeddedCodePayload){.source = "return @arg;",
                                  .params = params,
                                  .param_count = 1});
  ASSERT_TRUE(idx >= 0);
  t_emit(u, (IR_Inst){.op = IR_OP_PUSH_STRING,
                      .imm = (int64_t)(intptr_t)"parameterized.target"});
  t_emit(u, (IR_Inst){.op = IR_OP_ITEM_SAVE_CODE, .a = idx});
  t_emit(u, (IR_Inst){.op = IR_OP_HALT});
  OUTPUT_t out = make_out(64u);
  CompilerDiagnostic diag;
  compiler_diag_init(&diag);
  ASSERT_EQ_INT(ERR_NOERROR,
                t_emit_bytecode_diag(u, 0, 0, &out, &diag));
  ASSERT_TRUE(diag.message == NULL);
  size_t n = (size_t)(out.nextbyte - out.bytecode);
  ASSERT_EQ_INT(BC_VERIFY_OK,
                bc_verify_bytecode(out.bytecode, (uint32_t)n,
                                   "parameterized", NULL).status);
  BC_ConvertResult converted = bc_convert_latest(out.bytecode, (uint32_t)n);
  ASSERT_EQ_INT(BC_CONVERT_SUCCESS, converted.status);
  ASSERT_EQ_INT(n, converted.length);
  ASSERT_EQ_INT(0, memcmp(out.bytecode, converted.data, n));
  bc_convert_result_free(&converted);
  free(out.bytecode);
  compiler_diag_reset(&diag);
  ir_destroy_unit(u);
}

static void test_compiler_embedded_code_boundary_lengths(void) {
  const size_t lengths[] = {0x50u, 0x150u, 0xff50u};
  const char prefix[] = "boundary.compiled = code (";
  const char suffix[] = ");";
  for (size_t i = 0; i < sizeof(lengths) / sizeof(lengths[0]); i++) {
    size_t program_len = sizeof(prefix) - 1u + lengths[i] +
                         sizeof(suffix) - 1u;
    char *program = malloc(program_len + 1u);
    ASSERT_NOT_NULL(program);
    size_t pos = 0;
    memcpy(program + pos, prefix, sizeof(prefix) - 1u);
    pos += sizeof(prefix) - 1u;
    memcpy(program + pos, "return 7;", 9u);
    memset(program + pos + 9u, ' ', lengths[i] - 9u);
    pos += lengths[i];
    memcpy(program + pos, suffix, sizeof(suffix));

    OUTPUT_t *out = NULL;
    CompilerDiagnostic diag;
    compiler_diag_init(&diag);
    ASSERT_EQ_INT(ERR_NOERROR,
                  compile_source_to_bytecode_diag(program, program_len, &out,
                                                  &diag));
    ASSERT_EQ_INT(ERR_NOERROR, diag.code);
    ASSERT_NOT_NULL(out);
    size_t bytecode_len = (size_t)(out->nextbyte - out->bytecode);
    EmbeddedDecodeCapture capture = {0};
    BC_VerifyResult decoded = bc_decode_bytecode_events(
        out->bytecode, (uint32_t)bytecode_len, "compiled boundary", NULL,
        NULL, capture_embedded_source, &capture);
    ASSERT_EQ_INT(BC_VERIFY_OK, decoded.status);
    ASSERT_EQ_INT(1, capture.embedded_count);
    ASSERT_EQ_INT(lengths[i], capture.source_len);

    free(out->bytecode);
    free(out);
    compiler_diag_reset(&diag);
    free(program);
  }
}

static void emit_random_program(IR_Unit *u, uint32_t *seed, int count) {
  for (int i = 0; i < count; i++) {
    uint32_t r = lcg_next(seed);
    switch (r % 6u) {
      case 0: t_emit(u, (IR_Inst){.op = IR_OP_PUSH_INT, .imm = (int64_t)(r & 0x7FFF)}); break;
      case 1: t_emit(u, (IR_Inst){.op = IR_OP_PUSH_FLOAT, .imm = (int64_t)r}); break;
      case 2: t_emit(u, (IR_Inst){.op = IR_OP_LOAD_LOCAL, .a = (int32_t)(r % 8u)}); break;
      case 3: t_emit(u, (IR_Inst){.op = IR_OP_PUSH_BOOL, .a = (int32_t)(r & 1u)}); break;
      case 4: t_emit(u, (IR_Inst){.op = IR_OP_LIBCALL, .a = 1, .b = (int32_t)(r % 4u)}); break;
      case 5: t_emit(u, (IR_Inst){.op = IR_OP_INC_LOCAL, .a = (int32_t)(r % 8u)}); break;
    }
  }
  t_emit(u, (IR_Inst){.op = IR_OP_HALT});
}

static void test_emitbc_determinism_fixed_seed(void) {
  printf("emitbc invariant seed=0x%08X\n", TEST_SEED);
  IR_Unit *a = t_new_unit();
  IR_Unit *b = t_new_unit();
  ASSERT_NOT_NULL(a);
  ASSERT_NOT_NULL(b);
  uint32_t sa = TEST_SEED;
  uint32_t sb = TEST_SEED;
  emit_random_program(a, &sa, 200);
  emit_random_program(b, &sb, 200);

  OUTPUT_t oa = make_out(32);
  OUTPUT_t ob = make_out(32);
  CompilerDiagnostic diag;
  compiler_diag_init(&diag);
  ASSERT_EQ_INT(ERR_NOERROR, t_emit_bytecode_diag(a, 8, 0, &oa, &diag));
  ASSERT_TRUE(diag.message == NULL);
  ASSERT_EQ_INT(ERR_NOERROR, t_emit_bytecode_diag(b, 8, 0, &ob, &diag));
  ASSERT_TRUE(diag.message == NULL);

  size_t lena = (size_t)(oa.nextbyte - oa.bytecode);
  size_t lenb = (size_t)(ob.nextbyte - ob.bytecode);
  ASSERT_EQ_INT(lena, lenb);
  ASSERT_EQ_INT(0, memcmp(oa.bytecode, ob.bytecode, lena));

  free(oa.bytecode);
  free(ob.bytecode);
  ir_destroy_unit(a);
  ir_destroy_unit(b);
  compiler_diag_reset(&diag);
}

static void test_emitbc_successful_emission_verifies(void) {
  IR_Unit *unit = t_new_unit();
  ASSERT_NOT_NULL(unit);
  t_emit(unit, (IR_Inst){.op = IR_OP_PUSH_INT, .imm = 42});
  t_emit(unit, (IR_Inst){.op = IR_OP_HALT});

  OUTPUT_t out = make_out(16);
  CompilerDiagnostic diag;
  compiler_diag_init(&diag);
  int8_t rc = t_emit_bytecode_diag(unit, 0, 0, &out, &diag);
  ASSERT_EQ_INT(ERR_NOERROR, rc);
  ASSERT_TRUE(diag.message == NULL);
  ASSERT_EQ_INT('h', out.bytecode[(out.nextbyte - out.bytecode) - 1]);

  free(out.bytecode);
  ir_destroy_unit(unit);
  compiler_diag_reset(&diag);
}

static void test_emitbc_label_heavy_jump_targets_in_bounds(void) {
  IR_Unit *u = t_new_unit();
  ASSERT_NOT_NULL(u);
  int32_t labels[32];
  for (int i = 0; i < 32; i++) labels[i] = ir_new_label(u);
  for (int i = 0; i < 32; i++) {
    t_bind(u, labels[i]);
    t_emit(u, (IR_Inst){.op = IR_OP_LABEL, .a = labels[i]});
    t_emit(u, (IR_Inst){.op = IR_OP_PUSH_INT, .imm = i});
    t_emit(u, (IR_Inst){.op = IR_OP_STORE_LOCAL, .a = 0});
    if (i + 1 < 32) {
      t_emit(u, (IR_Inst){.op = IR_OP_JUMP, .a = labels[i + 1]});
    }
  }
  t_emit(u, (IR_Inst){.op = IR_OP_HALT});

  OUTPUT_t out = make_out(128);
  CompilerDiagnostic diag;
  compiler_diag_init(&diag);
  ASSERT_EQ_INT(ERR_NOERROR, t_emit_bytecode_diag(u, 1, 0, &out, &diag));
  ASSERT_TRUE(diag.message == NULL);

  size_t len = (size_t)(out.nextbyte - out.bytecode);
  size_t pc = 2;
  while (pc < len) {
    uint8_t op = out.bytecode[pc++];
    if (op == 'j' || op == 'k') {
      ASSERT_TRUE(pc + 1 < len);
      int16_t diff = (int16_t)((uint16_t)out.bytecode[pc] | ((uint16_t)out.bytecode[pc + 1] << 8));
      long target = (long)pc + (long)diff;
      ASSERT_TRUE(target >= 2);
      ASSERT_TRUE((size_t)target < len);
      pc += 2;
      continue;
    }
    if (op == 'p' || op == 'P') pc += 8;
    else if (op == 'l' || op == 'B') {
      uint16_t n = (uint16_t)(((uint16_t)out.bytecode[pc]) | ((uint16_t)out.bytecode[pc + 1] << 8));
      pc += 2 + n;
    } else if (op == 'e' || op == 'c' || op == 'f' || op == 'g') pc += 1;
    else if (op == 'F') pc += 2;
    else if (op == 'M') pc += 1;
    else if (op == 'L') {
      uint8_t n = out.bytecode[pc++];
      pc += n;
    }
  }
  ASSERT_EQ_INT(len, pc);

  free(out.bytecode);
  ir_destroy_unit(u);
  compiler_diag_reset(&diag);
}

void test_emitbc_invariants(void) {
  test_emitbc_preflight_rejects_malformed_ir();
  test_emitbc_checked_size_boundaries();
  test_emitbc_accepts_unencoded_embedded_locals();
  test_emitbc_accepts_unreferenced_terminal_label();
  test_emitbc_op_class_invariants();
  test_embedded_code_canonical_boundaries();
  test_embedded_code_parameterized_compatibility();
  test_compiler_embedded_code_boundary_lengths();
  test_emitbc_determinism_fixed_seed();
  test_emitbc_label_heavy_jump_targets_in_bounds();
  test_emitbc_successful_emission_verifies();
}
