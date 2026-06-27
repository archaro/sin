#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "absyn.h"
#include "error.h"
#include "parse_input.h"
#include "parser.h"
#include "floatconv.h"
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
  AS_NODE *root = parse_ok("1.0; 0.5; 1.25e2;");
  ASSERT_EQ_INT(N_STMTLIST, root->nodetype);
  AS_STMTLIST *list = (AS_STMTLIST *)root->lhs;
  ASSERT_EQ_INT(3, list->count);

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

  stmt = list->stmts[2];
  ASSERT_EQ_INT(N_EXPRSTMT, stmt->nodetype);
  value_node = (AS_NODE *)stmt->lhs;
  ASSERT_EQ_INT(N_VALUE, value_node->nodetype);
  value = (AS_VALUE *)value_node->lhs;
  ASSERT_EQ_INT(V_FLOAT, value->valtype);
  ASSERT_TRUE(value->value.f_bits == bits_for_double(125.0));

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

static void assert_parse_bits(const char *literal, uint64_t expected) {
  uint64_t bits = 0;
  char *errdetail = NULL;
  ASSERT_TRUE(sin_parse_binary64_bits(literal, &bits, &errdetail));
  ASSERT_TRUE(errdetail == NULL);
  ASSERT_TRUE(bits == expected);
}

static void assert_parse_rejected(const char *literal) {
  uint64_t bits = 0;
  char *errdetail = NULL;
  ASSERT_TRUE(!sin_parse_binary64_bits(literal, &bits, &errdetail));
  ASSERT_NOT_NULL(errdetail);
  free(errdetail);
}

void test_floatconv_binary64_edge_cases(void) {
  /* Exact integers around 2^53. */
  assert_parse_bits("9007199254740991.0", UINT64_C(0x433fffffffffffff));
  assert_parse_bits("9007199254740992.0", UINT64_C(0x4340000000000000));
  assert_parse_bits("9007199254740993.0", UINT64_C(0x4340000000000000));
  assert_parse_bits("9007199254740994.0", UINT64_C(0x4340000000000001));

  /* Smallest positive subnormal and neighboring underflow cases. */
  assert_parse_bits("4.9406564584124654e-324", UINT64_C(0x0000000000000001));
  assert_parse_bits("2.4703282292062327e-324", UINT64_C(0x0000000000000000));
  assert_parse_bits("-2.4703282292062327e-324", UINT64_C(0x8000000000000000));

  /* Largest finite double and explicit overflow to infinity. */
  assert_parse_bits("1.7976931348623157e308", UINT64_C(0x7fefffffffffffff));
  assert_parse_bits("1.7976931348623159e308", UINT64_C(0x7ff0000000000000));

  /* Halfway/tie-to-even rounding cases. */
  assert_parse_bits("1.00000000000000011102230246251565404236316680908203125", UINT64_C(0x3ff0000000000000));
  assert_parse_bits("1.00000000000000033306690738754696212708950042724609375", UINT64_C(0x3ff0000000000002));

  /* Negative zero literal. */
  assert_parse_bits("-0.0", UINT64_C(0x8000000000000000));

  /* Special spellings and locale-specific decimal commas are rejected. */
  assert_parse_rejected("nan");
  assert_parse_rejected("inf");
  assert_parse_rejected("infinity");
  assert_parse_rejected("1,5");
}
