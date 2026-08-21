#include "test_framework.h"

void test_parser_scanner_setup_allocation_failures(void);
void test_parser_cleanup_allocation_failures(void);
void test_parser_input_api(void);
void test_parser_compound_spans_preserve_construct_start(void);
void test_parser_ast_node_budget_stops_construction_early(void);
void test_parser_lists_and_itemrefs_ast(void);
void test_parser_foreach_ast(void);
void test_parser_foreach_allocation_failures(void);

static const TF_TestDescriptor tests[] = {
    {"rewrite.core.test_parser_input_api", test_parser_input_api, "exclusive", 30000,
     "language.token.tlocal,language.token.tlayer,language.token.tlibname,language.token.tcodebody,language.token.tliststart,language.token.titemref,language.token.tbreak,language.token.tcontinue,language.token.tsemi,language.token.twhile,language.token.tdo,language.token.tendwhile,language.token.tif,language.token.tthen,language.token.telse,language.token.telsif,language.token.tendif,language.token.treturn,language.token.tforeach,language.token.tin,language.token.tendfor,language.token.tassign,language.token.tlayersep,language.token.tderefstart,language.token.tcode,language.token.tderefend,language.token.tlparen,language.token.trparen,language.token.tlbrace,language.token.trbrace,language.token.tcomma,language.production.input,language.production.stmtlist,language.production.stmtsemi,language.production.stmt,language.production.expr,language.production.libcall,language.production.elsif_else_opt,language.production.params,language.production.param_list,language.production.param_local,language.production.args,language.production.arg_list,language.production.item_assignment,language.production.list,language.production.list_elems,language.production.itemref,language.production.item,language.production.first_layer,language.production.subsequent_layers,language.production.layer,language.production.dereference,language.production.deref_content,language.diagnostic.lexer-error,api.compiler.parser-lifecycle"},
    {"rewrite.core.test_parser_compound_spans_preserve_construct_start", test_parser_compound_spans_preserve_construct_start, "exclusive", 30000,
     "test.core.test_parser_compound_spans_preserve_construct_start"},
    {"rewrite.core.test_parser_ast_node_budget_stops_construction_early", test_parser_ast_node_budget_stops_construction_early, "exclusive", 30000,
     "test.core.test_parser_ast_node_budget_stops_construction_early"},
    {"rewrite.core.test_parser_lists_and_itemrefs_ast", test_parser_lists_and_itemrefs_ast, "exclusive", 30000,
     "language.expression.item-reference,language.expression.list,language.item-syntax.absolute-layer,bytecode.ast.n_deref,bytecode.ast.n_item,bytecode.ast.n_relitem,bytecode.ast.n_itemref,bytecode.ast.n_list,bytecode.ast.n_listelem"},
    {"rewrite.core.test_parser_foreach_ast", test_parser_foreach_ast, "exclusive", 30000,
     "language.statement.foreach,bytecode.ast.n_whilestmt,bytecode.ast.n_ifstmt,bytecode.ast.n_foreach,bytecode.ast.n_foreachspec"},
    {"rewrite.core.test_parser_scanner_setup_allocation_failures",
     test_parser_scanner_setup_allocation_failures, "exclusive", 30000,
     "api.common.memory"},
    {"rewrite.core.test_parser_cleanup_allocation_failures",
     test_parser_cleanup_allocation_failures, "exclusive", 30000,
     "language.diagnostic.allocation-error,api.common.memory,api.compiler.parser-lifecycle"},
    {"rewrite.core.test_parser_foreach_allocation_failures", test_parser_foreach_allocation_failures, "exclusive", 30000,
     "test.core.test_parser_foreach_allocation_failures"},
};

int main(int argc, char **argv) {
  return tf_main(argc, argv, tests, sizeof tests / sizeof tests[0]);
}
