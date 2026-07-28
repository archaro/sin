// Abstract syntax tree

// Licensed under the MIT License - see LICENSE file for details.
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

#include "log.h"
#include "memory.h"
#include "compiler/absyn.h"
#include "floatconv.h"

static void as_delete_value_payload(AS_VALUE *value) {
  if (!value) return;
  if (value->valtype != V_INT && value->valtype != V_FLOAT && value->valtype != V_BOOLTRUE && value->valtype != V_BOOLFALSE && value->valtype != V_NIL) {
    free(value->value.s);
  }
}

AS_VALUE *as_new_value(ENUM_VALUE valtype, uint64_t ival, char *sval) {
  // Create a new AS value
  //    valtype: type of value
  //    ival:    integer value (V_INT)
  //    sval:    null-terminated string value (V_STR, V_LOCAL, V_ITEM, V_LAYER)
  // Integers, strings, locals and layers are easy.
  // Items are encoded as the name of the item.
  AS_VALUE *newval = alloc_malloc(sizeof *newval);
  if (!newval) return NULL;
  newval->valtype = valtype;
  if (valtype == V_INT || valtype == V_BOOLTRUE || valtype == V_BOOLFALSE || valtype == V_NIL) {
    newval->value.i = (int64_t)ival;
  } else if (valtype == V_FLOAT) {
    newval->value.f_bits = ival;
  } else {
    newval->value.s = sval;  // Remember to free this eventually!
  }
  return newval;
}

AS_NODE *as_new_intnode(int64_t value) {
  AS_VALUE *newval = as_new_value(V_INT, (uint64_t)value, NULL);
  if (!newval) return NULL;
  AS_NODE *node = as_new_node(N_VALUE, newval, NULL);
  if (!node) {
    as_delete_value_payload(newval);
    free(newval);
  }
  return node;
}

AS_NODE *as_new_valnode(ENUM_VALUE valtype, char *sval) {
  // Create a new node of type N_VALUE
  // Like as_new_value(), but puts the value into a node and returns that.
  AS_VALUE *newval;
  if (valtype != V_BOOLTRUE && valtype != V_BOOLFALSE && valtype != V_NIL && !sval) return NULL;
  if (valtype == V_INT) {
    newval = as_new_value(V_INT, (uint64_t)atoll(sval), NULL);
    free(sval);
  } else if (valtype == V_FLOAT) {
    uint64_t bits = 0;
    char *errdetail = NULL;
    if (!sin_parse_binary64_bits(sval, &bits, &errdetail)) {
      logerr("Failed to parse float literal '%s'%s%s\n", sval, errdetail ? ": " : "", errdetail ? errdetail : "");
      free(errdetail);
      free(sval);
      return NULL;
    }
    free(errdetail);
    newval = as_new_value(V_FLOAT, bits, NULL);
    free(sval);
  } else if (valtype == V_BOOLTRUE) {
    free(sval);
    newval = as_new_value(V_BOOLTRUE, 1, NULL);
  } else if (valtype == V_BOOLFALSE) {
    free(sval);
    newval = as_new_value(V_BOOLFALSE, 0, NULL);
  } else if (valtype == V_NIL) {
    free(sval);
    newval = as_new_value(V_NIL, 0, NULL);
  } else {
    newval = as_new_value(valtype, 0, sval);
    if (!newval) {
      free(sval);
      return NULL;
    }
  }
  if (!newval) return NULL;
  AS_NODE *node = as_new_node(N_VALUE, newval, NULL);
  if (!node) {
    as_delete_value_payload(newval);
    free(newval);
  }
  return node;
}

