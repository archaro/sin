#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "compiler/absyn.h"
#include "error.h"
#include "compiler/parse_input.h"
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
  AS_NODE *root = parse_ok("1.0; 0.5; 1.25e2; 6.022E+23; 1.0e-3;");
  ASSERT_EQ_INT(N_STMTLIST, root->nodetype);
  AS_STMTLIST *list = (AS_STMTLIST *)root->lhs;
  ASSERT_EQ_INT(5, list->count);

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

  stmt = list->stmts[3];
  ASSERT_EQ_INT(N_EXPRSTMT, stmt->nodetype);
  value_node = (AS_NODE *)stmt->lhs;
  ASSERT_EQ_INT(N_VALUE, value_node->nodetype);
  value = (AS_VALUE *)value_node->lhs;
  ASSERT_EQ_INT(V_FLOAT, value->valtype);
  ASSERT_TRUE(value->value.f_bits == bits_for_double(6.022e23));

  stmt = list->stmts[4];
  ASSERT_EQ_INT(N_EXPRSTMT, stmt->nodetype);
  value_node = (AS_NODE *)stmt->lhs;
  ASSERT_EQ_INT(N_VALUE, value_node->nodetype);
  value = (AS_VALUE *)value_node->lhs;
  ASSERT_EQ_INT(V_FLOAT, value->valtype);
  ASSERT_TRUE(value->value.f_bits == bits_for_double(1.0e-3));

  as_delete(root);
}

void test_parser_float_literals_unary_minus_preserves_float_literals(void) {
  AS_NODE *root = parse_ok("-1.5; -0.0;");
  ASSERT_EQ_INT(N_STMTLIST, root->nodetype);
  AS_STMTLIST *list = (AS_STMTLIST *)root->lhs;
  ASSERT_EQ_INT(2, list->count);

  AS_NODE *stmt = list->stmts[0];
  ASSERT_EQ_INT(N_EXPRSTMT, stmt->nodetype);
  AS_NODE *value_node = (AS_NODE *)stmt->lhs;
  ASSERT_EQ_INT(N_VALUE, value_node->nodetype);
  AS_VALUE *value = (AS_VALUE *)value_node->lhs;
  ASSERT_EQ_INT(V_FLOAT, value->valtype);
  ASSERT_TRUE(value->value.f_bits == bits_for_double(-1.5));

  stmt = list->stmts[1];
  ASSERT_EQ_INT(N_EXPRSTMT, stmt->nodetype);
  value_node = (AS_NODE *)stmt->lhs;
  ASSERT_EQ_INT(N_VALUE, value_node->nodetype);
  value = (AS_VALUE *)value_node->lhs;
  ASSERT_EQ_INT(V_FLOAT, value->valtype);
  ASSERT_TRUE(value->value.f_bits == UINT64_C(0x8000000000000000));

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
  parse_fails("1.0e;");
  parse_fails("1.0e+;");
  parse_fails("1.2.3;");
}

void test_parser_integer_literals_range(void) {
  AS_NODE *root = parse_ok("0; 9223372036854775807; -42;");
  AS_STMTLIST *list = (AS_STMTLIST *)root->lhs;
  ASSERT_EQ_INT(3, list->count);
  AS_VALUE *value = (AS_VALUE *)((AS_NODE *)list->stmts[0]->lhs)->lhs;
  ASSERT_EQ_INT(V_INT, value->valtype);
  ASSERT_EQ_INT(0, value->value.i);
  value = (AS_VALUE *)((AS_NODE *)list->stmts[1]->lhs)->lhs;
  ASSERT_EQ_INT(V_INT, value->valtype);
  ASSERT_EQ_INT(INT64_MAX, value->value.i);
  value = (AS_VALUE *)((AS_NODE *)list->stmts[2]->lhs)->lhs;
  ASSERT_EQ_INT(V_INT, value->valtype);
  ASSERT_EQ_INT(-42, value->value.i);
  as_delete(root);

  const char *out_of_range[] = {
    "9223372036854775808;",
    "99999999999999999999999999999999999999999999999999;",
  };
  for (size_t i = 0; i < sizeof(out_of_range) / sizeof(out_of_range[0]); ++i) {
    AS_NODE *absyn = NULL;
    char *errdetail = NULL;
    ParseInput input = {out_of_range[i], strlen(out_of_range[i]),
                        "integer-literal-test.src"};
    ASSERT_TRUE(parse_source(&input, &absyn, &errdetail) != ERR_NOERROR);
    ASSERT_TRUE(absyn == NULL);
    ASSERT_NOT_NULL(errdetail);
    ASSERT_TRUE(strcmp(errdetail,
                       "parser: integer literal out of range (expected 0..9223372036854775807)") == 0);
    free(errdetail);
  }
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

static void assert_format_text(uint64_t bits, const char *expected) {
  double value = 0.0;
  memcpy(&value, &bits, sizeof(value));
  char buf[64];
  ASSERT_TRUE(sin_format_binary64_buf(value, buf, sizeof(buf)));
  ASSERT_TRUE(strcmp(buf, expected) == 0);
}

static void assert_format_roundtrip(uint64_t bits) {
  double value = 0.0;
  memcpy(&value, &bits, sizeof(value));
  char buf[64];
  ASSERT_TRUE(sin_format_binary64_buf(value, buf, sizeof(buf)));
  uint64_t reparsed = 0;
  char *errdetail = NULL;
  ASSERT_TRUE(sin_parse_binary64_bits(buf, &reparsed, &errdetail));
  ASSERT_TRUE(errdetail == NULL);
  ASSERT_TRUE(reparsed == bits);
}

void test_floatconv_binary64_formatting(void) {
  assert_format_text(UINT64_C(0x0000000000000000), "0.0");
  assert_format_text(UINT64_C(0x8000000000000000), "-0.0");
  assert_format_text(UINT64_C(0x7ff0000000000000), "inf");
  assert_format_text(UINT64_C(0xfff0000000000000), "-inf");
  assert_format_text(UINT64_C(0x7ff8000000000042), "nan");
  assert_format_text(UINT64_C(0x3ff0000000000000), "1.0");
  assert_format_text(UINT64_C(0x3ff8000000000000), "1.5");

  char small[3];
  double one = 1.0;
  ASSERT_TRUE(!sin_format_binary64_buf(one, small, sizeof(small)));
  char *allocated = sin_format_binary64(one);
  ASSERT_NOT_NULL(allocated);
  ASSERT_TRUE(strcmp(allocated, "1.0") == 0);
  free(allocated);
}

void test_floatconv_binary64_format_roundtrip(void) {
  const uint64_t cases[] = {
    UINT64_C(0x3fb999999999999a), /* 0.1 */
    UINT64_C(0x400921fb54442d18), /* pi-ish */
    UINT64_C(0x7fefffffffffffff),
    UINT64_C(0x0000000000000001),
    UINT64_C(0x0010000000000000),
    UINT64_C(0x433fffffffffffff),
    UINT64_C(0x4340000000000001),
    UINT64_C(0xbff0000000000000),
    UINT64_C(0xc008000000000000),
  };
  for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
    assert_format_roundtrip(cases[i]);
  }
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
