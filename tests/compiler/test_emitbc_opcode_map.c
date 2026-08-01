#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "error.h"
#include "compiler/ir.h"
#include "test_assert.h"
#include "test_helpers.h"
#include "compiler/compiler_pipeline.h"
#include "bytecode_format.h"
#include "bytecode_wire.h"

typedef struct {
  const char *name;
  IR_Op op;
  uint8_t expected_opcode;
  size_t offset;
} OpcodeCase;

static void emit_case_inst(IR_Unit *unit, IR_Op op) {
  switch (op) {
    case IR_OP_PUSH_INT:
      t_emit(unit, (IR_Inst){.op = op, .imm = 42});
      break;
    case IR_OP_PUSH_FLOAT:
      t_emit(unit, (IR_Inst){.op = op, .imm = (int64_t)UINT64_C(0x400921fb54442d18)});
      break;
    case IR_OP_PUSH_STRING:
      t_emit(unit, (IR_Inst){.op = op, .imm = (int64_t)(intptr_t)"x"});
      break;
    case IR_OP_PUSH_BOOL:
      t_emit(unit, (IR_Inst){.op = op, .a = 1});
      break;
    case IR_OP_RETURN:
      t_emit(unit, (IR_Inst){.op = IR_OP_PUSH_BOOL, .a = 1});
      t_emit(unit, (IR_Inst){.op = op});
      break;
    case IR_OP_LOAD_LOCAL:
    case IR_OP_STORE_LOCAL:
    case IR_OP_INC_LOCAL:
    case IR_OP_DEC_LOCAL:
    case IR_OP_CALL:
    case IR_OP_LIBCALL_TOKEN:
      t_emit(unit, (IR_Inst){.op = op, .a = 3});
      break;
    case IR_OP_ITEM_SAVE_CODE: {
      IR_EmbeddedCodePayload payload = {0};
      payload.source = "x";
      int32_t idx = ir_add_embedded_code_payload(unit, payload);
      t_emit(unit, (IR_Inst){.op = op, .a = idx});
      break;
    }
    case IR_OP_JUMP:
    case IR_OP_JUMP_IF_FALSE: {
      int32_t label = ir_new_label(unit);
      t_emit(unit, (IR_Inst){.op = op, .a = label});
      t_bind(unit, label);
      t_emit(unit, (IR_Inst){.op = IR_OP_LABEL, .a = label});
      break;
    }
    case IR_OP_ITEM_PUSH_LAYER:
      t_emit(unit, (IR_Inst){.op = IR_OP_ITEM_BEGIN});
      t_emit(unit, (IR_Inst){.op = op, .imm = (int64_t)(intptr_t)"L"});
      t_emit(unit, (IR_Inst){.op = IR_OP_ITEM_END});
      break;
    case IR_OP_ITEM_PUSH_DEREF:
      t_emit(unit, (IR_Inst){.op = IR_OP_ITEM_BEGIN});
      t_emit(unit, (IR_Inst){.op = op});
      t_emit(unit, (IR_Inst){.op = IR_OP_ITEM_PUSH_DEREF_LOCAL, .a = 0});
      t_emit(unit, (IR_Inst){.op = IR_OP_ITEM_END});
      break;
    case IR_OP_ITEM_PUSH_DEREF_LOCAL:
      t_emit(unit, (IR_Inst){.op = IR_OP_ITEM_BEGIN});
      t_emit(unit, (IR_Inst){.op = IR_OP_ITEM_PUSH_DEREF});
      t_emit(unit, (IR_Inst){.op = op, .a = 0});
      t_emit(unit, (IR_Inst){.op = IR_OP_ITEM_END});
      break;
    case IR_OP_ITEM_END:
      t_emit(unit, (IR_Inst){.op = IR_OP_ITEM_BEGIN});
      t_emit(unit, (IR_Inst){.op = op});
      break;
    case IR_OP_ITEM_BEGIN:
    case IR_OP_ITEM_BEGIN_REL:
      t_emit(unit, (IR_Inst){.op = op});
      t_emit(unit, (IR_Inst){.op = IR_OP_ITEM_END});
      break;
    default:
      t_emit(unit, (IR_Inst){.op = op});
      break;
  }
}

