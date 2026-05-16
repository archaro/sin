#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "ir.h"
#include "test_assert.h"
#include "test_helpers.h"

void test_emitbc_header(void);
void test_emitbc_opcode_map(void);
void test_emitbc_opcode_map_unsupported_ir_op(void);
void test_emitbc_opcode_map_call_item_deref_alias_layout(void);
void test_emitbc_jumps(void);
void test_emitbc_invariants(void);
void test_pipeline_golden(void);
void test_pipeline_large_local_lookup_duplicate(void);
void test_pipeline_source_golden(void);
void test_ir_validate(void);
void test_pipeline_negative_matrix(void);
void test_absyn_nested_binary_expressions(void);
void test_absyn_stmtlist_multiple_statements(void);
void test_absyn_if_elsif_else_chain(void);
void test_absyn_item_deref_chains(void);
void test_sem_check_locals_reusable_context(void);
void test_sem_duplicate_local_keeps_original_index(void);
void test_sem_code_params_are_treated_as_defined_locals(void);
void test_sdiss_fixture_basic(void);
void test_parser_examples_obj_golden(void);
void test_interpret_semantics_golden(void);
void test_fixture_policy_declared_goldens_exist(void);

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



static void run_test(const char *label, void (*test_fn)(void)) {
  printf("[test-harness] running %s\n", label);
  test_fn();
}

int main(void) {
  run_test("test_absyn_helpers", test_absyn_helpers);
  run_test("test_ir_and_emitbc_helpers", test_ir_and_emitbc_helpers);
  run_test("test_emitbc_header", test_emitbc_header);
  run_test("test_emitbc_opcode_map", test_emitbc_opcode_map);
  run_test("test_emitbc_opcode_map_call_item_deref_alias_layout", test_emitbc_opcode_map_call_item_deref_alias_layout);
  run_test("test_emitbc_opcode_map_unsupported_ir_op", test_emitbc_opcode_map_unsupported_ir_op);
  run_test("test_emitbc_jumps", test_emitbc_jumps);
  run_test("test_emitbc_invariants", test_emitbc_invariants);
  run_test("test_pipeline_golden", test_pipeline_golden);
  run_test("test_pipeline_large_local_lookup_duplicate", test_pipeline_large_local_lookup_duplicate);
  run_test("test_pipeline_source_golden", test_pipeline_source_golden);
  run_test("test_ir_validate", test_ir_validate);
  run_test("test_pipeline_negative_matrix", test_pipeline_negative_matrix);
  run_test("test_absyn_nested_binary_expressions", test_absyn_nested_binary_expressions);
  run_test("test_absyn_stmtlist_multiple_statements", test_absyn_stmtlist_multiple_statements);
  run_test("test_absyn_if_elsif_else_chain", test_absyn_if_elsif_else_chain);
  run_test("test_absyn_item_deref_chains", test_absyn_item_deref_chains);
  run_test("test_sem_check_locals_reusable_context", test_sem_check_locals_reusable_context);
  run_test("test_sem_duplicate_local_keeps_original_index", test_sem_duplicate_local_keeps_original_index);
  run_test("test_sem_code_params_are_treated_as_defined_locals", test_sem_code_params_are_treated_as_defined_locals);
  run_test("test_sdiss_fixture_basic", test_sdiss_fixture_basic);
  run_test("test_parser_examples_obj_golden", test_parser_examples_obj_golden);
  run_test("test_interpret_semantics_golden", test_interpret_semantics_golden);
  run_test("test_fixture_policy_declared_goldens_exist", test_fixture_policy_declared_goldens_exist);
  return 0;
}