AS_NODE *as_new_node(ENUM_NODE nodetype, void *lhs, void *rhs) {
  // Creates a new AS node
  //    nodetype: type of node
  //    lhs: node payload (lhs if a binary operation node)
  //    rhs: rhs if a binary operation node
  AS_NODE *newnode = alloc_malloc(sizeof *newnode);
  if (!newnode) return NULL;
  newnode->nodetype = nodetype;
  // Add nodetypes to the switch below.
  switch (nodetype) {
    case N_VALUE:
    case N_INC:
    case N_DEC:
    case N_STMTLIST: {
      newnode->lhs = lhs;
      newnode->rhs = NULL;
      break;
    }
    default: {
      // The default is a binary node (ie both lhs and rhs point to something)
      newnode->lhs = lhs;
      newnode->rhs = rhs;
    }
  }
  return newnode;
}

AS_STMTLIST *as_new_stmtlist(void) {
  AS_STMTLIST *newlist = alloc_malloc(sizeof *newlist);
  if (!newlist) return NULL;
  newlist->stmts = NULL;
  newlist->count = 0;
  newlist->capacity = 0;
  return newlist;
}

AS_NODE *as_new_stmtlist_node(void) {
  AS_STMTLIST *stmtlist = as_new_stmtlist();
  if (!stmtlist) return NULL;
  AS_NODE *node = as_new_node(N_STMTLIST, stmtlist, NULL);
  if (!node) free(stmtlist);
  return node;
}

bool as_stmtlist_append_checked(AS_NODE *stmtlist_node, AS_NODE *stmt) {
  if (!stmtlist_node || stmtlist_node->nodetype != N_STMTLIST || !stmt) {
    return false;
  }
  AS_STMTLIST *stmtlist = (AS_STMTLIST *)stmtlist_node->lhs;
  if (!stmtlist || stmtlist->count == UINT32_MAX) return false;
  if (stmtlist->count == stmtlist->capacity) {
    size_t oldcap = stmtlist->capacity;
    size_t newcap = 0;
    if (!alloc_grow_capacity(oldcap, oldcap + 1, &newcap)) return false;
    if (newcap > UINT32_MAX ||
        !alloc_grow_array((void **)&stmtlist->stmts, newcap, sizeof(AS_NODE*))) {
      return false;
    }
    stmtlist->capacity = (uint32_t)newcap;
  }
  stmtlist->stmts[stmtlist->count++] = stmt;
  return true;
}

AS_NODE *as_stmtlist_append(AS_NODE *stmtlist_node, AS_NODE *stmt) {
  if (!as_stmtlist_append_checked(stmtlist_node, stmt)) as_delete(stmt);
  return stmtlist_node;
}

AS_IF *as_new_if(AS_NODE *condition, AS_NODE *then, AS_IF *elsif) {
  // Creates a new AS IF node
  // condition: An AS_NODE* which represents the condition to test, or null
  //            if this AS_IF just contains the ELSE branch
  // then:      the statements to execute if condition is true
  // elsif:     further tests, or the else branch, or null
  AS_IF *newif = alloc_malloc(sizeof *newif);
  if (!newif) return NULL;
  newif->condition = condition;
  newif->then = then;
  newif->elsif = elsif;
  return newif;
}

void as_delete_if(AS_IF *asif) {
  // Internal helper for deleting an AS_IF node
  // asif: the node to delete
  // WARNING: the pointer passed to this function is freed!
  if (!asif) return;
  if (asif->condition) {
    as_delete(asif->condition);
  }
  if (asif->then) {
    as_delete(asif->then);
  }
  if (asif->elsif) {
    as_delete_if(asif->elsif);
  }
  free(asif);
}

