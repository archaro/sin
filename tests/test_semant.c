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
  ASSERT_TRUE(strcmp(errdetail, "missing") == 0);

  char *first_errdetail = errdetail;

  rc = sem_check_locals(bad_prog, &errdetail, ctx);
  ASSERT_EQ_INT(ERR_COMP_LOCALBEFOREDEF, rc);
  ASSERT_NOT_NULL(errdetail);
  ASSERT_TRUE(strcmp(errdetail, "missing") == 0);
  ASSERT_TRUE(errdetail != first_errdetail);
  free(first_errdetail);
  free(errdetail);

  as_delete(ok_prog);
  as_delete(bad_prog);
  sem_delete_ctx(ctx);
}
