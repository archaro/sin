#include <stdint.h>
#include <stdlib.h>

#include "ir.h"
#include "test_assert.h"
#include "test_helpers.h"

static void test_absyn_helpers(void) {
  AS_NODE *lhs = t_int(1);
  AS_NODE *rhs = t_int(2);
  AS_NODE *sum = t_node(N_ADD, lhs, rhs);
  AS_NODE *stmt = t_node(N_EXPRSTMT, sum, NULL);
  AS_NODE *list = t_stmtlist_with_one(stmt);

  ASSERT_NOT_NULL(list);
  ASSERT_EQ_INT(N_STMTLIST, list->nodetype);
  ASSERT_TRUE(((AS_STMTLIST *)list->lhs)->count == 1);

  as_delete(list);
}

static void test_ir_and_emitbc_helpers(void) {
  IR_Unit *unit = t_new_unit();
  ASSERT_NOT_NULL(unit);

  int32_t done = ir_new_label(unit);
  t_emit(unit, (IR_Inst){.op = IR_OP_PUSH_INT, .imm = 7});
  t_emit(unit, (IR_Inst){.op = IR_OP_JUMP, .a = done});
  t_bind(unit, done);
  t_emit(unit, (IR_Inst){.op = IR_OP_LABEL, .a = done});
  t_emit(unit, (IR_Inst){.op = IR_OP_HALT});

  OUTPUT_t out = {0};
  out.maxsize = 64;
  out.bytecode = malloc(out.maxsize);
  out.nextbyte = out.bytecode;
  ASSERT_NOT_NULL(out.bytecode);

  char *errdetail = NULL;
  int8_t rc = t_emit_bytecode(unit, 0, 0, &out, &errdetail);
  ASSERT_EQ_INT(0, rc);
  ASSERT_TRUE(errdetail == NULL);
  ASSERT_TRUE((size_t)(out.nextbyte - out.bytecode) > 0);

  free(out.bytecode);
  ir_destroy_unit(unit);
}

int main(void) {
  test_absyn_helpers();
  test_ir_and_emitbc_helpers();
  return 0;
}
