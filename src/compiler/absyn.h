// Abstract syntax tree

// Licensed under the MIT License - see LICENSE file for details.

#pragma once

#include <stdint.h>
#include <stdbool.h>

typedef enum { V_INT, V_FLOAT, V_STR, V_LOCAL, V_LAYER, V_BOOLTRUE, V_BOOLFALSE, V_NIL } ENUM_VALUE;
struct AS_VALUE_s {
  ENUM_VALUE valtype;
  union {
    int64_t i;
    uint64_t f_bits;
    char *s;
  } value;
};
typedef struct AS_VALUE_s AS_VALUE;

typedef enum { N_VALUE, N_ADD, N_SUB, N_MUL, N_DIV, N_INC, N_DEC,
               N_EQUAL, N_NOTEQ, N_OR, N_AND, N_LT, N_LTEQ, N_GT, N_GTEQ,
               N_DEREF, N_ITEM, N_RELITEM, N_NOT, N_LIBCALL, N_ARGLIST,
               N_CODE, N_CALL, N_ASSITEM,
               N_ASSLOCAL, N_EXPRSTMT, N_RETURN, N_STMTLIST, N_STMT,
               N_WHILESTMT, N_IFSTMT, N_DOWHILESTMT
             } ENUM_NODE;
struct AS_NODE_s {
  ENUM_NODE nodetype;
  void *lhs; // May be AS_NODE or AS_VALUE
  void *rhs; // May be AS_NODE or AS_VALUE
};
typedef struct AS_NODE_s AS_NODE;

struct AS_STMTLIST_s {
  AS_NODE **stmts;
  uint32_t count;
  uint32_t capacity;
};
typedef struct AS_STMTLIST_s AS_STMTLIST;

// This is vexing but IF is vexing
struct AS_IF_s {
  AS_NODE *condition;
  AS_NODE *then;
  struct AS_IF_s *elsif;
};
typedef struct AS_IF_s AS_IF;

/*
 * as_new_value() borrows sval on failure and takes ownership on success.
 * as_new_valnode() always consumes sval, including when construction fails.
 */
AS_VALUE *as_new_value(ENUM_VALUE valtype, uint64_t ival, char *sval);
AS_NODE *as_new_valnode(ENUM_VALUE valtype, char *sval);
AS_NODE *as_new_intnode(int64_t value);
AS_STMTLIST *as_new_stmtlist(void);
AS_NODE *as_new_stmtlist_node(void);
/* On failure, the convenience append consumes stmt because it cannot report it. */
AS_NODE *as_stmtlist_append(AS_NODE *stmtlist_node, AS_NODE *stmt);
/* The checked append does not consume stmt when it returns false. */
bool as_stmtlist_append_checked(AS_NODE *stmtlist_node, AS_NODE *stmt);
/*
 * as_new_node() and as_new_if() leave pointer inputs untouched when
 * allocation fails. The successful result owns those inputs.
 */
AS_NODE *as_new_node(ENUM_NODE nodetype, void *lhs, void *rhs);
AS_IF *as_new_if(AS_NODE *condition, AS_NODE *then, AS_IF *elsif);
void as_delete(AS_NODE *root);
void as_delete_if(AS_IF *asif);
void as_walk(AS_NODE *root);
