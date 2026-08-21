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
  ASSERT_TRUE(absyn == NULL);
  ASSERT_TRUE(errdetail != NULL);
  ASSERT_TRUE(strcmp(errdetail, "parser: NUL byte in source is not allowed") == 0);
  free(errdetail);

  const char nul_escape[] = "\"\\000\";";
  ParseInput nul_escape_input = {nul_escape, sizeof(nul_escape) - 1,
                                 "nul-escape.src"};
  absyn = NULL;
  errdetail = NULL;
  rc = parse_source(&nul_escape_input, &absyn, &errdetail);
  ASSERT_TRUE(rc != ERR_NOERROR);
  ASSERT_TRUE(absyn == NULL);
  ASSERT_TRUE(errdetail != NULL);
  ASSERT_TRUE(strcmp(errdetail, "NUL byte escape \\000 is not allowed.") == 0);
  free(errdetail);

  const char nonzero_escape[] = "\"\\077\";";
  ParseInput nonzero_escape_input = {nonzero_escape,
                                     sizeof(nonzero_escape) - 1,
                                     "nonzero-escape.src"};
  absyn = NULL;
  errdetail = NULL;
  rc = parse_source(&nonzero_escape_input, &absyn, &errdetail);
  ASSERT_EQ_INT(ERR_NOERROR, rc);
  ASSERT_TRUE(errdetail == NULL);
  ASSERT_NOT_NULL(absyn);
  AS_STMTLIST *escape_list = (AS_STMTLIST *)absyn->lhs;
  AS_VALUE *escape_value = (AS_VALUE *)((AS_NODE *)escape_list->stmts[0]->lhs)->lhs;
  ASSERT_EQ_INT(V_STR, escape_value->valtype);
  ASSERT_EQ_INT(077, (unsigned char)escape_value->value.s[0]);
  ASSERT_EQ_INT('\0', escape_value->value.s[1]);
  as_delete(absyn);

  const char raw_source[] =
      "\"\"\"one\\ntwo\n"
      "\"quoted\" C:\\temp\"\"\";";
  ParseInput raw_input = {raw_source, sizeof(raw_source) - 1, "raw-string.src"};
  absyn = NULL;
  errdetail = NULL;
  rc = parse_source(&raw_input, &absyn, &errdetail);
  ASSERT_EQ_INT(ERR_NOERROR, rc);
  ASSERT_TRUE(errdetail == NULL);
  ASSERT_NOT_NULL(absyn);
  AS_STMTLIST *raw_list = (AS_STMTLIST *)absyn->lhs;
  ASSERT_EQ_INT(1, raw_list->count);
  AS_VALUE *raw_value =
      (AS_VALUE *)((AS_NODE *)raw_list->stmts[0]->lhs)->lhs;
  ASSERT_EQ_INT(V_STR, raw_value->valtype);
  ASSERT_TRUE(strcmp(raw_value->value.s,
                     "one\\ntwo\n\"quoted\" C:\\temp") == 0);
  as_delete(absyn);

  const char raw_eof[] = "\"\"\"unterminated";
  ParseInput raw_eof_input = {raw_eof, sizeof(raw_eof) - 1, "raw-eof.src"};
  absyn = NULL;
  errdetail = NULL;
  rc = parse_source(&raw_eof_input, &absyn, &errdetail);
  ASSERT_EQ_INT(ERR_COMP_UNKNOWNCHAR, rc);
  ASSERT_TRUE(absyn == NULL);
  ASSERT_NOT_NULL(errdetail);
  ASSERT_TRUE(strcmp(errdetail, "EOF in raw string.") == 0);
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

  const char crlf_code_source[] = "target = code (\r\nreturn 1;\r\n);";
  ParseInput crlf_code_input = {crlf_code_source, sizeof(crlf_code_source) - 1,
                                "crlf-code.src"};
  absyn = NULL;
  errdetail = NULL;
  rc = parse_source(&crlf_code_input, &absyn, &errdetail);
  ASSERT_EQ_INT(ERR_NOERROR, rc);
  ASSERT_TRUE(errdetail == NULL);
  AS_STMTLIST *crlf_code_list = (AS_STMTLIST *)absyn->lhs;
  ASSERT_EQ_INT(1, crlf_code_list->count);
  AS_NODE *crlf_assignment = crlf_code_list->stmts[0];
  ASSERT_EQ_INT(N_ASSITEM, crlf_assignment->nodetype);
  AS_NODE *crlf_code = (AS_NODE *)crlf_assignment->rhs;
  ASSERT_EQ_INT(N_CODE, crlf_code->nodetype);
  AS_NODE *crlf_code_body = (AS_NODE *)crlf_code->rhs;
  ASSERT_EQ_INT(N_VALUE, crlf_code_body->nodetype);
  AS_VALUE *crlf_code_value = (AS_VALUE *)crlf_code_body->lhs;
  ASSERT_EQ_INT(V_STR, crlf_code_value->valtype);
  ASSERT_TRUE(strchr(crlf_code_value->value.s, '\r') == NULL);
  ASSERT_TRUE(strcmp(crlf_code_value->value.s, " return 1; ") == 0);
  as_delete(absyn);

  const char crlf_embedded_string_source[] =
      "target = code (\r\n\"first\r\nsecond\"\r\n);";
  ParseInput crlf_embedded_string_input = {
      crlf_embedded_string_source, sizeof(crlf_embedded_string_source) - 1,
      "crlf-embedded-string.src"};
  absyn = NULL;
  errdetail = NULL;
  rc = parse_source(&crlf_embedded_string_input, &absyn, &errdetail);
  ASSERT_EQ_INT(ERR_COMP_UNKNOWNCHAR, rc);
  ASSERT_TRUE(absyn == NULL);
  ASSERT_NOT_NULL(errdetail);
  ASSERT_TRUE(strcmp(errdetail, "Newline in string.") == 0);
  free(errdetail);

  const char raw_code_source[] =
      "target = code ( @x = \"\"\"left )\nright\"\"\"; return @x; );";
  ParseInput raw_code_input = {raw_code_source, sizeof(raw_code_source) - 1,
                               "raw-code-string.src"};
  absyn = NULL;
  errdetail = NULL;
  rc = parse_source(&raw_code_input, &absyn, &errdetail);
  ASSERT_EQ_INT(ERR_NOERROR, rc);
  ASSERT_TRUE(errdetail == NULL);
  ASSERT_NOT_NULL(absyn);
  AS_STMTLIST *raw_code_list = (AS_STMTLIST *)absyn->lhs;
  ASSERT_EQ_INT(1, raw_code_list->count);
  AS_NODE *raw_assignment = raw_code_list->stmts[0];
  ASSERT_EQ_INT(N_ASSITEM, raw_assignment->nodetype);
  AS_NODE *raw_code = (AS_NODE *)raw_assignment->rhs;
  ASSERT_EQ_INT(N_CODE, raw_code->nodetype);
  AS_NODE *raw_code_body = (AS_NODE *)raw_code->rhs;
  ASSERT_EQ_INT(N_VALUE, raw_code_body->nodetype);
  AS_VALUE *raw_code_value = (AS_VALUE *)raw_code_body->lhs;
  ASSERT_EQ_INT(V_STR, raw_code_value->valtype);
  ASSERT_TRUE(strstr(raw_code_value->value.s,
                     "\"\"\"left )\nright\"\"\"") != NULL);
  ASSERT_TRUE(strstr(raw_code_value->value.s, "return @x;") != NULL);
  as_delete(absyn);

  const char raw_code_eof_source[] =
      "target = code ( @x = \"\"\"unterminated";
  ParseInput raw_code_eof_input = {
      raw_code_eof_source, sizeof(raw_code_eof_source) - 1,
      "raw-code-eof.src"};
  absyn = NULL;
  errdetail = NULL;
  rc = parse_source(&raw_code_eof_input, &absyn, &errdetail);
  ASSERT_EQ_INT(ERR_COMP_UNKNOWNCHAR, rc);
  ASSERT_TRUE(absyn == NULL);
  ASSERT_NOT_NULL(errdetail);
  ASSERT_TRUE(strcmp(errdetail,
                     "Unterminated raw string inside code(...) body.") == 0);
  free(errdetail);

  const char crlf_error_source[] = "@x = 1;\r\n^;";
  ParseInput crlf_error_input = {crlf_error_source,
                                 sizeof(crlf_error_source) - 1,
                                 "crlf-location.src"};
  CompilerDiagnostic crlf_diag = {0};
  SCANNER_STATE_t crlf_state = {0};
  absyn = NULL;
  errdetail = NULL;
  rc = parse_source_compiler_diag(&crlf_error_input, &absyn, &errdetail,
                                  &crlf_diag, &crlf_state);
  ASSERT_EQ_INT(ERR_COMP_UNKNOWNCHAR, rc);
  ASSERT_EQ_INT(2, crlf_state.line);
  ASSERT_EQ_INT(1, crlf_state.column);
  ASSERT_EQ_INT(1, crlf_state.span);
  ASSERT_EQ_INT(2, crlf_diag.line);
  ASSERT_EQ_INT(1, crlf_diag.column);
  ASSERT_TRUE(crlf_diag.has_loc);
  free(errdetail);
  free(crlf_state.offending_token);
  compiler_diag_reset(&crlf_diag);

  parse_lists_fails("@x = 1;\r@y = 2;");

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

  source_len = literal_len + 7;
  char *large_raw = malloc(source_len + 1);
  ASSERT_NOT_NULL(large_raw);
  memcpy(large_raw, "\"\"\"", 3);
  memset(large_raw + 3, 'x', literal_len);
  memcpy(large_raw + 3 + literal_len, "\"\"\";", 4);
  large_raw[source_len] = '\0';
  ParseInput large_raw_input = {large_raw, source_len, "large-raw-string.src"};
  absyn = NULL;
  errdetail = NULL;
  rc = parse_source(&large_raw_input, &absyn, &errdetail);
  ASSERT_EQ_INT(ERR_COMP_UNKNOWNCHAR, rc);
  ASSERT_NOT_NULL(errdetail);
  ASSERT_TRUE(strstr(errdetail, "String literal too long.") != NULL);
  free(errdetail);
  free(large_raw);
}

