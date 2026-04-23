// Abstract syntax tree

// Licensed under the MIT License - see LICENSE file for details.
#include <stdlib.h>

#include "memory.h"
#include "absyn.h"

AS_VALUE *as_new_value(ENUM_VALUE valtype, uint64_t ival, char *sval) {
  // Create a new AS value
  //    valtype: type of value
  //    ival:    integer value (V_INT)
  //    sval:    null-terminated string value (V_STR, V_LOCAL, V_ITEM, V_LAYER)
  // Integers, strings, locals and layers are easy.
  // Items are encoded as the name of the item.
  AS_VALUE *newval = GROW_ARRAY(AS_VALUE, NULL, 0, 1);
  newval->valtype = valtype;
  if (valtype == V_INT) {
    newval->value.i = ival;
  } else {
    newval->value.s = sval;  // Remember to free this eventually!
  }
  return newval;
}

AS_NODE *as_new_valnode(ENUM_VALUE valtype, char *sval) {
  // Create a new node of type N_VALUE
  // Like as_new_value(), but puts the value into a node and returns that.
  AS_VALUE *newval;
  if (valtype == V_INT) {
    newval = as_new_value(V_INT, atoi(sval), NULL);
  } else {
    newval = as_new_value(valtype, 0, sval);
  }
  return as_new_node(N_VALUE, newval, NULL);
}

AS_NODE *as_new_node(ENUM_NODE nodetype, void *lhs, void *rhs) {
  // Creates a new AS node
  //    nodetype: type of node
  //    lhs: node payload (lhs if a binary operation node)
  //    rhs: rhs if a binary operation node
  AS_NODE *newnode = GROW_ARRAY(AS_NODE, NULL, 0, 1);
  newnode->nodetype = nodetype;
  // Add nodetypes to the switch below.
  switch (nodetype) {
    case N_VALUE:
    case N_INC:
    case N_DEC: {
      newnode->lhs = lhs;
      newnode->rhs = NULL;
    }
    default: {
      // The default is a binary node (ie both lhs and rhs point to something)
      newnode->lhs = lhs;
      newnode->rhs = rhs;
    }
  }
  return newnode;
}

AS_IF *as_new_if(AS_NODE *condition, AS_NODE *then, AS_IF *elsif) {
  // Creates a new AS IF node
  // condition: An AS_NODE* which represents the condition to test, or null
  //            if this AS_IF just contains the ELSE branch
  // then:      the statements to execute if condition is true
  // elsif:     further tests, or the else branch, or null
  AS_IF *newif = GROW_ARRAY(AS_IF, NULL, 0, 1);
  newif->condition = condition;
  newif->then = then;
  newif->elsif = elsif;
  return newif;
}

