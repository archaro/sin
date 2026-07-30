#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "compiler/absyn.h"
#include "compiler/compdiag.h"
#include "error.h"
#include "compiler/parse_input.h"
#include "memory.h"
#include "parser.h"
#include "string_limits.h"
#include "test_assert.h"

static AS_NODE *parse_lists_ok(const char *source) {
  AS_NODE *absyn = NULL;
  char *errdetail = NULL;
  ParseInput input = {source, strlen(source), "list-parser-test.src"};
  ASSERT_EQ_INT(ERR_NOERROR, parse_source(&input, &absyn, &errdetail));
  ASSERT_TRUE(errdetail == NULL);
  ASSERT_NOT_NULL(absyn);
  return absyn;
}

static void parse_lists_fails(const char *source) {
  AS_NODE *absyn = NULL;
  char *errdetail = NULL;
  ParseInput input = {source, strlen(source), "list-parser-test.src"};
  ASSERT_TRUE(parse_source(&input, &absyn, &errdetail) != ERR_NOERROR);
  ASSERT_NOT_NULL(errdetail);
  free(errdetail);
  as_delete(absyn);
}

static AS_NODE *list_element(AS_NODE *list, unsigned int index) {
  AS_NODE *elem = (AS_NODE *)list->lhs;
  for (unsigned int i = 0; i < index; ++i) {
    ASSERT_NOT_NULL(elem);
    ASSERT_EQ_INT(N_LISTELEM, elem->nodetype);
    elem = (AS_NODE *)elem->rhs;
  }
  ASSERT_NOT_NULL(elem);
  ASSERT_EQ_INT(N_LISTELEM, elem->nodetype);
  return elem;
}

static AS_VALUE *node_value(AS_NODE *node) {
  ASSERT_NOT_NULL(node);
  ASSERT_EQ_INT(N_VALUE, node->nodetype);
  ASSERT_NOT_NULL(node->lhs);
  return (AS_VALUE *)node->lhs;
}