void as_delete(AS_NODE *root) {
  // Deletes the abstract syntax tree.  Recursive.
  // root: The root of the tree
  // WARNING: the pointer passed to this function is freed!

  if (!root) return; // Nothing to do!

  switch (root->nodetype) {
    case N_VALUE: {
      AS_VALUE *val = (AS_VALUE*)root->lhs;
      as_delete_value_payload(val);
      free(root->lhs);
      // rhs is always null for this nodetype
      break;
    }
    case N_IFSTMT: {
      as_delete_if((AS_IF*)root->lhs);
      // rhs is always null for this nodetype
      break;
    }
    case N_STMTLIST: {
      AS_STMTLIST *stmtlist = (AS_STMTLIST*)root->lhs;
      for (uint32_t i = 0; i < stmtlist->count; i++) {
        as_delete(stmtlist->stmts[i]);
      }
      free(stmtlist->stmts);
      free(stmtlist);
      break;
    }
    case N_ADD:
    case N_SUB:
    case N_MUL:
    case N_DIV:
    case N_INC:
    case N_DEC:
    case N_EQUAL:
    case N_NOTEQ:
    case N_OR:
    case N_AND:
    case N_LT:
    case N_LTEQ:
    case N_GT:
    case N_GTEQ:
    case N_DEREF:
    case N_ITEM:
    case N_RELITEM:
    case N_NOT:
    case N_LIBCALL:
    case N_ARGLIST:
    case N_CODE:
    case N_CALL:
    case N_ASSITEM:
    case N_ASSLOCAL:
    case N_EXPRSTMT:
    case N_RETURN:
    case N_STMT:
    case N_WHILESTMT:
    case N_DOWHILESTMT:
    {
      if (root->lhs) {
        as_delete((AS_NODE*)root->lhs);
      }
      if (root->rhs) {
        as_delete((AS_NODE*)root->rhs);
      }
      break;
    }
    default: {
      logerr("Calling as_delete() with invalid node type %d\n", root->nodetype);
    }
  }
  free(root);
}

// Keep this in sync with ENUM_VALUE!
const char *valname[] = { "V_INT", "V_FLOAT", "V_STR", "V_LOCAL", "V_LAYER", "V_BOOLTRUE", "V_BOOLFALSE", "V_NIL" };
// And keep this in sync with ENUM_NODE!
const char *nodename[] = { "N_VALUE", "N_ADD", "N_SUB", "N_MUL", "N_DIV", "N_INC", "N_DEC", "N_EQUAL", "N_NOTEQ", "N_OR", "N_AND", "N_LT", "N_LTEQ", "N_GT", "N_GTEQ", "N_DEREF", "N_ITEM", "N_RELITEM", "N_NOT", "N_LIBCALL", "N_ARGLIST", "N_CODE", "N_CALL", "N_ASSITEM", "N_ASSLOCAL", "N_EXPRSTMT", "N_RETURN", "N_STMTLIST", "N_STMT", "N_WHILESTMT", "N_IFSTMT", "N_DOWHILESTMT" };

void as_pretty_print(int tree_depth) {
  // Indent to make everything look all neat and professional
  for (int s = tree_depth * 2; s > 0; s--)
    logverbose(" ");
}

void as_reconstruct_value(AS_NODE *node) {
  // Given a N_VALUE node, output the type and the contents
  AS_VALUE *val = (AS_VALUE*)node->lhs;
  logverbose("%s: ", valname[val->valtype]);
  if (val->valtype == V_INT || val->valtype == V_BOOLTRUE || val->valtype == V_BOOLFALSE) {
    logverbose("%" PRId64, val->value.i);
  } else if (val->valtype == V_FLOAT) {
    double f = 0.0;
    memcpy(&f, &val->value.f_bits, sizeof(f));
    char fbuffer[64];
    if (sin_format_binary64_buf(f, fbuffer, sizeof(fbuffer))) {
      logverbose("%s (bits=0x%016llx)", fbuffer, (unsigned long long)val->value.f_bits);
    } else {
      logverbose("0x%016llx", (unsigned long long)val->value.f_bits);
    }
  } else if (val->valtype == V_NIL) {
    logverbose("nil");
  } else {
    logverbose("%s", val->value.s);
  }
}

