#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "error.h"
#include "lower.h"
#include "semant.h"
#include "test_assert.h"
#include "test_helpers.h"

typedef AS_NODE *(*AstBuilder)(void);

typedef struct {
  const char *name;
  AstBuilder build;
  const char *fixture_path;
} GoldenCase;

static AS_NODE *v_int(int64_t n) { return t_int(n); }
static AS_NODE *v_str(const char *s) { return as_new_valnode(V_STR, strdup(s)); }

static void run_case(const GoldenCase *tc) {
  AS_NODE *root = tc->build();
  ASSERT_NOT_NULL(root);

  SEM_CTX *sem = sem_create_ctx();
  ASSERT_NOT_NULL(sem);
  char *errdetail = NULL;
  int8_t rc = sem_check_locals(root, &errdetail, sem);
  ASSERT_EQ_INT(ERR_NOERROR, rc);
  ASSERT_TRUE(errdetail == NULL);

  IR_Unit *ir = NULL;
  rc = lower_ast_to_ir(root, sem, &ir, &errdetail);
  ASSERT_EQ_INT(ERR_NOERROR, rc);
  ASSERT_TRUE(errdetail == NULL);
  ASSERT_NOT_NULL(ir);

  OUTPUT_t out = {0};
  out.maxsize = 64;
  out.bytecode = malloc(out.maxsize);
  out.nextbyte = out.bytecode;
  ASSERT_NOT_NULL(out.bytecode);

  rc = t_emit_bytecode(ir, (uint8_t)sem->count, 0, &out, &errdetail);
  ASSERT_EQ_INT(ERR_NOERROR, rc);
  ASSERT_TRUE(errdetail == NULL);

  size_t expected_len = 0;
  uint8_t *expected = load_hex_fixture(tc->fixture_path, &expected_len);
  size_t actual_len = (size_t)(out.nextbyte - out.bytecode);
  ASSERT_EQ_INT((int)expected_len, (int)actual_len);
  ASSERT_EQ_INT(0, memcmp(expected, out.bytecode, expected_len));

  free(expected);
  free(out.bytecode);
  ir_destroy_unit(ir);
  sem_delete_ctx(sem);
  as_delete(root);
}

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

static AS_NODE *build_many_locals_with_duplicate_program(void) {
  AS_NODE *list = as_new_stmtlist_node();
  char name[32];

  for (int i = 0; i < 120; i++) {
    snprintf(name, sizeof(name), "local_%03d", i);
    list = as_stmtlist_append(list, t_node(N_ASSLOCAL, t_local(name), v_int(i)));
  }

  list = as_stmtlist_append(list, t_node(N_ASSLOCAL, t_local("local_057"), v_int(999)));
  list = as_stmtlist_append(list, t_node(N_EXPRSTMT, t_local("local_057"), NULL));
  list = as_stmtlist_append(list, t_node(N_EXPRSTMT, t_local("local_119"), NULL));
  return list;
}

void test_pipeline_golden(void) {
  const GoldenCase cases[] = {
      {"int_literal", build_int_literal_program, "tests/fixtures/int_literal.hex"},
      {"string_literal", build_string_literal_program, "tests/fixtures/string_literal.hex"},
      {"locals_store_load", build_locals_store_load_program, "tests/fixtures/locals_store_load.hex"},
      {"arithmetic_add", build_arithmetic_program, "tests/fixtures/arithmetic_add.hex"},
      {"boolean_compare", build_boolean_compare_program, "tests/fixtures/boolean_compare.hex"},
      {"simple_if", build_simple_if_program, "tests/fixtures/simple_if.hex"},
  };

  for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
    run_case(&cases[i]);
  }
}

void test_pipeline_large_local_lookup_duplicate(void) {
  AS_NODE *root = build_many_locals_with_duplicate_program();
  ASSERT_NOT_NULL(root);

  SEM_CTX *sem = sem_create_ctx();
  ASSERT_NOT_NULL(sem);
  char *errdetail = NULL;
  int8_t rc = sem_check_locals(root, &errdetail, sem);
  ASSERT_EQ_INT(ERR_NOERROR, rc);
  ASSERT_TRUE(errdetail == NULL);
  ASSERT_EQ_INT(120, (int)sem->count);

  uint8_t idx = 255;
  ASSERT_TRUE(sem_get_local_index(sem, "local_057", &idx));
  ASSERT_EQ_INT(57, (int)idx);
  ASSERT_TRUE(sem_get_local_index(sem, "local_119", &idx));
  ASSERT_EQ_INT(119, (int)idx);

  IR_Unit *ir = NULL;
  rc = lower_ast_to_ir(root, sem, &ir, &errdetail);
  ASSERT_EQ_INT(ERR_NOERROR, rc);
  ASSERT_TRUE(errdetail == NULL);
  ASSERT_NOT_NULL(ir);

  ir_destroy_unit(ir);
  sem_delete_ctx(sem);
  as_delete(root);
}