void test_parser_input_api(void) {
  AS_NODE *absyn = NULL;
  char *errdetail = NULL;

  const char malformed[] = "@x = ;";
  ParseInput malformed_input = {malformed, sizeof(malformed) - 1, "malformed.src"};
  int8_t rc = parse_source(&malformed_input, &absyn, &errdetail);
  ASSERT_TRUE(rc != ERR_NOERROR);
  ASSERT_TRUE(errdetail != NULL);
  free(errdetail);

  const char embedded_nul[] = {'@','x','=','1',';','\0','@','y','=','2',';'};
  ParseInput nul_input = {embedded_nul, sizeof(embedded_nul), "embedded-nul.src"};
  absyn = NULL;
  errdetail = NULL;
  rc = parse_source(&nul_input, &absyn, &errdetail);
  ASSERT_TRUE(rc != ERR_NOERROR);
  ASSERT_TRUE(errdetail != NULL);
  free(errdetail);

  const char empty[] = "";
  ParseInput empty_input = {empty, 0, "empty.src"};
  absyn = NULL;
  errdetail = NULL;
  rc = parse_source(&empty_input, &absyn, &errdetail);
  ASSERT_EQ_INT(ERR_NOERROR, rc);
  ASSERT_TRUE(absyn != NULL);
  ASSERT_TRUE(errdetail == NULL);
  as_delete(absyn);

  const char nil_source[] = "RETURN NiL;";
  ParseInput nil_input = {nil_source, sizeof(nil_source) - 1, "nil.src"};
  absyn = NULL;
  errdetail = NULL;
  ASSERT_EQ_INT(ERR_NOERROR, parse_source(&nil_input, &absyn, &errdetail));
  ASSERT_TRUE(errdetail == NULL);
  AS_STMTLIST *nil_list = (AS_STMTLIST *)absyn->lhs;
  ASSERT_EQ_INT(1, nil_list->count);
  ASSERT_EQ_INT(N_RETURN, nil_list->stmts[0]->nodetype);
  ASSERT_NOT_NULL(nil_list->stmts[0]->lhs);
  ASSERT_EQ_INT(N_VALUE, ((AS_NODE *)nil_list->stmts[0]->lhs)->nodetype);
  AS_VALUE *nil_value = (AS_VALUE *)((AS_NODE *)nil_list->stmts[0]->lhs)->lhs;
  ASSERT_NOT_NULL(nil_value);
  ASSERT_EQ_INT(V_NIL, nil_value->valtype);
  as_delete(absyn);

  const char control_flow[] = "break; continue;";
  ParseInput control_input = {control_flow, sizeof(control_flow) - 1,
                              "control-flow.src"};
  absyn = NULL;
  errdetail = NULL;
  rc = parse_source(&control_input, &absyn, &errdetail);
  ASSERT_EQ_INT(ERR_NOERROR, rc);
  ASSERT_TRUE(errdetail == NULL);
  AS_STMTLIST *controls = (AS_STMTLIST *)absyn->lhs;
  ASSERT_EQ_INT(N_BREAK, controls->stmts[0]->nodetype);
  ASSERT_EQ_INT(N_CONTINUE, controls->stmts[1]->nodetype);
  as_delete(absyn);

  const char returns[] = "return; return 17;";
  ParseInput return_input = {returns, sizeof(returns) - 1, "returns.src"};
  absyn = NULL;
  errdetail = NULL;
  rc = parse_source(&return_input, &absyn, &errdetail);
  ASSERT_EQ_INT(ERR_NOERROR, rc);
  ASSERT_TRUE(errdetail == NULL);
  AS_STMTLIST *return_list = (AS_STMTLIST *)absyn->lhs;
  ASSERT_EQ_INT(2, return_list->count);
  ASSERT_EQ_INT(N_RETURN, return_list->stmts[0]->nodetype);
  ASSERT_TRUE(return_list->stmts[0]->lhs == NULL);
  ASSERT_EQ_INT(N_RETURN, return_list->stmts[1]->nodetype);
  ASSERT_EQ_INT(N_VALUE, ((AS_NODE *)return_list->stmts[1]->lhs)->nodetype);
  as_delete(absyn);

  const char malformed_return[] = "return +;";
  ParseInput malformed_return_input = {malformed_return,
                                       sizeof(malformed_return) - 1,
                                       "malformed-return.src"};
  absyn = NULL;
  errdetail = NULL;
  rc = parse_source(&malformed_return_input, &absyn, &errdetail);
  ASSERT_TRUE(rc != ERR_NOERROR);
  ASSERT_TRUE(absyn == NULL);
  ASSERT_NOT_NULL(errdetail);
  free(errdetail);

  absyn = NULL;
  errdetail = NULL;
  rc = parse_source(NULL, &absyn, &errdetail);
  ASSERT_EQ_INT(ERR_COMP_SYNTAX, rc);
  ASSERT_TRUE(absyn == NULL);
  ASSERT_TRUE(errdetail == NULL);

  ParseInput null_data_input = {NULL, 0, "null-data.src"};
  CompilerDiagnostic diag = {0};
  SCANNER_STATE_t state = {0};
  rc = parse_source_compiler_diag(&null_data_input, &absyn, &errdetail,
                                  &diag, &state);
  ASSERT_EQ_INT(ERR_COMP_SYNTAX, rc);
  ASSERT_EQ_INT(1, state.line);
  ASSERT_EQ_INT(1, state.column);
  ASSERT_EQ_INT(1, state.span);
  ASSERT_TRUE(state.offending_token == NULL);
  ASSERT_EQ_INT(DIAG_PHASE_PARSE, diag.phase);
  compiler_diag_reset(&diag);

  rc = parse_source(&empty_input, NULL, &errdetail);
  ASSERT_EQ_INT(ERR_COMP_SYNTAX, rc);
  rc = parse_source(&empty_input, &absyn, NULL);
  ASSERT_EQ_INT(ERR_COMP_SYNTAX, rc);

  size_t literal_len = SIN_MAX_STRING_BYTES + 1;
  size_t source_len = literal_len + 3;
  char *large_literal = malloc(source_len + 1);
  ASSERT_NOT_NULL(large_literal);
  large_literal[0] = '"';
  memset(large_literal + 1, 'x', literal_len);
  large_literal[literal_len + 1] = '"';
  large_literal[literal_len + 2] = ';';
  large_literal[source_len] = '\0';
  ParseInput large_input = {large_literal, source_len, "large-string.src"};
  absyn = NULL;
  errdetail = NULL;
  rc = parse_source(&large_input, &absyn, &errdetail);
  ASSERT_EQ_INT(ERR_COMP_UNKNOWNCHAR, rc);
  ASSERT_NOT_NULL(errdetail);
  ASSERT_TRUE(strstr(errdetail, "String literal too long.") != NULL);
  free(errdetail);
  free(large_literal);
}

void test_parser_scanner_setup_allocation_failures(void) {
  const char source[] = "1;";
  ParseInput input = {source, sizeof(source) - 1, "scanner-failure.src"};

  for (long fail_at = 0; fail_at <= 4; ++fail_at) {
    AS_NODE *absyn = NULL;
    char *errdetail = NULL;
    SCANNER_STATE_t state = {0};
    alloc_test_fail_after(fail_at);
    int8_t rc = parse_source_diag(&input, &absyn, &errdetail, &state);
    alloc_test_fail_after(-1);
    ASSERT_TRUE(rc != ERR_NOERROR);
    ASSERT_TRUE(absyn == NULL);
    free(errdetail);
    free(state.offending_token);
  }
}

void test_parser_cleanup_allocation_failures(void) {
  const char source[] =
      "1 + 2; return; return 9; if 3 then 4; elsif 0 then 5; else 6; endif; while 1 do 7; endwhile; do 8; while 1; #[1, #[2, &players.[@index]], &fred];";
  ParseInput input = {source, sizeof(source) - 1, "parser-failure.src"};

  for (long fail_at = 0; fail_at < 128; ++fail_at) {
    AS_NODE *absyn = NULL;
    char *errdetail = NULL;
    alloc_test_fail_after(fail_at);
    int8_t rc = parse_source(&input, &absyn, &errdetail);
    alloc_test_fail_after(-1);
    if (rc == ERR_NOERROR) {
      ASSERT_NOT_NULL(absyn);
      as_delete(absyn);
    } else {
      ASSERT_TRUE(absyn == NULL);
    }
    free(errdetail);
  }
}

