#include <stdint.h>
#include <stdlib.h>

#include "ir.h"
#include "test_assert.h"
#include "test_helpers.h"

void test_emitbc_header(void);
void test_emitbc_opcode_map(void);
void test_emitbc_opcode_map_unsupported_ir_op(void);
void test_emitbc_opcode_map_call_item_deref_alias_layout(void);
void test_emitbc_jumps(void);
void test_pipeline_golden(void);
void test_pipeline_source_golden(void);
void test_scomp_e2e_golden(void);
void test_ir_validate(void);
void test_absyn_nested_binary_expressions(void);
void test_absyn_stmtlist_multiple_statements(void);
void test_absyn_if_elsif_else_chain(void);
void test_absyn_item_deref_chains(void);
void test_sem_check_locals_reusable_context(void);
void test_sdiss_fixture_basic(void);
void test_parser_examples_obj_golden(void);

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
  test_emitbc_header();
  test_emitbc_opcode_map();
  test_emitbc_opcode_map_call_item_deref_alias_layout();
  test_emitbc_opcode_map_unsupported_ir_op();
  test_emitbc_jumps();
  test_pipeline_golden();
  test_pipeline_source_golden();
  test_scomp_e2e_golden();
  test_ir_validate();
  test_absyn_nested_binary_expressions();
  test_absyn_stmtlist_multiple_statements();
  test_absyn_if_elsif_else_chain();
  test_absyn_item_deref_chains();
  test_sem_check_locals_reusable_context();
  test_sdiss_fixture_basic();
  test_parser_examples_obj_golden();
  return 0;
}