void as_reconstruct_item(AS_NODE *root) {
  // Given an N_ITEM node, follow it to its end
  if (root && root->nodetype == N_RELITEM) {
    logverbose(".");
    root = (AS_NODE*)root->lhs;
  }

  AS_NODE *node = root->lhs;
  // An item node can only have children of type N_VALUE or N_DEREF
  if (node->nodetype == N_VALUE) {
    AS_VALUE *val = (AS_VALUE*)node->lhs;
    if (val->valtype == V_INT || val->valtype == V_BOOLTRUE || val->valtype == V_BOOLFALSE) {
      logverbose("%" PRId64, val->value.i);
    } else if (val->valtype == V_FLOAT) {
      double f = 0.0;
      memcpy(&f, &val->value.f_bits, sizeof(f));
      char fbuffer[64];
      if (sin_format_binary64_buf(f, fbuffer, sizeof(fbuffer))) {
        logverbose("%s (bits=0x%016llx)", fbuffer, (unsigned long long)val->value.f_bits);
      } else {
        logverbose("0x%016llx", (unsigned long long)val->value.f_bits);
      }
    } else {
      logverbose("%s", val->value.s);
    }
  } else {
    // Must be N_DEREF
    logverbose("[");
    AS_NODE *inner = (AS_NODE*)node->lhs;
    // If it's a deref, the lhs node must be either an N_ITEM or a N_VALUE
    // If the latter, it must be a value of type V_LOCAL
    if (inner->nodetype == N_ITEM) {
      as_reconstruct_item(inner);
    } else {
      AS_VALUE *val = inner->lhs;
      logverbose("%s", val->value.s);
    }
    logverbose("]");
  }
  if (root->rhs) {
    logverbose(".");
    as_reconstruct_item(root->rhs);
  }
}

static void as_walk_internal(AS_NODE *root, int tree_depth);

void as_parse_if(AS_IF *ifstmt, int tree_depth) {
  as_pretty_print(tree_depth);
  if (ifstmt->condition) {
    logverbose("Condition:\n");
    as_walk_internal(ifstmt->condition, tree_depth + 1);
    as_pretty_print(tree_depth);
    logverbose("Then:\n");
  } else {
    logverbose("Else:\n");
  }
  as_walk_internal(ifstmt->then, tree_depth + 1);
  if (ifstmt->elsif) {
    as_pretty_print(tree_depth);
    logverbose("Tail:\n");
    as_parse_if(ifstmt->elsif, tree_depth + 1);
  }
}

