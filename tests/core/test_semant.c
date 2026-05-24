#include <string.h>
#include <stdlib.h>

#include "error.h"
#include "semant.h"
#include "test_assert.h"
#include "test_helpers.h"

void test_sem_check_locals_reusable_context(void) {
  SEM_CTX *ctx = sem_create_ctx();
  ASSERT_NOT_NULL(ctx);

  AS_NODE *ok_stmt = t_node(N_ASSLOCAL, t_local("defined"), t_int(1));
  AS_NODE *ok_prog = t_stmtlist_with_one(ok_stmt);

  char *errdetail = (char *)0x1;
  int8_t rc = sem_check_locals(ok_prog, &errdetail, ctx);
  ASSERT_EQ_INT(ERR_NOERROR, rc);
  ASSERT_TRUE(errdetail == NULL);

  AS_NODE *bad_stmt = t_node(N_EXPRSTMT, t_local("missing"), NULL);
  AS_NODE *bad_prog = t_stmtlist_with_one(bad_stmt);

  rc = sem_check_locals(bad_prog, &errdetail, ctx);
  ASSERT_EQ_INT(ERR_COMP_LOCALBEFOREDEF, rc);
  ASSERT_NOT_NULL(errdetail);
  ASSERT_TRUE(strcmp(errdetail, "semant: missing") == 0);

  char *first_errdetail = errdetail;

  rc = sem_check_locals(bad_prog, &errdetail, ctx);
  ASSERT_EQ_INT(ERR_COMP_LOCALBEFOREDEF, rc);
  ASSERT_NOT_NULL(errdetail);
  ASSERT_TRUE(strcmp(errdetail, "semant: missing") == 0);
  ASSERT_TRUE(errdetail != first_errdetail);
  free(first_errdetail);
  free(errdetail);

  as_delete(ok_prog);
  as_delete(bad_prog);
  sem_delete_ctx(ctx);
}

void test_sem_duplicate_local_keeps_original_index(void) {
  SEM_CTX *ctx = sem_create_ctx();
  ASSERT_NOT_NULL(ctx);

  AS_NODE *list = as_new_stmtlist_node();
  list = as_stmtlist_append(list, t_node(N_ASSLOCAL, t_local("x"), t_int(1)));
  list = as_stmtlist_append(list, t_node(N_ASSLOCAL, t_local("y"), t_int(2)));
  list = as_stmtlist_append(list, t_node(N_ASSLOCAL, t_local("x"), t_int(3)));

  char *errdetail = NULL;
  int8_t rc = sem_check_locals(list, &errdetail, ctx);
  ASSERT_EQ_INT(ERR_NOERROR, rc);
  ASSERT_TRUE(errdetail == NULL);
  ASSERT_EQ_INT(2, (int)ctx->count);

  uint8_t idx = 255;
  ASSERT_TRUE(sem_get_local_index(ctx, "x", &idx));
  ASSERT_EQ_INT(0, (int)idx);
  ASSERT_TRUE(sem_get_local_index(ctx, "y", &idx));
  ASSERT_EQ_INT(1, (int)idx);

  as_delete(list);
  sem_delete_ctx(ctx);
}


void test_sem_seed_params_duplicate_name_only_marks_target_symbol(void) {
  SEM_CTX *ctx = sem_create_ctx();
  ASSERT_NOT_NULL(ctx);

  AS_NODE *list = as_new_stmtlist_node();
  list = as_stmtlist_append(list, t_node(N_ASSLOCAL, t_local("z"), t_int(1)));
  list = as_stmtlist_append(list, t_node(N_ASSLOCAL, t_local("a"), t_int(2)));

  char *errdetail = NULL;
  int8_t rc = sem_check_locals(list, &errdetail, ctx);
  ASSERT_EQ_INT(ERR_NOERROR, rc);
  ASSERT_TRUE(errdetail == NULL);

  const char *params[] = {"a", "a"};
  sem_seed_params(ctx, params, 2);

  uint8_t idx = 255;
  ASSERT_TRUE(sem_get_local_index(ctx, "a", &idx));
  ASSERT_TRUE(ctx->locals[idx].param);
  ASSERT_TRUE(sem_get_local_index(ctx, "z", &idx));
  ASSERT_TRUE(!ctx->locals[idx].param);

  as_delete(list);
  sem_delete_ctx(ctx);
}

void test_sem_code_params_duplicate_after_unrelated_locals_no_param_corruption(void) {
  AS_NODE *params_tail = as_new_node(N_ARGLIST, t_local("a"), NULL);
  AS_NODE *params = as_new_node(N_ARGLIST, t_local("a"), params_tail);

  AS_NODE *body = as_new_stmtlist_node();
  body = as_stmtlist_append(body, t_node(N_ASSLOCAL, t_local("m"), t_int(1)));
  body = as_stmtlist_append(body, t_node(N_ASSLOCAL, t_local("n"), t_int(2)));
  body = as_stmtlist_append(body, t_node(N_EXPRSTMT, t_local("m"), NULL));

  AS_NODE *code = t_node(N_CODE, params, body);
  AS_NODE *program = t_stmtlist_with_one(t_node(N_EXPRSTMT, code, NULL));

  SEM_CTX *ctx = sem_create_ctx();
  ASSERT_NOT_NULL(ctx);
  char *errdetail = NULL;
  int8_t rc = sem_check_locals(program, &errdetail, ctx);
  ASSERT_EQ_INT(ERR_NOERROR, rc);
  ASSERT_TRUE(errdetail == NULL);

  as_delete(program);
  sem_delete_ctx(ctx);
}
void test_sem_code_params_are_treated_as_defined_locals(void) {
  SEM_CTX *ctx = sem_create_ctx();
  ASSERT_NOT_NULL(ctx);

  AS_NODE *params = as_new_node(N_ARGLIST, t_local("a"), NULL);
  AS_NODE *body_stmt = t_node(N_EXPRSTMT, t_local("a"), NULL);
  AS_NODE *body = t_stmtlist_with_one(body_stmt);
  AS_NODE *code = t_node(N_CODE, params, body);
  AS_NODE *program = t_stmtlist_with_one(t_node(N_EXPRSTMT, code, NULL));

  char *errdetail = NULL;
  int8_t rc = sem_check_locals(program, &errdetail, ctx);
  ASSERT_EQ_INT(ERR_NOERROR, rc);
  ASSERT_TRUE(errdetail == NULL);

  as_delete(program);
  sem_delete_ctx(ctx);
}