void test_emitbc_opcode_map(void) {
  /* Keep this case list aligned with src/emitbc.c:map_opcode. */
  const OpcodeCase cases[] = {
      {"halt", IR_OP_HALT, 'h', 0},
      {"return", IR_OP_RETURN, 'Q', 2},
      {"push_int", IR_OP_PUSH_INT, 'p', 0},
      {"push_float", IR_OP_PUSH_FLOAT, 'P', 0},
      {"push_bool", IR_OP_PUSH_BOOL, 'b', 0},
      {"push_string", IR_OP_PUSH_STRING, 'l', 0},
      {"push_nil", IR_OP_PUSH_NIL, 'N', 0},
      {"add", IR_OP_ADD, 'a', 0},
      {"sub", IR_OP_SUB, 's', 0},
      {"mul", IR_OP_MUL, 'm', 0},
      {"div", IR_OP_DIV, 'd', 0},
      {"mod", IR_OP_MOD, '%', 0},
      {"neg", IR_OP_NEG, 'n', 0},
      {"eq", IR_OP_EQ, 'o', 0},
      {"neq", IR_OP_NEQ, 'q', 0},
      {"lt", IR_OP_LT, 'r', 0},
      {"gt", IR_OP_GT, 't', 0},
      {"le", IR_OP_LE, 'u', 0},
      {"ge", IR_OP_GE, 'v', 0},
      {"not", IR_OP_NOT, 'x', 0},
      {"and", IR_OP_AND, 'y', 0},
      {"or", IR_OP_OR, 'z', 0},
      {"discard", IR_OP_DISCARD, 'w', 0},
      {"load_local", IR_OP_LOAD_LOCAL, 'e', 0},
      {"store_local", IR_OP_STORE_LOCAL, 'c', 0},
      {"inc_local", IR_OP_INC_LOCAL, 'f', 0},
      {"dec_local", IR_OP_DEC_LOCAL, 'g', 0},
      {"jump", IR_OP_JUMP, 'j', 0},
      {"jump_if_false", IR_OP_JUMP_IF_FALSE, 'k', 0},
      {"item_begin", IR_OP_ITEM_BEGIN, 'I', 0},
      {"item_begin_rel", IR_OP_ITEM_BEGIN_REL, 'R', 0},
      {"item_push_layer", IR_OP_ITEM_PUSH_LAYER, 'L', 1},
      {"item_push_deref", IR_OP_ITEM_PUSH_DEREF, 'D', 1},
      {"item_push_deref_local", IR_OP_ITEM_PUSH_DEREF_LOCAL, 'V', 2},
      {"item_end", IR_OP_ITEM_END, 'E', 1},
      {"item_deref", IR_OP_ITEM_DEREF, 'F', 0},
      {"item_save", IR_OP_ITEM_SAVE, 'C', 0},
      {"call", IR_OP_CALL, 'F', 0},
      {"libcall_token", IR_OP_LIBCALL_TOKEN, 'M', 0},
      {"item_save_code", IR_OP_ITEM_SAVE_CODE, 'B', 0},
  };

  for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
    const OpcodeCase *tc = &cases[i];
    IR_Unit *unit = t_new_unit();
    ASSERT_NOT_NULL(unit);
    emit_case_inst(unit, tc->op);
    if (tc->op != IR_OP_HALT) {
      t_emit(unit, (IR_Inst){.op = IR_OP_HALT});
    }

    OUTPUT_t out = {0};
    out.maxsize = 8;
    out.bytecode = malloc(out.maxsize);
    out.nextbyte = out.bytecode;
    ASSERT_NOT_NULL(out.bytecode);

    char *errdetail = NULL;
    int8_t rc = t_emit_bytecode(unit, 8, 8, &out, &errdetail);
    if (rc != ERR_NOERROR) {
      TEST_FAILF("opcode case %s failed: %s", tc->name,
                 errdetail ? errdetail : "<no diagnostic>");
    }
    ASSERT_TRUE(errdetail == NULL);
    ASSERT_TRUE((size_t)(out.nextbyte - out.bytecode) > BC_V1_HEADER_SIZE + tc->offset);
    ASSERT_EQ_INT((int)tc->expected_opcode, out.bytecode[BC_V1_HEADER_SIZE + tc->offset]);

    free(out.bytecode);
    ir_destroy_unit(unit);
  }
}

