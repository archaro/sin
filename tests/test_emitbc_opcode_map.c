#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "error.h"
#include "ir.h"
#include "test_assert.h"
#include "test_helpers.h"

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
    case IR_OP_PUSH_STRING:
      t_emit(unit, (IR_Inst){.op = op, .imm = (int64_t)(intptr_t)"x"});
      break;
    case IR_OP_LOAD_LOCAL:
    case IR_OP_STORE_LOCAL:
    case IR_OP_INC_LOCAL:
    case IR_OP_DEC_LOCAL:
    case IR_OP_CALL:
      t_emit(unit, (IR_Inst){.op = op, .a = 3});
      break;
    case IR_OP_LIBCALL:
      t_emit(unit, (IR_Inst){.op = op, .a = 1, .b = 1});
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
      t_emit(unit, (IR_Inst){.op = op, .imm = (int64_t)(intptr_t)"L"});
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
      {"push_int", IR_OP_PUSH_INT, 'p', 0},
      {"push_string", IR_OP_PUSH_STRING, 'l', 0},
      {"add", IR_OP_ADD, 'a', 0},
      {"sub", IR_OP_SUB, 's', 0},
      {"mul", IR_OP_MUL, 'm', 0},
      {"div", IR_OP_DIV, 'd', 0},
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
      {"load_local", IR_OP_LOAD_LOCAL, 'e', 0},
      {"store_local", IR_OP_STORE_LOCAL, 'c', 0},
      {"inc_local", IR_OP_INC_LOCAL, 'f', 0},
      {"dec_local", IR_OP_DEC_LOCAL, 'g', 0},
      {"jump", IR_OP_JUMP, 'j', 0},
      {"jump_if_false", IR_OP_JUMP_IF_FALSE, 'k', 0},
      {"item_begin", IR_OP_ITEM_BEGIN, 'I', 0},
      {"item_push_layer", IR_OP_ITEM_PUSH_LAYER, 'L', 0},
      {"item_push_deref", IR_OP_ITEM_PUSH_DEREF, 'D', 0},
      {"item_end", IR_OP_ITEM_END, 'E', 0},
      {"item_deref", IR_OP_ITEM_DEREF, 'F', 0},
      {"item_save", IR_OP_ITEM_SAVE, 'C', 0},
      {"exists", IR_OP_EXISTS, 'X', 0},
      {"delete", IR_OP_DELETE, 'W', 0},
      {"nthname", IR_OP_NTHNAME, 'Y', 0},
      {"rootname", IR_OP_ROOTNAME, 'Z', 0},
      {"call", IR_OP_CALL, 'F', 0},
      {"libcall", IR_OP_LIBCALL, 'A', 0},
      {"item_save_code", IR_OP_ITEM_SAVE_CODE, 'B', 0},
  };

  for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
    const OpcodeCase *tc = &cases[i];
    IR_Unit *unit = t_new_unit();
    ASSERT_NOT_NULL(unit);
    emit_case_inst(unit, tc->op);

    OUTPUT_t out = {0};
    out.maxsize = 8;
    out.bytecode = malloc(out.maxsize);
    out.nextbyte = out.bytecode;
    ASSERT_NOT_NULL(out.bytecode);

    char *errdetail = NULL;
    int8_t rc = t_emit_bytecode(unit, 0, 0, &out, &errdetail);
    ASSERT_EQ_INT(0, rc);
    ASSERT_TRUE(errdetail == NULL);
    ASSERT_TRUE((size_t)(out.nextbyte - out.bytecode) > 2 + tc->offset);
    ASSERT_EQ_INT((int)tc->expected_opcode, out.bytecode[2 + tc->offset]);

    free(out.bytecode);
    ir_destroy_unit(unit);
  }
}


void test_emitbc_opcode_map_call_item_deref_alias_layout(void) {
  IR_Unit *unit = t_new_unit();
  ASSERT_NOT_NULL(unit);

  t_emit(unit, (IR_Inst){.op = IR_OP_ITEM_DEREF});
  t_emit(unit, (IR_Inst){.op = IR_OP_CALL, .a = 2});

  OUTPUT_t out = {0};
  out.maxsize = 8;
  out.bytecode = malloc(out.maxsize);
  out.nextbyte = out.bytecode;
  ASSERT_NOT_NULL(out.bytecode);

  char *errdetail = NULL;
  int8_t rc = t_emit_bytecode(unit, 0, 0, &out, &errdetail);
  ASSERT_EQ_INT(0, rc);
  ASSERT_TRUE(errdetail == NULL);

  /* header[2], ITEM_DEREF('F'), CALL('F'), CALL argc byte */
  ASSERT_TRUE((size_t)(out.nextbyte - out.bytecode) >= 5);
  ASSERT_EQ_INT('F', out.bytecode[2]);
  ASSERT_EQ_INT('F', out.bytecode[3]);
  ASSERT_EQ_INT(2, out.bytecode[4]);

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
