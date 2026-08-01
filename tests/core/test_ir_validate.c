#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "error.h"
#include "compiler/ir.h"
#include "compiler/lower.h"
#include "compiler/semant.h"
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

  t_emit(unit, (IR_Inst){.op = IR_OP_LIBCALL, .a = 1, .b = 0});
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
  t_emit(unit, (IR_Inst){.op = IR_OP_LIBCALL, .a = -2, .b = 0});
  assert_validate_error(unit, 0, ERR_COMP_SYNTAX, "invalid pair");
  ir_destroy_unit(unit);
}

static void assert_lower_local_error(AS_NODE *root, const char *expected_name) {
  IR_Unit *ir = NULL;
  char *errdetail = NULL;
  int8_t rc = lower_ast_to_ir(root, NULL, &ir, &errdetail);
  ASSERT_EQ_INT(ERR_COMP_LOCALBEFOREDEF, rc);
  ASSERT_TRUE(ir == NULL);
  ASSERT_NOT_NULL(errdetail);
  ASSERT_TRUE(strstr(errdetail, expected_name) != NULL);
  free(errdetail);
}

static void test_lower_float_value_emits_push_float(void) {
  const uint64_t bits = UINT64_C(0x7ff8000000000042);
  AS_NODE *root = t_node(N_EXPRSTMT, t_node(N_VALUE, as_new_value(V_FLOAT, bits, NULL), NULL), NULL);
  IR_Unit *ir = NULL;
  char *errdetail = NULL;

  int8_t rc = lower_ast_to_ir(root, NULL, &ir, &errdetail);
  ASSERT_EQ_INT(ERR_NOERROR, rc);
  ASSERT_TRUE(errdetail == NULL);
  ASSERT_NOT_NULL(ir);
  ASSERT_EQ_INT(3, (int)ir->function.count);
  ASSERT_EQ_INT(IR_OP_PUSH_FLOAT, ir->function.code[0].op);
  ASSERT_EQ_INT((int64_t)bits, ir->function.code[0].imm);
  ASSERT_EQ_INT(IR_OP_DISCARD, ir->function.code[1].op);
  ASSERT_EQ_INT(IR_OP_HALT, ir->function.code[2].op);

  ir_destroy_unit(ir);
  as_delete(root);
}

static void test_lower_nil_value_emits_push_nil(void) {
  AS_NODE *root = t_node(N_RETURN,
                         t_node(N_VALUE, as_new_value(V_NIL, 0, NULL), NULL), NULL);
  IR_Unit *ir = NULL;
  char *errdetail = NULL;
  ASSERT_EQ_INT(ERR_NOERROR, lower_ast_to_ir(root, NULL, &ir, &errdetail));
  ASSERT_TRUE(errdetail == NULL);
  ASSERT_NOT_NULL(ir);
  ASSERT_EQ_INT(3, (int)ir->function.count);
  ASSERT_EQ_INT(IR_OP_PUSH_NIL, ir->function.code[0].op);
  ASSERT_EQ_INT(IR_OP_RETURN, ir->function.code[1].op);
  ASSERT_EQ_INT(IR_OP_HALT, ir->function.code[2].op);
  ir_destroy_unit(ir);
  as_delete(root);
  free(errdetail);
}

static void test_lower_returns_and_discards_expression_statements(void) {
  AS_NODE *root = as_new_stmtlist_node();
  root = as_stmtlist_append(root, t_node(N_EXPRSTMT, t_int(1), NULL));
  root = as_stmtlist_append(root, t_node(N_RETURN, t_int(2), NULL));
  root = as_stmtlist_append(root, t_node(N_RETURN, NULL, NULL));
  IR_Unit *ir = NULL;
  char *errdetail = NULL;

  ASSERT_EQ_INT(ERR_NOERROR, lower_ast_to_ir(root, NULL, &ir, &errdetail));
  ASSERT_TRUE(errdetail == NULL);
  ASSERT_NOT_NULL(ir);
  ASSERT_EQ_INT(6, (int)ir->function.count);
  ASSERT_EQ_INT(IR_OP_PUSH_INT, ir->function.code[0].op);
  ASSERT_EQ_INT(IR_OP_DISCARD, ir->function.code[1].op);
  ASSERT_EQ_INT(IR_OP_PUSH_INT, ir->function.code[2].op);
  ASSERT_EQ_INT(IR_OP_RETURN, ir->function.code[3].op);
  ASSERT_EQ_INT(IR_OP_HALT, ir->function.code[4].op);
  ASSERT_EQ_INT(IR_OP_HALT, ir->function.code[5].op);
  ir_destroy_unit(ir);
  as_delete(root);
}