static void as_walk_internal(AS_NODE *root, int tree_depth) {
  // Debug function to walk the abstract syntax tree.
  // Designed to be called recursively.

  // Don't try to walk an empty tree.
  if (!root) return;

  as_pretty_print(tree_depth);

  switch (root->nodetype) {
    case N_ADD:
    case N_SUB:
    case N_MUL:
    case N_DIV:
    case N_INC:
    case N_DEC:
    case N_EQUAL:
    case N_NOTEQ:
    case N_OR:
    case N_AND:
    case N_LT:
    case N_LTEQ:
    case N_GT:
    case N_GTEQ: {
      logverbose("Node type: %s\n", nodename[root->nodetype]);
      as_pretty_print(tree_depth);
      logverbose("LHS:\n");
      as_walk_internal((AS_NODE*)root->lhs, tree_depth + 1);
      as_pretty_print(tree_depth);
      logverbose("RHS:\n");
      as_walk_internal((AS_NODE*)root->rhs, tree_depth + 1);
      return;
    }
    case N_CODE: {
      if (root->lhs) {
        logverbose("Parameters:\n");
        as_pretty_print(tree_depth);
        as_walk_internal((AS_NODE*)root->lhs, tree_depth + 1);
      }
      logverbose("Code block:\n");
      as_pretty_print(tree_depth);
      as_reconstruct_value((AS_NODE*)root->rhs);
      logverbose("\n");
      return;
    }
    case N_VALUE: {
      logverbose("Value type ");
      as_reconstruct_value(root);
      logverbose("\n");
      return;
    }
    case N_WHILESTMT: {
      logverbose("Node type: %s\n", nodename[root->nodetype]);
      as_pretty_print(tree_depth + 1);
      logverbose("Condition:\n");
      as_walk_internal((AS_NODE*)root->lhs, tree_depth + 2);
      as_pretty_print(tree_depth + 1);
      logverbose("Execute while true:\n");
      as_walk_internal((AS_NODE*)root->rhs, tree_depth + 2);
      return;
    }
    case N_DOWHILESTMT: {
      logverbose("Node type: %s\n", nodename[root->nodetype]);
      as_pretty_print(tree_depth + 1);
      logverbose("Execute body:\n");
      as_walk_internal((AS_NODE*)root->rhs, tree_depth + 2);
      as_pretty_print(tree_depth + 1);
      logverbose("Condition:\n");
      as_walk_internal((AS_NODE*)root->lhs, tree_depth + 2);
      return;
    }
    case N_NOT:
    case N_RETURN:
    case N_CALL:
    case N_LIBCALL:
    case N_EXPRSTMT: {
      logverbose("Node type: %s\n", nodename[root->nodetype]);
      break;
    }
    case N_STMTLIST: {
      AS_STMTLIST *stmtlist = (AS_STMTLIST*)root->lhs;
      logverbose("Node type: %s (%u statements)\n",
             nodename[root->nodetype], stmtlist->count);
      for (uint32_t i = 0; i < stmtlist->count; i++) {
        as_pretty_print(tree_depth);
        logverbose("Statement %u:\n", i + 1);
        as_walk_internal(stmtlist->stmts[i], tree_depth + 1);
      }
      return;
    }
    case N_STMT: {
      logverbose("Node type: %s\n", nodename[root->nodetype]);
      break;
    }
    case N_ITEM:
    case N_RELITEM: {
      logverbose("Item node: ");
      as_reconstruct_item(root);
      logverbose("\n");
      return;
    }
    case N_ARGLIST: {
      logverbose("Node type: %s\n", nodename[root->nodetype]);
      as_pretty_print(tree_depth);
      logverbose("Parameter: \n");
      as_walk_internal((AS_NODE*)root->lhs, tree_depth + 1);
      if (root->rhs) {
        as_walk_internal((AS_NODE*)root->rhs, tree_depth + 1);
      }
      return;
    }
    case N_ASSITEM: {
      logverbose("Node type: %s\n", nodename[root->nodetype]);
      as_pretty_print(tree_depth);
      logverbose("Item: ");
      as_reconstruct_item((AS_NODE*)root->lhs);
      logverbose("\n");
      as_pretty_print(tree_depth);
      logverbose("Assigned:\n");
      as_walk_internal((AS_NODE*)root->rhs, tree_depth + 1);
      return;
    }
    case N_ASSLOCAL: {
      logverbose("Node type: %s\n", nodename[root->nodetype]);
      as_pretty_print(tree_depth);
      logverbose("Local: ");
      as_reconstruct_value((AS_NODE*)root->lhs);
      logverbose("\n");
      as_pretty_print(tree_depth);
      logverbose("Assigned:\n");
      as_walk_internal((AS_NODE*)root->rhs, tree_depth + 1);
      return;
    }
    case N_IFSTMT: {
      logverbose("Node type: %s\n", nodename[root->nodetype]);
      as_parse_if((AS_IF *)root->lhs, tree_depth + 1);
      return;
    }
    default: {
      logerr("Calling as_walk() with invalid node type %d\n", root->nodetype);
    }
  }
  if (root->lhs) {
    as_walk_internal((AS_NODE*)root->lhs, tree_depth + 1);
  }
  if (root->rhs) {
    as_walk_internal((AS_NODE*)root->rhs, tree_depth + 1);
  }
}

void as_walk(AS_NODE *root) {
  as_walk_internal(root, 0);
}
