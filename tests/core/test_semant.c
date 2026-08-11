#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include "error.h"
#include "compiler/parse_input.h"
#include "memory.h"
#include "parser.h"
#include "compiler/semant.h"
#include "test_assert.h"
#include "test_helpers.h"

static void test_sem_break_continue_loop_scope(void);
static AS_NODE *parse_semantic_lists(const char *source);

static AS_NODE *parse_semantic_lists(const char *source) {
  AS_NODE *absyn = NULL;
  char *errdetail = NULL;
  ParseInput input = {source, strlen(source), "list-semantic-test.src"};
  ASSERT_EQ_INT(ERR_NOERROR, parse_source(&input, &absyn, &errdetail));
  ASSERT_TRUE(errdetail == NULL);
  ASSERT_NOT_NULL(absyn);
  return absyn;
}

void test_sem_locals_in_lists_and_itemrefs(void) {
  AS_NODE *root = parse_semantic_lists(
      "@x = 1; #[@missing, &players.[@index]];");
  SEM_CTX *ctx = sem_create_ctx();
  ASSERT_NOT_NULL(ctx);
  char *errdetail = NULL;
  ASSERT_EQ_INT(ERR_COMP_LOCALBEFOREDEF,
                sem_check_locals(root, &errdetail, ctx));
  ASSERT_NOT_NULL(errdetail);
  ASSERT_TRUE(strcmp(errdetail, "semant: @missing") == 0);
  free(errdetail);
  as_delete(root);
  sem_delete_ctx(ctx);

  root = parse_semantic_lists(
      "@x = 1; #[@x, &players.[@index]];");
  ctx = sem_create_ctx();
  ASSERT_NOT_NULL(ctx);
  errdetail = NULL;
  ASSERT_EQ_INT(ERR_COMP_LOCALBEFOREDEF,
                sem_check_locals(root, &errdetail, ctx));
  ASSERT_NOT_NULL(errdetail);
  ASSERT_TRUE(strcmp(errdetail, "semant: @index") == 0);
  free(errdetail);
  as_delete(root);
  sem_delete_ctx(ctx);

  root = parse_semantic_lists(
      "@x = 1; @index = 2; #[@x, &players.[@index]];");
  ctx = sem_create_ctx();
  ASSERT_NOT_NULL(ctx);
  errdetail = NULL;
  ASSERT_EQ_INT(ERR_NOERROR, sem_check_locals(root, &errdetail, ctx));
  ASSERT_TRUE(errdetail == NULL);
  as_delete(root);
  sem_delete_ctx(ctx);
}

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