void test_parser_lists_and_itemrefs_ast(void) {
  AS_NODE *root = parse_lists_ok(
      "#[]; #[fred]; #[&fred]; [fred]; "
      "#[1, #[], fred, &players.[@index]]; return #[[fred]];");
  AS_STMTLIST *stmts = (AS_STMTLIST *)root->lhs;
  ASSERT_EQ_INT(6, stmts->count);

  AS_NODE *expr = (AS_NODE *)stmts->stmts[0]->lhs;
  ASSERT_EQ_INT(N_LIST, expr->nodetype);
  ASSERT_TRUE(expr->lhs == NULL);

  expr = (AS_NODE *)stmts->stmts[1]->lhs;
  ASSERT_EQ_INT(N_LIST, expr->nodetype);
  AS_NODE *elem = list_element(expr, 0);
  ASSERT_EQ_INT(N_CALL, ((AS_NODE *)elem->lhs)->nodetype);
  ASSERT_EQ_INT(N_ITEM, ((AS_NODE *)((AS_NODE *)elem->lhs)->lhs)->nodetype);

  expr = (AS_NODE *)stmts->stmts[2]->lhs;
  ASSERT_EQ_INT(N_LIST, expr->nodetype);
  elem = list_element(expr, 0);
  ASSERT_EQ_INT(N_ITEMREF, ((AS_NODE *)elem->lhs)->nodetype);
  ASSERT_EQ_INT(N_ITEM, ((AS_NODE *)((AS_NODE *)elem->lhs)->lhs)->nodetype);

  expr = (AS_NODE *)stmts->stmts[3]->lhs;
  ASSERT_EQ_INT(N_CALL, expr->nodetype);
  ASSERT_EQ_INT(N_ITEM, ((AS_NODE *)expr->lhs)->nodetype);
  ASSERT_EQ_INT(N_DEREF, ((AS_NODE *)((AS_NODE *)expr->lhs)->lhs)->nodetype);

  expr = (AS_NODE *)stmts->stmts[4]->lhs;
  ASSERT_EQ_INT(N_LIST, expr->nodetype);
  elem = list_element(expr, 0);
  ASSERT_EQ_INT(V_INT, node_value((AS_NODE *)elem->lhs)->valtype);
  elem = list_element(expr, 1);
  ASSERT_EQ_INT(N_LIST, ((AS_NODE *)elem->lhs)->nodetype);
  ASSERT_TRUE(((AS_NODE *)elem->lhs)->lhs == NULL);
  elem = list_element(expr, 2);
  ASSERT_EQ_INT(N_CALL, ((AS_NODE *)elem->lhs)->nodetype);
  elem = list_element(expr, 3);
  AS_NODE *dynamic_ref = (AS_NODE *)elem->lhs;
  ASSERT_EQ_INT(N_ITEMREF, dynamic_ref->nodetype);
  AS_NODE *dynamic_item = (AS_NODE *)dynamic_ref->lhs;
  ASSERT_EQ_INT(N_ITEM, dynamic_item->nodetype);
  ASSERT_EQ_INT(V_LAYER, node_value((AS_NODE *)dynamic_item->lhs)->valtype);
  ASSERT_TRUE(strcmp("players", node_value((AS_NODE *)dynamic_item->lhs)->value.s) == 0);
  AS_NODE *dynamic_tail = (AS_NODE *)dynamic_item->rhs;
  ASSERT_EQ_INT(N_ITEM, dynamic_tail->nodetype);
  AS_NODE *dynamic_deref = (AS_NODE *)dynamic_tail->lhs;
  ASSERT_EQ_INT(N_DEREF, dynamic_deref->nodetype);
  ASSERT_EQ_INT(V_LOCAL, node_value((AS_NODE *)dynamic_deref->lhs)->valtype);
  ASSERT_TRUE(strcmp("@index", node_value((AS_NODE *)dynamic_deref->lhs)->value.s) == 0);

  expr = (AS_NODE *)stmts->stmts[5]->lhs;
  ASSERT_EQ_INT(N_LIST, expr->nodetype);
  AS_NODE *nested_call = (AS_NODE *)list_element(expr, 0)->lhs;
  ASSERT_EQ_INT(N_CALL, nested_call->nodetype);
  ASSERT_EQ_INT(N_DEREF, ((AS_NODE *)((AS_NODE *)nested_call->lhs)->lhs)->nodetype);

  as_delete(root);

  parse_lists_fails("#[1 2];");
  parse_lists_fails("#[1;");
  parse_lists_fails("#[1,];");
  parse_lists_fails("# [1];");
  parse_lists_fails("&1;");
}