void test_emitbc_lists_and_itemrefs_emission(void) {
  const char *source = "return #[1, 2, &fred];";
  OUTPUT_t *out = NULL;
  char *errdetail = NULL;
  int8_t rc = compile_source_to_bytecode(source, strlen(source), &out, &errdetail);
  if (rc != ERR_NOERROR) fprintf(stderr, "list compile: rc=%d detail=%s\n", rc, errdetail ? errdetail : "<none>");
  ASSERT_EQ_INT(ERR_NOERROR, rc);
  ASSERT_NOT_NULL(out);
  size_t len = (size_t)(out->nextbyte - out->bytecode);
  size_t build = SIZE_MAX;
  size_t ref = SIZE_MAX;
  for (size_t i = BC_V1_HEADER_SIZE; i < len; i++) {
    if (out->bytecode[i] == '[') build = i;
    if (out->bytecode[i] == '&') ref = i;
  }
  ASSERT_TRUE(build != SIZE_MAX);
  ASSERT_TRUE(ref != SIZE_MAX);
  ASSERT_TRUE(ref < build);
  ASSERT_TRUE(build >= 4);
  ASSERT_EQ_INT(3, out->bytecode[build + 1]);
  ASSERT_EQ_INT(0, out->bytecode[build + 2]);
  ASSERT_EQ_INT(0, out->bytecode[build + 3]);
  ASSERT_EQ_INT(0, out->bytecode[build + 4]);
  ASSERT_TRUE(out->bytecode[ref - 1] == 'E');
  ASSERT_TRUE(out->bytecode[ref + 1] == '[' || out->bytecode[ref + 1] == 'Q');
  free(errdetail);
  free(out->bytecode);
  free(out);
}



void test_emitbc_push_float_immediate_layout(void) {
  const uint64_t values[] = {
      UINT64_C(0x3ff0000000000000), /* 1.0 */
      UINT64_C(0x8000000000000000), /* -0.0 */
      UINT64_C(0x7ff0000000000000), /* +inf */
      UINT64_C(0x7ff8000000000042), /* quiet NaN */
      UINT64_C(0xc006000000000000), /* -2.75 */
  };
  const uint8_t expected_values[][8] = {
      {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xf0, 0x3f},
      {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80},
      {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xf0, 0x7f},
      {0x42, 0x00, 0x00, 0x00, 0x00, 0x00, 0xf8, 0x7f},
      {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x06, 0xc0},
  };

  IR_Unit *unit = t_new_unit();
  ASSERT_NOT_NULL(unit);
  for (size_t i = 0; i < sizeof(values) / sizeof(values[0]); i++) {
    t_emit(unit, (IR_Inst){.op = IR_OP_PUSH_FLOAT,
                           .imm = bc_wire_i64_from_bits(values[i])});
  }
  t_emit(unit, (IR_Inst){.op = IR_OP_HALT});

  OUTPUT_t out = {0};
  out.maxsize = 8;
  out.bytecode = malloc(out.maxsize);
  out.nextbyte = out.bytecode;
  ASSERT_NOT_NULL(out.bytecode);

  char *errdetail = NULL;
  int8_t rc = t_emit_bytecode(unit, 0, 0, &out, &errdetail);
  ASSERT_EQ_INT(0, rc);
  ASSERT_TRUE(errdetail == NULL);

  size_t pos = BC_V1_HEADER_SIZE;
  for (size_t i = 0; i < sizeof(values) / sizeof(values[0]); i++) {
    ASSERT_EQ_INT('P', out.bytecode[pos++]);
    ASSERT_TRUE(memcmp(out.bytecode + pos, expected_values[i], 8) == 0);
    pos += 8;
  }
  ASSERT_EQ_INT('h', out.bytecode[pos++]);
  ASSERT_EQ_INT((int)pos, (int)(out.nextbyte - out.bytecode));

  free(out.bytecode);
  ir_destroy_unit(unit);
}

