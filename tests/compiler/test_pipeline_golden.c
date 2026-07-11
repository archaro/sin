#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "error.h"
#include "compiler/lower.h"
#include "compiler/semant.h"
#include "test_assert.h"
#include "test_helpers.h"
#include "shared/test_pipeline_cases.h"

static void run_ast_case(const PipelineGoldenCase *tc) {
  ASSERT_NOT_NULL(tc->build_ast);
  AS_NODE *root = tc->build_ast();
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
  if (rc != ERR_NOERROR) {
    TEST_FAILF("pipeline case %s failed emission: %s", tc->name,
               errdetail ? errdetail : "<no diagnostic>");
  }
  ASSERT_TRUE(errdetail == NULL);

  size_t actual_len = (size_t)(out.nextbyte - out.bytecode);
  size_t expected_len = 0;
  uint8_t *expected = load_hex_fixture(tc->fixture_path, &expected_len);
  assert_bytes_equal_with_diag(expected, expected_len, out.bytecode, actual_len, tc->name);

  free(expected);
  free(out.bytecode);
  ir_destroy_unit(ir);
  sem_delete_ctx(sem);
  as_delete(root);
}

static AS_NODE *build_many_locals_with_duplicate_program(void) {
  AS_NODE *list = as_new_stmtlist_node();
  char name[32];

  for (int i = 0; i < 120; i++) {
    snprintf(name, sizeof(name), "local_%03d", i);
    list = as_stmtlist_append(list, t_node(N_ASSLOCAL, t_local(name), t_int(i)));
  }

  list = as_stmtlist_append(list, t_node(N_ASSLOCAL, t_local("local_057"), t_int(999)));
  list = as_stmtlist_append(list, t_node(N_EXPRSTMT, t_local("local_057"), NULL));
  list = as_stmtlist_append(list, t_node(N_EXPRSTMT, t_local("local_119"), NULL));
  return list;
}

void test_pipeline_golden(void) {
  size_t case_count = 0;
  const PipelineGoldenCase *cases = pipeline_cases_for_layers(PIPELINE_LAYER_AST, &case_count);

  for (size_t i = 0; i < case_count; i++) {
    run_ast_case(&cases[i]);
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
