#include <stdlib.h>
#include <string.h>

#include "error.h"
#include "compiler/ir.h"
#include "test_assert.h"
#include "test_helpers.h"

static void assert_emission_failure(IR_Unit *unit, uint8_t local_count,
                                    uint8_t param_count,
                                    int8_t expected_code,
                                    const char *expected_origin,
                                    const char *expected_detail) {
  OUTPUT_t out = {0};
  out.maxsize = 16;
  out.bytecode = malloc(out.maxsize);
  out.nextbyte = out.bytecode;
  ASSERT_NOT_NULL(out.bytecode);

  char *errdetail = NULL;
  int8_t rc = t_emit_bytecode(unit, local_count, param_count, &out,
                              &errdetail);
  if (rc == ERR_NOERROR) {
    TEST_FAILF("expected emission failure containing: %s", expected_detail);
  }
  ASSERT_EQ_INT(expected_code, rc);
  ASSERT_NOT_NULL(errdetail);
  ASSERT_TRUE(strstr(errdetail, expected_origin) != NULL);
  ASSERT_TRUE(strstr(errdetail, expected_detail) != NULL);

  free(errdetail);
  free(out.bytecode);
  ir_destroy_unit(unit);
}

void test_emitbc_post_emission_verification(void) {
  IR_Unit *unit = t_new_unit();
  t_emit(unit, (IR_Inst){.op = IR_OP_HALT});
  assert_emission_failure(unit, 0, 1, ERR_COMP_SYNTAX,
                          "emitbc: parameter count exceeds local count",
                          "parameter count exceeds local count");

  unit = t_new_unit();
  t_emit(unit, (IR_Inst){.op = IR_OP_PUSH_INT, .imm = 1});
  assert_emission_failure(unit, 0, 0, ERR_COMP_SYNTAX,
                          "emitbc: bytecode verification failed",
                          "final physical instruction must be HALT");

  unit = t_new_unit();
  t_emit(unit, (IR_Inst){.op = IR_OP_HALT});
  t_emit(unit, (IR_Inst){.op = IR_OP_PUSH_INT, .imm = 1});
  assert_emission_failure(unit, 0, 0, ERR_COMP_SYNTAX,
                          "emitbc: bytecode verification failed",
                          "final physical instruction must be HALT");

  unit = t_new_unit();
  t_emit(unit, (IR_Inst){.op = IR_OP_LOAD_LOCAL, .a = 1});
  t_emit(unit, (IR_Inst){.op = IR_OP_HALT});
  assert_emission_failure(unit, 1, 0, ERR_COMP_LOCALBEFOREDEF,
                          "ir: Instruction",
                          "out-of-range local index 1");

  unit = t_new_unit();
  t_emit(unit, (IR_Inst){.op = IR_OP_ADD});
  t_emit(unit, (IR_Inst){.op = IR_OP_HALT});
  assert_emission_failure(unit, 0, 0, ERR_COMP_SYNTAX,
                          "emitbc: bytecode verification failed",
                          "stack underflow");

  unit = t_new_unit();
  t_emit(unit, (IR_Inst){.op = IR_OP_ITEM_BEGIN});
  t_emit(unit, (IR_Inst){.op = IR_OP_HALT});
  assert_emission_failure(unit, 0, 0, ERR_COMP_SYNTAX,
                          "emitbc: bytecode verification failed",
                          "unknown item-layer opcode");
}