void test_emitbc_push_int_immediate_layout(void) {
  const int64_t values[] = {
      0,
      42,
      -1,
      INT64_C(0x0102030405060708),
      INT64_C(-9223372036854775807) - INT64_C(1),
  };
  const uint8_t expected_values[][8] = {
      {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
      {0x2a, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
      {0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff},
      {0x08, 0x07, 0x06, 0x05, 0x04, 0x03, 0x02, 0x01},
      {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80},
  };

  IR_Unit *unit = t_new_unit();
  ASSERT_NOT_NULL(unit);
  for (size_t i = 0; i < sizeof(values) / sizeof(values[0]); i++) {
    t_emit(unit, (IR_Inst){.op = IR_OP_PUSH_INT, .imm = values[i]});
  }
  t_emit(unit, (IR_Inst){.op = IR_OP_HALT});

  OUTPUT_t out = {0};
  out.maxsize = 8;
  out.bytecode = malloc(out.maxsize);
  out.nextbyte = out.bytecode;
  ASSERT_NOT_NULL(out.bytecode);

  char *errdetail = NULL;
  int8_t rc = t_emit_bytecode(unit, 0, 0, &out, &errdetail);
  ASSERT_EQ_INT(0, rc);
  ASSERT_TRUE(errdetail == NULL);

  size_t pos = BC_V1_HEADER_SIZE;
  for (size_t i = 0; i < sizeof(values) / sizeof(values[0]); i++) {
    ASSERT_EQ_INT('p', out.bytecode[pos++]);
    ASSERT_TRUE(memcmp(out.bytecode + pos, expected_values[i], 8) == 0);
    pos += 8;
  }
  ASSERT_EQ_INT('h', out.bytecode[pos++]);
  ASSERT_EQ_INT((int)pos, (int)(out.nextbyte - out.bytecode));

  free(out.bytecode);
  ir_destroy_unit(unit);
}

void test_emitbc_opcode_map_call_item_deref_alias_layout(void) {
  IR_Unit *unit = t_new_unit();
  ASSERT_NOT_NULL(unit);

  t_emit(unit, (IR_Inst){.op = IR_OP_ITEM_DEREF});
  t_emit(unit, (IR_Inst){.op = IR_OP_CALL, .a = 2});
  t_emit(unit, (IR_Inst){.op = IR_OP_HALT});

  OUTPUT_t out = {0};
  out.maxsize = 8;
  out.bytecode = malloc(out.maxsize);
  out.nextbyte = out.bytecode;
  ASSERT_NOT_NULL(out.bytecode);

  char *errdetail = NULL;
  int8_t rc = t_emit_bytecode(unit, 3, 3, &out, &errdetail);
  ASSERT_EQ_INT(0, rc);
  ASSERT_TRUE(errdetail == NULL);

  /* v1 header, ITEM_DEREF('F', argc=0), CALL('F', argc=2), HALT */
  ASSERT_TRUE((size_t)(out.nextbyte - out.bytecode) >= 9);
  ASSERT_EQ_INT('F', out.bytecode[BC_V1_HEADER_SIZE]);
  ASSERT_EQ_INT(0, out.bytecode[BC_V1_HEADER_SIZE + 1]);
  ASSERT_EQ_INT(0, out.bytecode[BC_V1_HEADER_SIZE + 2]);
  ASSERT_EQ_INT('F', out.bytecode[BC_V1_HEADER_SIZE + 3]);
  ASSERT_EQ_INT(2, out.bytecode[BC_V1_HEADER_SIZE + 4]);
  ASSERT_EQ_INT(0, out.bytecode[BC_V1_HEADER_SIZE + 5]);
  ASSERT_EQ_INT('h', out.bytecode[BC_V1_HEADER_SIZE + 6]);

  free(out.bytecode);
  ir_destroy_unit(unit);
}

void test_emitbc_opcode_map_unsupported_ir_op(void) {
  /* Keep this check aligned with src/emitbc.c:"unsupported IR op" error text. */
  IR_Unit *unit = t_new_unit();
  ASSERT_NOT_NULL(unit);
  t_emit(unit, (IR_Inst){.op = (IR_Op)999});

  OUTPUT_t out = {0};
  out.maxsize = 8;
  out.bytecode = malloc(out.maxsize);
  out.nextbyte = out.bytecode;
  ASSERT_NOT_NULL(out.bytecode);

  char *errdetail = NULL;
  int8_t rc = t_emit_bytecode(unit, 0, 0, &out, &errdetail);
  ASSERT_TRUE(rc != ERR_NOERROR);
  ASSERT_NOT_NULL(errdetail);
  ASSERT_TRUE(strstr(errdetail, "unsupported IR op") != NULL);

  free(errdetail);
  free(out.bytecode);
  ir_destroy_unit(unit);
}
