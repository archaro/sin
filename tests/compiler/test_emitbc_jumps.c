#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "error.h"
#include "compiler/compdiag.h"
#include "compiler/ir.h"
#include "test_assert.h"
#include "test_helpers.h"

static OUTPUT_t t_out(void) {
  OUTPUT_t out = {0};
  out.maxsize = 64;
  out.bytecode = malloc(out.maxsize);
  out.nextbyte = out.bytecode;
  ASSERT_NOT_NULL(out.bytecode);
  return out;
}

static void test_emitbc_jump_forward_offsets(void) {
  IR_Unit *unit = t_new_unit();
  ASSERT_NOT_NULL(unit);

  int32_t l1 = ir_new_label(unit);
  int32_t l2 = ir_new_label(unit);

  t_emit(unit, (IR_Inst){.op = IR_OP_JUMP, .a = l1});
  t_emit(unit, (IR_Inst){.op = IR_OP_PUSH_INT, .imm = 99});
  t_bind(unit, l1);
  t_emit(unit, (IR_Inst){.op = IR_OP_LABEL, .a = l1});

  t_emit(unit, (IR_Inst){.op = IR_OP_PUSH_BOOL, .a = 1});
  t_emit(unit, (IR_Inst){.op = IR_OP_JUMP_IF_FALSE, .a = l2});
  t_bind(unit, l2);
  t_emit(unit, (IR_Inst){.op = IR_OP_LABEL, .a = l2});
  t_emit(unit, (IR_Inst){.op = IR_OP_HALT});

  OUTPUT_t out = t_out();
  CompilerDiagnostic diag;
  compiler_diag_init(&diag);
  int8_t rc = t_emit_bytecode_diag(unit, 0, 0, &out, &diag);
  ASSERT_EQ_INT(ERR_NOERROR, rc);
  ASSERT_TRUE(diag.message == NULL);

  ASSERT_EQ_INT('j', out.bytecode[8]);
  ASSERT_EQ_INT(0x0B, out.bytecode[9]);
  ASSERT_EQ_INT(0x00, out.bytecode[10]);

  ASSERT_EQ_INT('k', out.bytecode[22]);
  ASSERT_EQ_INT(0x02, out.bytecode[23]);
  ASSERT_EQ_INT(0x00, out.bytecode[24]);

  free(out.bytecode);
  compiler_diag_reset(&diag);
  ir_destroy_unit(unit);
}

static void test_emitbc_jump_backward_offset_negative(void) {
  IR_Unit *unit = t_new_unit();
  ASSERT_NOT_NULL(unit);

  int32_t loop = ir_new_label(unit);
  t_bind(unit, loop);
  t_emit(unit, (IR_Inst){.op = IR_OP_LABEL, .a = loop});
  t_emit(unit, (IR_Inst){.op = IR_OP_PUSH_INT, .imm = 7});
  t_emit(unit, (IR_Inst){.op = IR_OP_JUMP_IF_FALSE, .a = loop});
  t_emit(unit, (IR_Inst){.op = IR_OP_HALT});

  OUTPUT_t out = t_out();
  CompilerDiagnostic diag;
  compiler_diag_init(&diag);
  int8_t rc = t_emit_bytecode_diag(unit, 0, 0, &out, &diag);
  ASSERT_EQ_INT(ERR_NOERROR, rc);
  ASSERT_TRUE(diag.message == NULL);

  ASSERT_EQ_INT('k', out.bytecode[17]);
  ASSERT_EQ_INT(0xF6, out.bytecode[18]);
  ASSERT_EQ_INT(0xFF, out.bytecode[19]);

  free(out.bytecode);
  compiler_diag_reset(&diag);
  ir_destroy_unit(unit);
}