static void test_lower_local_resolution_errors_consistent(void) {
  AS_NODE *expr_stmt = t_node(N_EXPRSTMT, t_local("x"), NULL);
  AS_NODE *asslocal = t_node(N_ASSLOCAL, t_local("x"), t_int(1));
  AS_NODE *inc = t_node(N_INC, t_local("x"), NULL);
  AS_NODE *dec = t_node(N_DEC, t_local("x"), NULL);
  AS_NODE *null_named_local = as_new_node(N_VALUE, as_new_value(V_LOCAL, 0, NULL), NULL);
  AS_NODE *null_expr_stmt = t_node(N_EXPRSTMT, null_named_local, NULL);

  assert_lower_local_error(expr_stmt, "x");
  assert_lower_local_error(asslocal, "x");
  assert_lower_local_error(inc, "x");
  assert_lower_local_error(dec, "x");
  assert_lower_local_error(null_expr_stmt, "<null>");

  as_delete(expr_stmt);
  as_delete(asslocal);
  as_delete(inc);
  as_delete(dec);
  as_delete(null_expr_stmt);
}

static void test_lower_control_flow_outside_loop_rejected(void) {
  for (int nodetype = N_BREAK; nodetype <= N_CONTINUE; nodetype++) {
    AS_NODE *root = t_stmtlist_with_one(t_node((ENUM_NODE)nodetype, NULL, NULL));
    IR_Unit *ir = NULL;
    char *errdetail = NULL;
    ASSERT_EQ_INT(ERR_COMP_SYNTAX,
                  lower_ast_to_ir(root, NULL, &ir, &errdetail));
    ASSERT_TRUE(ir == NULL);
    ASSERT_NOT_NULL(errdetail);
    ASSERT_TRUE(strstr(errdetail, nodetype == N_BREAK ? "BREAK outside loop"
                                                       : "CONTINUE outside loop") != NULL);
    free(errdetail);
    as_delete(root);
  }
}

static void test_lower_short_circuit_uses_balanced_control_flow(void) {
  const int nodes[] = {N_AND, N_OR};
  for (size_t n = 0; n < sizeof(nodes) / sizeof(nodes[0]); n++) {
    AS_NODE *expr = t_node((ENUM_NODE)nodes[n], t_int(0), t_int(1));
    AS_NODE *root = t_stmtlist_with_one(t_node(N_RETURN, expr, NULL));
    IR_Unit *ir = NULL;
    char *errdetail = NULL;
    ASSERT_EQ_INT(ERR_NOERROR, lower_ast_to_ir(root, NULL, &ir, &errdetail));
    ASSERT_NOT_NULL(ir);
    size_t jumps = 0;
    size_t labels = 0;
    size_t bools = 0;
    for (size_t i = 0; i < ir->function.count; i++) {
      IR_Op op = ir->function.code[i].op;
      ASSERT_TRUE(op != IR_OP_AND && op != IR_OP_OR);
      if (op == IR_OP_JUMP || op == IR_OP_JUMP_IF_FALSE) jumps++;
      if (op == IR_OP_LABEL) labels++;
      if (op == IR_OP_PUSH_BOOL) bools++;
    }
    ASSERT_EQ_INT(nodes[n] == N_OR ? 4 : 3, (int)jumps);
    ASSERT_EQ_INT(nodes[n] == N_OR ? 3 : 2, (int)labels);
    ASSERT_EQ_INT(nodes[n] == N_OR ? 3 : 2, (int)bools);
    free(errdetail);
    ir_destroy_unit(ir);
    as_delete(root);
  }
}

void test_ir_validate(void) {
  test_ir_validate_ok_case();
  test_ir_validate_unbound_label_rejected();
  test_ir_validate_invalid_label_ids_rejected();
  test_ir_validate_local_index_out_of_range_rejected();
  test_ir_validate_negative_arity_rejected();
  test_lower_float_value_emits_push_float();
  test_lower_nil_value_emits_push_nil();
  test_lower_returns_and_discards_expression_statements();
  test_lower_local_resolution_errors_consistent();
  test_lower_control_flow_outside_loop_rejected();
  test_lower_short_circuit_uses_balanced_control_flow();
}
