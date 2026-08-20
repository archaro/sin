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
     "api.compiler.parser-lifecycle,baseline.legacy.unified.core.test_parser_input_api,language.diagnostic.lexer-error,language.production.arg_list,language.production.args,language.production.deref_content,language.production.dereference,language.production.elsif_else_opt,language.production.expr,language.production.first_layer,language.production.input,language.production.item,language.production.item_assignment,language.production.itemref,language.production.layer,language.production.libcall,language.production.list,language.production.list_elems,language.production.param_list,language.production.param_local,language.production.params,language.production.stmt,language.production.stmtlist,language.production.stmtsemi,language.production.subsequent_layers,language.token.tassign,language.token.tbreak,language.token.tcode,language.token.tcodebody,language.token.tcomma,language.token.tcontinue,language.token.tderefend,language.token.tderefstart,language.token.tdo,language.token.telse,language.token.telsif,language.token.tendfor,language.token.tendif,language.token.tendwhile,language.token.tforeach,language.token.tif,language.token.tin,language.token.titemref,language.token.tlayer,language.token.tlayersep,language.token.tlbrace,language.token.tlibname,language.token.tliststart,language.token.tlocal,language.token.tlparen,language.token.trbrace,language.token.treturn,language.token.trparen,language.token.tsemi,language.token.tthen,language.token.twhile"},
    {"rewrite.core.test_parser_compound_spans_preserve_construct_start", test_parser_compound_spans_preserve_construct_start, "exclusive", 30000,
     "baseline.legacy.unified.core.test_parser_compound_spans_preserve_construct_start"},
    {"rewrite.core.test_parser_ast_node_budget_stops_construction_early", test_parser_ast_node_budget_stops_construction_early, "exclusive", 30000,
     "baseline.legacy.unified.core.test_parser_ast_node_budget_stops_construction_early"},
    {"rewrite.core.test_parser_lists_and_itemrefs_ast", test_parser_lists_and_itemrefs_ast, "exclusive", 30000,
     "baseline.legacy.unified.core.test_parser_lists_and_itemrefs_ast,bytecode.ast.n_deref,bytecode.ast.n_item,bytecode.ast.n_itemref,bytecode.ast.n_list,bytecode.ast.n_listelem,bytecode.ast.n_relitem,language.expression.item-reference,language.expression.list,language.item-syntax.absolute-layer"},
    {"rewrite.core.test_parser_foreach_ast", test_parser_foreach_ast, "exclusive", 30000,
     "baseline.legacy.unified.core.test_parser_foreach_ast,bytecode.ast.n_foreach,bytecode.ast.n_foreachspec,bytecode.ast.n_ifstmt,bytecode.ast.n_whilestmt,language.statement.foreach"},
    {"rewrite.core.test_parser_scanner_setup_allocation_failures",
     test_parser_scanner_setup_allocation_failures, "exclusive", 30000,
     "api.common.memory,baseline.legacy.unified.core.test_parser_scanner_setup_allocation_failures"},
    {"rewrite.core.test_parser_cleanup_allocation_failures",
     test_parser_cleanup_allocation_failures, "exclusive", 30000,
     "api.common.memory,api.compiler.parser-lifecycle,baseline.legacy.unified.core.test_parser_cleanup_allocation_failures,language.diagnostic.allocation-error"},
    {"rewrite.core.test_parser_foreach_allocation_failures", test_parser_foreach_allocation_failures, "exclusive", 30000,
     "baseline.legacy.unified.core.test_parser_foreach_allocation_failures"},
};

int main(int argc, char **argv) {
  return tf_main(argc, argv, tests, sizeof tests / sizeof tests[0]);
}
