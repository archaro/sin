#include "test_framework.h"

void test_absyn_constructor_allocation_failures(void);
void test_absyn_nested_binary_expressions(void);
void test_absyn_budget_limits_and_iterative_cleanup(void);
void test_absyn_stmtlist_multiple_statements(void);
void test_absyn_if_elsif_else_chain(void);
void test_absyn_item_deref_chains(void);
void test_absyn_malformed_float_valnode_returns_null(void);
void test_absyn_valnode_string_second_allocation_failure(void);
void test_absyn_stmtlist_growth_failure_preserves_statement(void);
void test_absyn_float_value_preserves_bits(void);
void test_absyn_nil_value_payload_free(void);

static const TF_TestDescriptor tests[] = {
    {"rewrite.core.test_absyn_nested_binary_expressions", test_absyn_nested_binary_expressions, "exclusive", 30000,
     "bytecode.ast.n_add,bytecode.ast.n_sub,bytecode.ast.n_mul,bytecode.ast.n_div,bytecode.ast.n_mod,bytecode.ast.n_equal,bytecode.ast.n_noteq,bytecode.ast.n_lt,bytecode.ast.n_lteq,bytecode.ast.n_gt,bytecode.ast.n_gteq,api.compiler.ast-lifecycle"},
    {"rewrite.core.test_absyn_budget_limits_and_iterative_cleanup", test_absyn_budget_limits_and_iterative_cleanup, "exclusive", 30000,
     "test.core.test_absyn_budget_limits_and_iterative_cleanup"},
    {"rewrite.core.test_absyn_stmtlist_multiple_statements", test_absyn_stmtlist_multiple_statements, "exclusive", 30000,
     "bytecode.ast.n_value,bytecode.ast.n_inc,bytecode.ast.n_dec,bytecode.ast.n_or,bytecode.ast.n_and,bytecode.ast.n_not,bytecode.ast.n_libcall,bytecode.ast.n_arglist,bytecode.ast.n_code,bytecode.ast.n_call,bytecode.ast.n_assitem,bytecode.ast.n_asslocal,bytecode.ast.n_exprstmt,bytecode.ast.n_return,bytecode.ast.n_stmtlist,bytecode.ast.n_stmt,bytecode.ast.n_dowhilestmt,bytecode.ast.n_break,bytecode.ast.n_continue"},
    {"rewrite.core.test_absyn_if_elsif_else_chain", test_absyn_if_elsif_else_chain, "exclusive", 30000,
     "bytecode.ast.n_whilestmt,bytecode.ast.n_ifstmt,bytecode.ast.n_foreach,bytecode.ast.n_foreachspec"},
    {"rewrite.core.test_absyn_item_deref_chains", test_absyn_item_deref_chains, "exclusive", 30000,
     "language.expression.item,language.item-syntax.dereference,language.item-syntax.layer-chain,bytecode.ast.n_deref,bytecode.ast.n_item,bytecode.ast.n_relitem,bytecode.ast.n_itemref,bytecode.ast.n_list,bytecode.ast.n_listelem"},
    {"rewrite.core.test_absyn_float_value_preserves_bits", test_absyn_float_value_preserves_bits, "exclusive", 30000,
     "test.core.test_absyn_float_value_preserves_bits"},
    {"rewrite.core.test_absyn_nil_value_payload_free", test_absyn_nil_value_payload_free, "exclusive", 30000,
     "test.core.test_absyn_nil_value_payload_free"},
    {"rewrite.core.test_absyn_malformed_float_valnode_returns_null", test_absyn_malformed_float_valnode_returns_null, "exclusive", 30000,
     "test.core.test_absyn_malformed_float_valnode_returns_null"},
    {"rewrite.core.test_absyn_constructor_allocation_failures",
     test_absyn_constructor_allocation_failures, "exclusive", 30000,
     "api.common.memory,api.compiler.ast-lifecycle"},
    {"rewrite.core.test_absyn_valnode_string_second_allocation_failure", test_absyn_valnode_string_second_allocation_failure, "exclusive", 30000,
     "test.core.test_absyn_valnode_string_second_allocation_failure"},
    {"rewrite.core.test_absyn_stmtlist_growth_failure_preserves_statement", test_absyn_stmtlist_growth_failure_preserves_statement, "exclusive", 30000,
     "api.compiler.ast-lifecycle"},
};

int main(int argc, char **argv) {
  return tf_main(argc, argv, tests, sizeof tests / sizeof tests[0]);
}
