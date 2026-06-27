#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "absyn.h"
#include "error.h"
#include "parse_input.h"
#include "parser.h"
#include "test_assert.h"

static AS_NODE *parse_ok(const char *source) {
  AS_NODE *absyn = NULL;
  char *errdetail = NULL;
  ParseInput input = {source, strlen(source), "float-literal-test.src"};
  int8_t rc = parse_source(&input, &absyn, &errdetail);
  ASSERT_EQ_INT(ERR_NOERROR, rc);
  ASSERT_TRUE(errdetail == NULL);
  ASSERT_NOT_NULL(absyn);
  return absyn;
}

static void parse_fails(const char *source) {
  AS_NODE *absyn = NULL;
  char *errdetail = NULL;
  ParseInput input = {source, strlen(source), "float-literal-test.src"};
  int8_t rc = parse_source(&input, &absyn, &errdetail);
  ASSERT_TRUE(rc != ERR_NOERROR);
  ASSERT_TRUE(errdetail != NULL);
  free(errdetail);
  if (absyn != NULL) {
    as_delete(absyn);
  }
}

static AS_NODE *single_stmt(AS_NODE *root) {
  ASSERT_EQ_INT(N_STMTLIST, root->nodetype);
  AS_STMTLIST *list = (AS_STMTLIST *)root->lhs;
  ASSERT_EQ_INT(1, list->count);
  ASSERT_NOT_NULL(list->stmts[0]);
  return list->stmts[0];
}

static uint64_t bits_for_double(double value) {
  uint64_t bits = 0;
  memcpy(&bits, &value, sizeof(bits));
  return bits;
}

void test_parser_float_literals_decimal_forms(void) {
  AS_NODE *root = parse_ok("1.0; 0.5;");
  ASSERT_EQ_INT(N_STMTLIST, root->nodetype);
  AS_STMTLIST *list = (AS_STMTLIST *)root->lhs;
  ASSERT_EQ_INT(2, list->count);

  AS_NODE *stmt = list->stmts[0];
  ASSERT_EQ_INT(N_EXPRSTMT, stmt->nodetype);
  AS_NODE *value_node = (AS_NODE *)stmt->lhs;
  ASSERT_EQ_INT(N_VALUE, value_node->nodetype);
  AS_VALUE *value = (AS_VALUE *)value_node->lhs;
  ASSERT_EQ_INT(V_FLOAT, value->valtype);
  ASSERT_TRUE(value->value.f_bits == bits_for_double(1.0));

  stmt = list->stmts[1];
  ASSERT_EQ_INT(N_EXPRSTMT, stmt->nodetype);
  value_node = (AS_NODE *)stmt->lhs;
  ASSERT_EQ_INT(N_VALUE, value_node->nodetype);
  value = (AS_VALUE *)value_node->lhs;
  ASSERT_EQ_INT(V_FLOAT, value->valtype);
  ASSERT_TRUE(value->value.f_bits == bits_for_double(0.5));

  as_delete(root);
}

void test_parser_float_literals_integer_still_int(void) {
  AS_NODE *root = parse_ok("42;");
  AS_NODE *stmt = single_stmt(root);
  ASSERT_EQ_INT(N_EXPRSTMT, stmt->nodetype);
  AS_NODE *value_node = (AS_NODE *)stmt->lhs;
  ASSERT_EQ_INT(N_VALUE, value_node->nodetype);
  AS_VALUE *value = (AS_VALUE *)value_node->lhs;
  ASSERT_EQ_INT(V_INT, value->valtype);
  ASSERT_EQ_INT(42, value->value.i);
  as_delete(root);
}

void test_parser_float_literals_item_layers_unchanged(void) {
  AS_NODE *root = parse_ok("foo.bar; .relative; foo.12;");
  ASSERT_EQ_INT(N_STMTLIST, root->nodetype);
  AS_STMTLIST *list = (AS_STMTLIST *)root->lhs;
  ASSERT_EQ_INT(3, list->count);

  AS_NODE *stmt = list->stmts[0];
  ASSERT_EQ_INT(N_EXPRSTMT, stmt->nodetype);
  AS_NODE *call = (AS_NODE *)stmt->lhs;
  ASSERT_EQ_INT(N_CALL, call->nodetype);
  AS_NODE *item = (AS_NODE *)call->lhs;
  ASSERT_EQ_INT(N_ITEM, item->nodetype);
  AS_VALUE *first = (AS_VALUE *)((AS_NODE *)item->lhs)->lhs;
  AS_VALUE *second = (AS_VALUE *)((AS_NODE *)((AS_NODE *)item->rhs)->lhs)->lhs;
  ASSERT_EQ_INT(V_LAYER, first->valtype);
  ASSERT_EQ_INT(V_LAYER, second->valtype);
  ASSERT_TRUE(strcmp("foo", first->value.s) == 0);
  ASSERT_TRUE(strcmp("bar", second->value.s) == 0);

  stmt = list->stmts[1];
  ASSERT_EQ_INT(N_EXPRSTMT, stmt->nodetype);
  call = (AS_NODE *)stmt->lhs;
  ASSERT_EQ_INT(N_CALL, call->nodetype);
  AS_NODE *relitem = (AS_NODE *)call->lhs;
  ASSERT_EQ_INT(N_RELITEM, relitem->nodetype);
  item = (AS_NODE *)relitem->lhs;
  first = (AS_VALUE *)((AS_NODE *)item->lhs)->lhs;
  ASSERT_EQ_INT(V_LAYER, first->valtype);
  ASSERT_TRUE(strcmp("relative", first->value.s) == 0);

  stmt = list->stmts[2];
  ASSERT_EQ_INT(N_EXPRSTMT, stmt->nodetype);
  call = (AS_NODE *)stmt->lhs;
  ASSERT_EQ_INT(N_CALL, call->nodetype);
  item = (AS_NODE *)call->lhs;
  second = (AS_VALUE *)((AS_NODE *)((AS_NODE *)item->rhs)->lhs)->lhs;
  ASSERT_EQ_INT(V_INT, second->valtype);
  ASSERT_EQ_INT(12, second->value.i);

  as_delete(root);
}

void test_parser_float_literals_malformed_rejected(void) {
  parse_fails("1.;");
  parse_fails("1.2.3;");
}
