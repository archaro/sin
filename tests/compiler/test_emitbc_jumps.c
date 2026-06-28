#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "error.h"
#include "ir.h"
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
  char *errdetail = NULL;
  int8_t rc = t_emit_bytecode(unit, 0, 0, &out, &errdetail);
  ASSERT_EQ_INT(ERR_NOERROR, rc);
  ASSERT_TRUE(errdetail == NULL);

  ASSERT_EQ_INT('j', out.bytecode[2]);
  ASSERT_EQ_INT(0x0B, out.bytecode[3]);
  ASSERT_EQ_INT(0x00, out.bytecode[4]);

  ASSERT_EQ_INT('k', out.bytecode[16]);
  ASSERT_EQ_INT(0x02, out.bytecode[17]);
  ASSERT_EQ_INT(0x00, out.bytecode[18]);

  free(out.bytecode);
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
  char *errdetail = NULL;
  int8_t rc = t_emit_bytecode(unit, 0, 0, &out, &errdetail);
  ASSERT_EQ_INT(ERR_NOERROR, rc);
  ASSERT_TRUE(errdetail == NULL);

  ASSERT_EQ_INT('k', out.bytecode[11]);
  ASSERT_EQ_INT(0xF6, out.bytecode[12]);
  ASSERT_EQ_INT(0xFF, out.bytecode[13]);

  free(out.bytecode);
  ir_destroy_unit(unit);
}

static void test_emitbc_jump_label_errors(void) {
  IR_Unit *unit = t_new_unit();
  OUTPUT_t out = t_out();
  char *errdetail = NULL;

  t_emit(unit, (IR_Inst){.op = IR_OP_JUMP, .a = -1});
  ASSERT_TRUE(t_emit_bytecode(unit, 0, 0, &out, &errdetail) != ERR_NOERROR);
  ASSERT_NOT_NULL(errdetail);
  ASSERT_TRUE(strstr(errdetail, "jump invalid label id") != NULL);
  free(errdetail);
  errdetail = NULL;
  ir_destroy_unit(unit);

  unit = t_new_unit();
  t_emit(unit, (IR_Inst){.op = IR_OP_JUMP_IF_FALSE, .a = 999});
  ASSERT_TRUE(t_emit_bytecode(unit, 0, 0, &out, &errdetail) != ERR_NOERROR);
  ASSERT_NOT_NULL(errdetail);
  ASSERT_TRUE(strstr(errdetail, "jump invalid label id") != NULL);
  free(errdetail);
  errdetail = NULL;
  ir_destroy_unit(unit);

  unit = t_new_unit();
  int32_t unbound = ir_new_label(unit);
  t_emit(unit, (IR_Inst){.op = IR_OP_JUMP, .a = unbound});
  ASSERT_TRUE(t_emit_bytecode(unit, 0, 0, &out, &errdetail) != ERR_NOERROR);
  ASSERT_NOT_NULL(errdetail);
  ASSERT_TRUE(strstr(errdetail, "jump unbound label") != NULL);
  free(errdetail);

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
  char *errdetail = NULL;
  ASSERT_TRUE(t_emit_bytecode(unit, 0, 0, &out, &errdetail) != ERR_NOERROR);
  ASSERT_NOT_NULL(errdetail);
  ASSERT_TRUE(strstr(errdetail, "jump offset out of range") != NULL);
  free(errdetail);
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
  errdetail = NULL;
  ASSERT_TRUE(t_emit_bytecode(unit, 0, 0, &out, &errdetail) != ERR_NOERROR);
  ASSERT_NOT_NULL(errdetail);
  ASSERT_TRUE(strstr(errdetail, "jump offset out of range") != NULL);

  free(errdetail);
  free(out.bytecode);
  ir_destroy_unit(unit);
}

void test_emitbc_jumps(void) {
  test_emitbc_jump_forward_offsets();
  test_emitbc_jump_backward_offset_negative();
  test_emitbc_jump_label_errors();
  test_emitbc_jump_offset_out_of_range();
}
