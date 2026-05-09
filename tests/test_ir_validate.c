#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "error.h"
#include "ir.h"
#include "test_assert.h"
#include "test_helpers.h"

static void assert_validate_error(IR_Unit *unit, uint32_t local_count,
                                  int8_t expected_code,
                                  const char *expected_substring) {
  char *errdetail = NULL;
  int8_t rc = ir_validate(unit, local_count, &errdetail);
  ASSERT_EQ_INT(expected_code, rc);
  ASSERT_NOT_NULL(errdetail);
  ASSERT_TRUE(strstr(errdetail, expected_substring) != NULL);
  free(errdetail);
}

static void test_ir_validate_ok_case(void) {
  IR_Unit *unit = t_new_unit();
  ASSERT_NOT_NULL(unit);

  int32_t done = ir_new_label(unit);

  t_emit(unit, (IR_Inst){.op = IR_OP_PUSH_STRING, .imm = (int64_t)(intptr_t)"std"});
  t_emit(unit, (IR_Inst){.op = IR_OP_PUSH_STRING, .imm = (int64_t)(intptr_t)"f"});
  t_emit(unit, (IR_Inst){.op = IR_OP_LIBCALL, .a = 0});
  t_emit(unit, (IR_Inst){.op = IR_OP_LOAD_LOCAL, .a = 1});
  t_emit(unit, (IR_Inst){.op = IR_OP_CALL, .a = 2});
  t_emit(unit, (IR_Inst){.op = IR_OP_JUMP, .a = done});
  t_bind(unit, done);
  t_emit(unit, (IR_Inst){.op = IR_OP_LABEL, .a = done});

  char *errdetail = NULL;
  ASSERT_EQ_INT(ERR_NOERROR, ir_validate(unit, 2, &errdetail));
  ASSERT_TRUE(errdetail == NULL);

  ir_destroy_unit(unit);
}

static void test_ir_validate_unbound_label_rejected(void) {
  IR_Unit *unit = t_new_unit();
  int32_t l0 = ir_new_label(unit);
  t_emit(unit, (IR_Inst){.op = IR_OP_JUMP, .a = l0});

  assert_validate_error(unit, 0, ERR_COMP_SYNTAX, "Unbound label");
  ir_destroy_unit(unit);
}

static void test_ir_validate_invalid_label_ids_rejected(void) {
  IR_Unit *unit = t_new_unit();
  t_emit(unit, (IR_Inst){.op = IR_OP_JUMP, .a = -1});
  assert_validate_error(unit, 0, ERR_COMP_SYNTAX, "references invalid label id");
  ir_destroy_unit(unit);

  unit = t_new_unit();
  int32_t l0 = ir_new_label(unit);
  t_bind(unit, l0);
  t_emit(unit, (IR_Inst){.op = IR_OP_LABEL, .a = 999});
  assert_validate_error(unit, 0, ERR_COMP_SYNTAX, "references invalid label id");
  ir_destroy_unit(unit);
}

static void test_ir_validate_local_index_out_of_range_rejected(void) {
  IR_Unit *unit = t_new_unit();
  t_emit(unit, (IR_Inst){.op = IR_OP_STORE_LOCAL, .a = 2});

  assert_validate_error(unit, 2, ERR_COMP_LOCALBEFOREDEF, "out-of-range local index");
  ir_destroy_unit(unit);
}

static void test_ir_validate_negative_arity_rejected(void) {
  IR_Unit *unit = t_new_unit();
  t_emit(unit, (IR_Inst){.op = IR_OP_CALL, .a = -1});
  assert_validate_error(unit, 0, ERR_COMP_TOOMANYARGS, "negative arity");
  ir_destroy_unit(unit);

  unit = t_new_unit();
  t_emit(unit, (IR_Inst){.op = IR_OP_PUSH_STRING, .imm = (int64_t)(intptr_t)"lib"});
  t_emit(unit, (IR_Inst){.op = IR_OP_PUSH_STRING, .imm = (int64_t)(intptr_t)"func"});
  t_emit(unit, (IR_Inst){.op = IR_OP_LIBCALL, .a = -2});
  assert_validate_error(unit, 0, ERR_COMP_TOOMANYARGS, "negative arity");
  ir_destroy_unit(unit);
}

static void test_ir_validate_libcall_requires_two_push_string_operands(void) {
  IR_Unit *unit = t_new_unit();
  t_emit(unit, (IR_Inst){.op = IR_OP_PUSH_INT, .imm = 123});
  t_emit(unit, (IR_Inst){.op = IR_OP_PUSH_STRING, .imm = (int64_t)(intptr_t)"func"});
  t_emit(unit, (IR_Inst){.op = IR_OP_LIBCALL, .a = 0});

  assert_validate_error(unit, 0, ERR_COMP_SYNTAX,
                        "must be preceded by two PUSH_STRING");
  ir_destroy_unit(unit);
}

void test_ir_validate(void) {
  test_ir_validate_ok_case();
  test_ir_validate_unbound_label_rejected();
  test_ir_validate_invalid_label_ids_rejected();
  test_ir_validate_local_index_out_of_range_rejected();
  test_ir_validate_negative_arity_rejected();
  test_ir_validate_libcall_requires_two_push_string_operands();
}