void test_sem_local_table_growth_oom_preserves_source_span(void) {
  const char source[] = "\n\n@x = 1;";
  AS_NODE *root = parse_semantic_lists(source);
  AS_STMTLIST *stmts = (AS_STMTLIST *)root->lhs;
  ASSERT_EQ_INT(1, stmts->count);
  AS_NODE *assignment = stmts->stmts[0];
  ASSERT_EQ_INT(3, assignment->span.line);
  ASSERT_EQ_INT(1, assignment->span.column);

  for (int fail_index_growth = 0; fail_index_growth <= 1;
       ++fail_index_growth) {
    SEM_CTX *ctx = sem_create_ctx();
    ASSERT_NOT_NULL(ctx);
    if (fail_index_growth) {
      ctx->locals = malloc(sizeof *ctx->locals);
      ASSERT_NOT_NULL(ctx->locals);
      ctx->capacity = 1;
    }

    CompilerDiagnostic diag = {0};
    char *errdetail = NULL;
    alloc_test_fail_after(0);
    int8_t rc = sem_check_locals_diag(root, &errdetail, &diag, ctx);
    alloc_test_fail_after(-1);
    ASSERT_EQ_INT(ERR_COMP_UNKNOWN, rc);
    ASSERT_NOT_NULL(errdetail);
    ASSERT_EQ_INT(3, diag.line);
    ASSERT_EQ_INT(1, diag.column);
    ASSERT_EQ_INT(6, diag.span);
    ASSERT_TRUE(diag.has_loc);
    free(errdetail);
    compiler_diag_reset(&diag);
    sem_delete_ctx(ctx);
  }

  as_delete(root);
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
  ASSERT_EQ_INT(ERR_NOERROR, sem_seed_params(ctx, params, 2));

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

void test_sem_embedded_scope_error_detail_includes_provenance(void) {
  SEM_CTX *ctx = sem_create_ctx();
  ASSERT_NOT_NULL(ctx);

  AS_NODE *embedded_body =
      t_stmtlist_with_one(t_node(N_EXPRSTMT, t_local("missing_embedded"), NULL));
  AS_NODE *embedded = t_node(N_CODE, NULL, embedded_body);
  AS_NODE *program = t_stmtlist_with_one(t_node(N_EXPRSTMT, embedded, NULL));

  char *errdetail = NULL;
  int8_t rc = sem_check_locals(program, &errdetail, ctx);
  ASSERT_EQ_INT(ERR_COMP_LOCALBEFOREDEF, rc);
  ASSERT_NOT_NULL(errdetail);
  ASSERT_TRUE(
      strcmp(errdetail, "semant: embedded code: semant: missing_embedded") == 0);

  free(errdetail);
  as_delete(program);
  sem_delete_ctx(ctx);
  test_sem_break_continue_loop_scope();
}

void test_sem_foreach_semantics(void) {
  const char *valid[] = {
      "foreach @x in #[1] do break; continue; endfor;",
      "foreach @x in #[1] do foreach @y in #[2] do continue; endfor; endfor;",
      "foreach @x in #[1] do endfor; @x = 2;",
  };
  for (size_t i = 0; i < sizeof valid / sizeof valid[0]; ++i) {
    AS_NODE *root = parse_semantic_lists(valid[i]);
    SEM_CTX *ctx = sem_create_ctx();
    char *detail = NULL;
    ASSERT_NOT_NULL(ctx);
    ASSERT_EQ_INT(ERR_NOERROR, sem_check_locals(root, &detail, ctx));
    ASSERT_TRUE(detail == NULL);
    ASSERT_TRUE(ctx->count >= (i == 1 ? 8 : 4));
    free(detail);
    sem_delete_ctx(ctx);
    as_delete(root);
  }
  AS_NODE *root = parse_semantic_lists("foreach @x in @missing do endfor;");
  SEM_CTX *ctx = sem_create_ctx();
  char *detail = NULL;
  ASSERT_EQ_INT(ERR_COMP_LOCALBEFOREDEF, sem_check_locals(root, &detail, ctx));
  ASSERT_TRUE(detail != NULL && strstr(detail, "@missing") != NULL);
  free(detail);
  sem_delete_ctx(ctx);
  as_delete(root);
}

static void test_sem_break_continue_loop_scope(void) {
  AS_NODE *root = NULL;
  SEM_CTX *ctx = NULL;
  char *errdetail = NULL;
  for (int nodetype = N_BREAK; nodetype <= N_CONTINUE; nodetype++) {
    AS_NODE *embedded_body = as_new_stmtlist_node();
    embedded_body = as_stmtlist_append(
        embedded_body, t_node((ENUM_NODE)nodetype, NULL, NULL));
    AS_NODE *embedded = t_node(N_CODE, NULL, embedded_body);
    AS_NODE *outer_body = t_stmtlist_with_one(t_node(N_EXPRSTMT, embedded, NULL));
    root = t_stmtlist_with_one(t_node(N_WHILESTMT, t_int(1), outer_body));
    ctx = sem_create_ctx();
    ASSERT_NOT_NULL(ctx);
    errdetail = NULL;
    ASSERT_EQ_INT(ERR_COMP_SYNTAX, sem_check_locals(root, &errdetail, ctx));
    ASSERT_NOT_NULL(errdetail);
    ASSERT_TRUE(strstr(errdetail, nodetype == N_BREAK
                                      ? "embedded code: semant: BREAK outside loop"
                                      : "embedded code: semant: CONTINUE outside loop") != NULL);
    free(errdetail);
    as_delete(root);
    sem_delete_ctx(ctx);
  }

  AS_NODE *while_body = t_stmtlist_with_one(t_node(N_CONTINUE, NULL, NULL));
  AS_NODE *while_node = t_node(N_WHILESTMT, t_int(1), while_body);
  AS_NODE *do_body = t_stmtlist_with_one(t_node(N_BREAK, NULL, NULL));
  AS_NODE *do_node = t_node(N_DOWHILESTMT, t_int(1), do_body);
  root = as_new_stmtlist_node();
  root = as_stmtlist_append(root, while_node);
  root = as_stmtlist_append(root, do_node);
  ctx = sem_create_ctx();
  ASSERT_NOT_NULL(ctx);
  errdetail = NULL;
  ASSERT_EQ_INT(ERR_NOERROR, sem_check_locals(root, &errdetail, ctx));
  ASSERT_TRUE(errdetail == NULL);
  as_delete(root);
  sem_delete_ctx(ctx);
}


void test_sem_many_locals_deterministic_indices(void) {
  SEM_CTX *ctx = sem_create_ctx();
  ASSERT_NOT_NULL(ctx);

  AS_NODE *list = as_new_stmtlist_node();
  for (int i = 0; i < 220; i++) {
    char name[32];
    snprintf(name, sizeof(name), "local_%03d", i);
    list = as_stmtlist_append(list, t_node(N_ASSLOCAL, t_local(name), t_int(i)));
  }

  char *errdetail = NULL;
  int8_t rc = sem_check_locals(list, &errdetail, ctx);
  ASSERT_EQ_INT(ERR_NOERROR, rc);
  ASSERT_TRUE(errdetail == NULL);
  ASSERT_EQ_INT(220, (int)ctx->count);

  uint8_t idx = 255;
  ASSERT_TRUE(sem_get_local_index(ctx, "local_000", &idx));
  ASSERT_EQ_INT(0, (int)idx);
  ASSERT_TRUE(sem_get_local_index(ctx, "local_099", &idx));
  ASSERT_EQ_INT(99, (int)idx);
  ASSERT_TRUE(sem_get_local_index(ctx, "local_219", &idx));
  ASSERT_EQ_INT(219, (int)idx);

  rc = sem_check_locals(list, &errdetail, ctx);
  ASSERT_EQ_INT(ERR_NOERROR, rc);
  ASSERT_TRUE(errdetail == NULL);
  ASSERT_TRUE(sem_get_local_index(ctx, "local_099", &idx));
  ASSERT_EQ_INT(99, (int)idx);

  as_delete(list);
  sem_delete_ctx(ctx);
}

void test_sem_local_limit_255_is_accepted(void) {
  SEM_CTX *ctx = sem_create_ctx();
  ASSERT_NOT_NULL(ctx);

  AS_NODE *list = as_new_stmtlist_node();
  for (int i = 0; i < 255; i++) {
    char name[32];
    snprintf(name, sizeof(name), "local_%03d", i);
    list = as_stmtlist_append(list, t_node(N_ASSLOCAL, t_local(name), t_int(i)));
  }

  char *errdetail = NULL;
  int8_t rc = sem_check_locals(list, &errdetail, ctx);
  ASSERT_EQ_INT(ERR_NOERROR, rc);
  ASSERT_TRUE(errdetail == NULL);
  ASSERT_EQ_INT(255, (int)ctx->count);

  uint8_t idx = 0;
  ASSERT_TRUE(sem_get_local_index(ctx, "local_254", &idx));
  ASSERT_EQ_INT(254, (int)idx);

  as_delete(list);
  sem_delete_ctx(ctx);
}

void test_sem_local_limit_over_255_fails_deterministically(void) {
  SEM_CTX *ctx = sem_create_ctx();
  ASSERT_NOT_NULL(ctx);

  AS_NODE *list = as_new_stmtlist_node();
  for (int i = 0; i < 256; i++) {
    char name[32];
    snprintf(name, sizeof(name), "local_%03d", i);
    list = as_stmtlist_append(list, t_node(N_ASSLOCAL, t_local(name), t_int(i)));
  }

  char *errdetail = NULL;
  int8_t rc = sem_check_locals(list, &errdetail, ctx);
  ASSERT_EQ_INT(ERR_COMP_TOOMANYLOCALS, rc);
  ASSERT_NOT_NULL(errdetail);
  ASSERT_TRUE(strstr(errdetail, "local_255") != NULL);
  ASSERT_EQ_INT(255, (int)ctx->count);

  free(errdetail);
  as_delete(list);
  sem_delete_ctx(ctx);
}

void test_sem_embedded_local_limit_boundaries(void) {
  AS_NODE *body = as_new_stmtlist_node();
  for (int i = 0; i < 256; i++) {
    char name[32];
    snprintf(name, sizeof(name), "embedded_%03d", i);
    body = as_stmtlist_append(body,
                              t_node(N_ASSLOCAL, t_local(name), t_int(i)));
  }
  AS_NODE *code = t_node(N_CODE, NULL, body);
  AS_NODE *program = t_stmtlist_with_one(t_node(N_EXPRSTMT, code, NULL));
  SEM_CTX *ctx = sem_create_ctx();
  char *detail = NULL;
  ASSERT_NOT_NULL(ctx);
  ASSERT_EQ_INT(ERR_COMP_TOOMANYLOCALS,
                sem_check_locals(program, &detail, ctx));
  ASSERT_NOT_NULL(detail);
  ASSERT_TRUE(strcmp(detail,
                     "semant: embedded code: semant: embedded_255") == 0);
  free(detail);
  as_delete(program);
  sem_delete_ctx(ctx);

  /* Parameters and locals share one scope budget. */
  AS_NODE *params = NULL;
  for (int i = 0; i < 254; i++) {
    char name[32];
    snprintf(name, sizeof(name), "param_%03d", i);
    params = as_new_node(N_ARGLIST, t_local(name), params);
  }
  body = as_new_stmtlist_node();
  body = as_stmtlist_append(body,
                            t_node(N_ASSLOCAL, t_local("boundary_254"),
                                   t_int(1)));
  code = t_node(N_CODE, params, body);
  program = t_stmtlist_with_one(t_node(N_EXPRSTMT, code, NULL));
  ctx = sem_create_ctx();
  detail = NULL;
  ASSERT_NOT_NULL(ctx);
  ASSERT_EQ_INT(ERR_NOERROR, sem_check_locals(program, &detail, ctx));
  ASSERT_TRUE(detail == NULL);
  as_delete(program);
  sem_delete_ctx(ctx);

  params = NULL;
  for (int i = 0; i < 254; i++) {
    char name[32];
    snprintf(name, sizeof(name), "param_%03d", i);
    params = as_new_node(N_ARGLIST, t_local(name), params);
  }
  body = as_new_stmtlist_node();
  body = as_stmtlist_append(body,
                            t_node(N_ASSLOCAL, t_local("boundary_254"),
                                   t_int(1)));
  body = as_stmtlist_append(body,
                            t_node(N_ASSLOCAL, t_local("boundary_255"),
                                   t_int(2)));
  code = t_node(N_CODE, params, body);
  program = t_stmtlist_with_one(t_node(N_EXPRSTMT, code, NULL));
  ctx = sem_create_ctx();
  detail = NULL;
  ASSERT_NOT_NULL(ctx);
  ASSERT_EQ_INT(ERR_COMP_TOOMANYLOCALS,
                sem_check_locals(program, &detail, ctx));
  ASSERT_NOT_NULL(detail);
  ASSERT_TRUE(strcmp(detail,
                     "semant: embedded code: semant: boundary_255") == 0);
  free(detail);
  as_delete(program);
  sem_delete_ctx(ctx);
}