static void test_emitbc_jump_label_errors(void) {
  IR_Unit *unit = t_new_unit();
  OUTPUT_t out = t_out();
  CompilerDiagnostic diag;
  compiler_diag_init(&diag);

  t_emit(unit, (IR_Inst){.op = IR_OP_JUMP, .a = -1});
  ASSERT_TRUE(t_emit_bytecode_diag(unit, 0, 0, &out, &diag) != ERR_NOERROR);
  ASSERT_NOT_NULL(diag.message);
  ASSERT_TRUE(strstr(diag.message, "jump invalid label id") != NULL);
  compiler_diag_reset(&diag);
  ir_destroy_unit(unit);

  unit = t_new_unit();
  t_emit(unit, (IR_Inst){.op = IR_OP_JUMP_IF_FALSE, .a = 999});
  ASSERT_TRUE(t_emit_bytecode_diag(unit, 0, 0, &out, &diag) != ERR_NOERROR);
  ASSERT_NOT_NULL(diag.message);
  ASSERT_TRUE(strstr(diag.message, "jump invalid label id") != NULL);
  compiler_diag_reset(&diag);
  ir_destroy_unit(unit);

  unit = t_new_unit();
  int32_t unbound = ir_new_label(unit);
  t_emit(unit, (IR_Inst){.op = IR_OP_JUMP, .a = unbound});
  ASSERT_TRUE(t_emit_bytecode_diag(unit, 0, 0, &out, &diag) != ERR_NOERROR);
  ASSERT_NOT_NULL(diag.message);
  ASSERT_TRUE(strstr(diag.message, "jump unbound label") != NULL);
  compiler_diag_reset(&diag);

  free(out.bytecode);
  ir_destroy_unit(unit);
}

static void test_emitbc_jump_offset_out_of_range(void) {
  const int filler_count = 3641;
  IR_Unit *unit = t_new_unit();
  ASSERT_NOT_NULL(unit);

  int32_t far = ir_new_label(unit);
  t_emit(unit, (IR_Inst){.op = IR_OP_JUMP, .a = far});
  for (int i = 0; i < filler_count; i++) {
    t_emit(unit, (IR_Inst){.op = IR_OP_PUSH_INT, .imm = i});
  }
  t_bind(unit, far);
  t_emit(unit, (IR_Inst){.op = IR_OP_LABEL, .a = far});

  OUTPUT_t out = t_out();
  CompilerDiagnostic diag;
  compiler_diag_init(&diag);
  ASSERT_TRUE(t_emit_bytecode_diag(unit, 0, 0, &out, &diag) != ERR_NOERROR);
  ASSERT_NOT_NULL(diag.message);
  ASSERT_TRUE(strstr(diag.message, "jump offset out of range") != NULL);
  compiler_diag_reset(&diag);
  free(out.bytecode);
  ir_destroy_unit(unit);

  unit = t_new_unit();
  ASSERT_NOT_NULL(unit);
  int32_t start = ir_new_label(unit);
  t_bind(unit, start);
  t_emit(unit, (IR_Inst){.op = IR_OP_LABEL, .a = start});
  for (int i = 0; i < filler_count; i++) {
    t_emit(unit, (IR_Inst){.op = IR_OP_PUSH_INT, .imm = i});
  }
  t_emit(unit, (IR_Inst){.op = IR_OP_JUMP_IF_FALSE, .a = start});

  out = t_out();
  compiler_diag_reset(&diag);
  ASSERT_TRUE(t_emit_bytecode_diag(unit, 0, 0, &out, &diag) != ERR_NOERROR);
  ASSERT_NOT_NULL(diag.message);
  ASSERT_TRUE(strstr(diag.message, "jump offset out of range") != NULL);

  compiler_diag_reset(&diag);
  free(out.bytecode);
  ir_destroy_unit(unit);
}

void test_emitbc_jumps(void) {
  test_emitbc_jump_forward_offsets();
  test_emitbc_jump_backward_offset_negative();
  test_emitbc_jump_label_errors();
  test_emitbc_jump_offset_out_of_range();
}
