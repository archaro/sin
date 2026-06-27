#include "test_pipeline_cases.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "test_helpers.h"

static AS_NODE *v_int(int64_t n) { return t_int(n); }
static AS_NODE *v_str(const char *s) { return as_new_valnode(V_STR, strdup(s)); }
static AS_NODE *v_float_bits(uint64_t bits) { return t_node(N_VALUE, as_new_value(V_FLOAT, bits, NULL), NULL); }

static AS_NODE *build_int_literal_program(void) {
  AS_NODE *stmt = t_node(N_EXPRSTMT, v_int(42), NULL);
  return t_stmtlist_with_one(stmt);
}

static AS_NODE *build_string_literal_program(void) {
  AS_NODE *stmt = t_node(N_EXPRSTMT, v_str("hi"), NULL);
  return t_stmtlist_with_one(stmt);
}

static AS_NODE *build_locals_store_load_program(void) {
  AS_NODE *list = as_new_stmtlist_node();
  list = as_stmtlist_append(list, t_node(N_ASSLOCAL, t_local("x"), v_int(7)));
  list = as_stmtlist_append(list, t_node(N_EXPRSTMT, t_local("x"), NULL));
  return list;
}

static AS_NODE *build_arithmetic_program(void) {
  AS_NODE *add = t_node(N_ADD, v_int(2), v_int(3));
  AS_NODE *stmt = t_node(N_EXPRSTMT, add, NULL);
  return t_stmtlist_with_one(stmt);
}

static AS_NODE *build_float_literal_program(void) {
  AS_NODE *stmt = t_node(N_EXPRSTMT, v_float_bits(UINT64_C(0x3ff8000000000000)), NULL);
  return t_stmtlist_with_one(stmt);
}

static AS_NODE *build_mixed_float_arithmetic_program(void) {
  AS_NODE *add = t_node(N_ADD, v_int(2), v_float_bits(UINT64_C(0x3fe0000000000000)));
  AS_NODE *stmt = t_node(N_EXPRSTMT, add, NULL);
  return t_stmtlist_with_one(stmt);
}

static AS_NODE *build_boolean_compare_program(void) {
  AS_NODE *cmp = t_node(N_LT, v_int(1), v_int(2));
  AS_NODE *stmt = t_node(N_EXPRSTMT, cmp, NULL);
  return t_stmtlist_with_one(stmt);
}

static AS_NODE *build_simple_if_program(void) {
  AS_NODE *then_list = as_new_stmtlist_node();
  then_list = as_stmtlist_append(then_list, t_node(N_EXPRSTMT, v_int(9), NULL));
  AS_IF *branch = as_new_if(t_node(N_LT, v_int(1), v_int(2)), then_list, NULL);
  AS_NODE *ifstmt = t_node(N_IFSTMT, branch, NULL);
  AS_NODE *list = as_new_stmtlist_node();
  return as_stmtlist_append(list, ifstmt);
}

static const PipelineGoldenCase PIPELINE_CASES[] = {
    {"int_literal", build_int_literal_program, "42;", "tests/fixtures/int_literal.hex",
     PIPELINE_LAYER_AST | PIPELINE_LAYER_SOURCE},
    {"string_literal", build_string_literal_program, NULL, "tests/fixtures/string_literal.hex",
     PIPELINE_LAYER_AST},
    {"locals_store_load", build_locals_store_load_program, "@x = 7; @x;", "tests/fixtures/locals_store_load.hex",
     PIPELINE_LAYER_AST | PIPELINE_LAYER_SOURCE},
    {"arithmetic_add", build_arithmetic_program, "2 + 3;", "tests/fixtures/arithmetic_add.hex",
     PIPELINE_LAYER_AST | PIPELINE_LAYER_SOURCE},
    {"float_literal", build_float_literal_program, "1.5;", "tests/fixtures/float_literal.hex",
     PIPELINE_LAYER_AST | PIPELINE_LAYER_SOURCE},
    {"mixed_float_arithmetic", build_mixed_float_arithmetic_program, "2 + 0.5;",
     "tests/fixtures/mixed_float_arithmetic.hex", PIPELINE_LAYER_AST | PIPELINE_LAYER_SOURCE},
    {"boolean_compare", build_boolean_compare_program, NULL, "tests/fixtures/boolean_compare.hex",
     PIPELINE_LAYER_AST},
    {"simple_if", build_simple_if_program, NULL, "tests/fixtures/simple_if.hex", PIPELINE_LAYER_AST},
    {"if_elsif_else", NULL, "if 1 < 2 then 9; elsif 0 < 1 then 8; else 7; endif;", "tests/fixtures/if_elsif_else.hex",
     PIPELINE_LAYER_SOURCE},
    {"locals_inc", NULL, "@x = 1; @x++; @x;", "tests/fixtures/locals_inc.hex", PIPELINE_LAYER_SOURCE},
    {"locals_dec", NULL, "@x = 2; @x--; @x;", "tests/fixtures/locals_dec.hex", PIPELINE_LAYER_SOURCE},
    {"libcall_exprstmt", NULL, "sys.log{\"hello\"};", "tests/fixtures/libcall_exprstmt.hex", PIPELINE_LAYER_SOURCE},
    {"item_numeric_layer", NULL, "foo.12;", "tests/fixtures/item_numeric_layer.hex", PIPELINE_LAYER_SOURCE},
};

const PipelineGoldenCase *pipeline_golden_cases(size_t *count) {
  if (count != NULL) {
    *count = sizeof(PIPELINE_CASES) / sizeof(PIPELINE_CASES[0]);
  }
  return PIPELINE_CASES;
}

const PipelineGoldenCase *pipeline_cases_for_layers(unsigned layers, size_t *count) {
  static PipelineGoldenCase filtered[sizeof(PIPELINE_CASES) / sizeof(PIPELINE_CASES[0])];
  size_t total = 0;
  size_t all_count = 0;
  const PipelineGoldenCase *all = pipeline_golden_cases(&all_count);

  for (size_t i = 0; i < all_count; i++) {
    if ((all[i].layers & layers) == layers) {
      filtered[total++] = all[i];
    }
  }

  if (count != NULL) {
    *count = total;
  }
  return filtered;
}
