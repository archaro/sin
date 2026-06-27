#include <string.h>

#include "absyn.h"
#include "test_assert.h"
#include "test_helpers.h"

void test_absyn_nested_binary_expressions(void) {
  AS_NODE *left = t_node(N_ADD, t_int(1), t_int(2));
  AS_NODE *right = t_node(N_SUB, t_int(8), t_int(3));
  AS_NODE *root = t_node(N_MUL, left, right);

  ASSERT_EQ_INT(N_MUL, root->nodetype);
  ASSERT_EQ_INT(N_ADD, ((AS_NODE *)root->lhs)->nodetype);
  ASSERT_EQ_INT(N_SUB, ((AS_NODE *)root->rhs)->nodetype);
  ASSERT_EQ_INT(N_VALUE, ((AS_NODE *)((AS_NODE *)root->lhs)->lhs)->nodetype);
  ASSERT_EQ_INT(N_VALUE, ((AS_NODE *)((AS_NODE *)root->rhs)->rhs)->nodetype);

  as_delete(root);
}

void test_absyn_stmtlist_multiple_statements(void) {
  AS_NODE *stmtlist = as_new_stmtlist_node();
  AS_NODE *stmt1 = t_node(N_EXPRSTMT, t_int(10), NULL);
  AS_NODE *stmt2 = t_node(N_EXPRSTMT, t_node(N_ADD, t_int(3), t_int(4)), NULL);
  AS_NODE *stmt3 = t_node(N_RETURN, t_local("answer"), NULL);

  as_stmtlist_append(stmtlist, stmt1);
  as_stmtlist_append(stmtlist, stmt2);
  as_stmtlist_append(stmtlist, stmt3);

  AS_STMTLIST *list = (AS_STMTLIST *)stmtlist->lhs;
  ASSERT_EQ_INT(N_STMTLIST, stmtlist->nodetype);
  ASSERT_EQ_INT(3, list->count);
  ASSERT_TRUE(list->stmts[0] == stmt1);
  ASSERT_TRUE(list->stmts[1] == stmt2);
  ASSERT_TRUE(list->stmts[2] == stmt3);

  as_delete(stmtlist);
}

void test_absyn_if_elsif_else_chain(void) {
  AS_IF *else_branch = as_new_if(NULL, t_stmtlist_with_one(t_node(N_RETURN, t_int(0), NULL)), NULL);
  AS_IF *elsif_branch = as_new_if(t_node(N_GT, t_int(10), t_int(5)),
                                  t_stmtlist_with_one(t_node(N_RETURN, t_int(1), NULL)),
                                  else_branch);
  AS_IF *if_chain = as_new_if(t_node(N_EQUAL, t_int(2), t_int(2)),
                              t_stmtlist_with_one(t_node(N_RETURN, t_int(2), NULL)),
                              elsif_branch);

  AS_NODE *if_stmt = as_new_node(N_IFSTMT, if_chain, NULL);

  ASSERT_EQ_INT(N_IFSTMT, if_stmt->nodetype);
  AS_IF *root_if = (AS_IF *)if_stmt->lhs;
  ASSERT_NOT_NULL(root_if->condition);
  ASSERT_EQ_INT(N_EQUAL, root_if->condition->nodetype);
  ASSERT_NOT_NULL(root_if->elsif);
  ASSERT_EQ_INT(N_GT, root_if->elsif->condition->nodetype);
  ASSERT_NOT_NULL(root_if->elsif->elsif);
  ASSERT_TRUE(root_if->elsif->elsif->condition == NULL);

  as_delete(if_stmt);
}

void test_absyn_item_deref_chains(void) {
  AS_NODE *chain =
      t_node(N_ITEM,
             t_node(N_DEREF,
                    t_node(N_ITEM,
                           t_node(N_DEREF,
                                  t_node(N_VALUE, as_new_value(V_LOCAL, 0, strdup("player")), NULL),
                                  NULL),
                           t_node(N_ITEM,
                                  t_node(N_VALUE, as_new_value(V_LAYER, 0, strdup("stats")), NULL),
                                  NULL)),
                    NULL),
             t_node(N_ITEM,
                    t_node(N_VALUE, as_new_value(V_LOCAL, 0, strdup("hp")), NULL),
                    NULL));

  ASSERT_EQ_INT(N_ITEM, chain->nodetype);
  AS_NODE *first = (AS_NODE *)chain->lhs;
  ASSERT_EQ_INT(N_DEREF, first->nodetype);
  AS_NODE *inner_item = (AS_NODE *)first->lhs;
  ASSERT_EQ_INT(N_ITEM, inner_item->nodetype);
  AS_NODE *local_value = (AS_NODE *)((AS_NODE *)inner_item->lhs)->lhs;
  ASSERT_EQ_INT(N_VALUE, local_value->nodetype);
  ASSERT_EQ_INT(V_LOCAL, ((AS_VALUE *)local_value->lhs)->valtype);
  AS_NODE *layer_item = (AS_NODE *)inner_item->rhs;
  ASSERT_EQ_INT(N_ITEM, layer_item->nodetype);
  ASSERT_EQ_INT(V_LAYER, ((AS_VALUE *)((AS_NODE *)layer_item->lhs)->lhs)->valtype);
  ASSERT_EQ_INT(V_LOCAL, ((AS_VALUE *)((AS_NODE *)((AS_NODE *)chain->rhs)->lhs)->lhs)->valtype);

  as_delete(chain);
}



void test_absyn_float_value_preserves_bits(void) {
  const uint64_t bits = UINT64_C(0x7ff8000000000042);
  AS_NODE *node = t_node(N_VALUE, as_new_value(V_FLOAT, bits, NULL), NULL);
  AS_VALUE *value = (AS_VALUE *)node->lhs;

  ASSERT_EQ_INT(V_FLOAT, value->valtype);
  ASSERT_EQ_INT((int64_t)bits, (int64_t)value->value.f_bits);

  as_delete(node);
}