void test_parser_compound_spans_preserve_construct_start(void) {
  const char source[] =
      "if 1 then\n"
      "  @x = 2;\n"
      "endif;\n"
      "@y = 1 + 22;";
  AS_NODE *absyn = parse_lists_ok(source);
  AS_STMTLIST *stmts = (AS_STMTLIST *)absyn->lhs;
  ASSERT_EQ_INT(2, stmts->count);

  AS_NODE *ifstmt = stmts->stmts[0];
  ASSERT_EQ_INT(N_IFSTMT, ifstmt->nodetype);
  ASSERT_EQ_INT(1, ifstmt->span.line);
  ASSERT_EQ_INT(1, ifstmt->span.column);
  ASSERT_EQ_INT(9, ifstmt->span.span);

  AS_NODE *assignment = stmts->stmts[1];
  ASSERT_EQ_INT(N_ASSLOCAL, assignment->nodetype);
  ASSERT_EQ_INT(4, assignment->span.line);
  ASSERT_EQ_INT(1, assignment->span.column);
  ASSERT_EQ_INT(11, assignment->span.span);
  AS_NODE *sum = (AS_NODE *)assignment->rhs;
  ASSERT_EQ_INT(N_ADD, sum->nodetype);
  ASSERT_EQ_INT(4, sum->span.line);
  ASSERT_EQ_INT(6, sum->span.column);
  ASSERT_EQ_INT(6, sum->span.span);
  AS_NODE *left = (AS_NODE *)sum->lhs;
  AS_NODE *right = (AS_NODE *)sum->rhs;
  ASSERT_EQ_INT(6, left->span.column);
  ASSERT_EQ_INT(1, left->span.span);
  ASSERT_EQ_INT(10, right->span.column);
  ASSERT_EQ_INT(2, right->span.span);
  as_delete(absyn);
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

void test_parser_foreach_allocation_failures(void) {
  const char source[] = "foreach @x in #[1] do @y = 2; endfor;";
  ParseInput input = {source, sizeof(source) - 1, "foreach-parser-failure.src"};

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

void test_parser_ast_node_budget_stops_construction_early(void) {
  char source[256];
  size_t used = 0;
  for (int i = 0; i < 40; ++i) {
    memcpy(source + used, "1;", 2);
    used += 2;
  }
  source[used] = '\0';
  ParseInput input = {source, used, "node-budget.src"};
  AS_NODE *absyn = NULL;
  char *errdetail = NULL;
  SCANNER_STATE_t state = {0};

  int8_t rc = parse_source_diag_with_node_limit(&input, &absyn, &errdetail,
                                                &state, 32);

  ASSERT_EQ_INT(ERR_COMP_SYNTAX, rc);
  ASSERT_TRUE(absyn == NULL);
  ASSERT_NOT_NULL(errdetail);
  ASSERT_TRUE(strcmp(errdetail, "AST node budget exceeded") == 0);
  ASSERT_EQ_INT(32, state.ast_node_count);
  ASSERT_EQ_INT(32, state.ast_node_limit);
  free(errdetail);
  free(state.offending_token);

  errdetail = NULL;
  ASSERT_EQ_INT(ERR_NOERROR, parse_source(&input, &absyn, &errdetail));
  ASSERT_NOT_NULL(absyn);
  ASSERT_TRUE(errdetail == NULL);
  as_delete(absyn);
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

void test_parser_foreach_ast(void) {
  AS_NODE *empty_root = parse_lists_ok("foreach @x in #[] do endfor;");
  AS_STMTLIST *empty_stmts = (AS_STMTLIST *)empty_root->lhs;
  ASSERT_EQ_INT(1, empty_stmts->count);
  ASSERT_EQ_INT(N_FOREACH, empty_stmts->stmts[0]->nodetype);
  ASSERT_EQ_INT(N_STMTLIST, ((AS_NODE *)empty_stmts->stmts[0]->rhs)->nodetype);
  ASSERT_EQ_INT(0, ((AS_STMTLIST *)((AS_NODE *)empty_stmts->stmts[0]->rhs)->lhs)->count);
  as_delete(empty_root);

  AS_NODE *root = parse_lists_ok(
      "foreach @x in #[1, 2] do @y = 3; endfor;"
      "foreach @outer in #[1] do foreach @inner in players{1} do endfor; endfor;");
  AS_STMTLIST *stmts = (AS_STMTLIST *)root->lhs;
  ASSERT_EQ_INT(2, stmts->count);

  AS_NODE *foreach_node = stmts->stmts[0];
  ASSERT_EQ_INT(N_FOREACH, foreach_node->nodetype);
  AS_NODE *spec = (AS_NODE *)foreach_node->lhs;
  ASSERT_EQ_INT(N_FOREACHSPEC, spec->nodetype);
  ASSERT_EQ_INT(V_LOCAL, node_value((AS_NODE *)spec->lhs)->valtype);
  ASSERT_TRUE(strcmp("@x", node_value((AS_NODE *)spec->lhs)->value.s) == 0);
  AS_NODE *list = (AS_NODE *)spec->rhs;
  ASSERT_EQ_INT(N_LIST, list->nodetype);
  ASSERT_EQ_INT(V_INT, node_value(list_element(list, 0)->lhs)->valtype);
  ASSERT_EQ_INT(V_INT, node_value(list_element(list, 1)->lhs)->valtype);
  ASSERT_EQ_INT(N_STMTLIST, ((AS_NODE *)foreach_node->rhs)->nodetype);
  AS_STMTLIST *body = (AS_STMTLIST *)((AS_NODE *)foreach_node->rhs)->lhs;
  ASSERT_EQ_INT(1, body->count);

  foreach_node = stmts->stmts[1];
  ASSERT_EQ_INT(N_STMTLIST, ((AS_NODE *)foreach_node->rhs)->nodetype);
  body = (AS_STMTLIST *)((AS_NODE *)foreach_node->rhs)->lhs;
  ASSERT_EQ_INT(1, body->count);
  AS_NODE *nested = body->stmts[0];
  ASSERT_EQ_INT(N_FOREACH, nested->nodetype);
  spec = (AS_NODE *)nested->lhs;
  ASSERT_EQ_INT(N_CALL, ((AS_NODE *)spec->rhs)->nodetype);
  ASSERT_EQ_INT(0, ((AS_STMTLIST *)((AS_NODE *)nested->rhs)->lhs)->count);
  as_delete(root);

  parse_lists_fails("foreach foo in #[1] do endfor;");
  parse_lists_fails("foreach @x #[1] do endfor;");
  parse_lists_fails("foreach @x in #[1] do @y = 1;");
  parse_lists_fails("foreach @x in #[1] @y = 1; endfor;");
}
