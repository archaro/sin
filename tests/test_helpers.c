#include "test_helpers.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

AS_NODE *t_int(int64_t value) {
  char buf[32];
  snprintf(buf, sizeof(buf), "%lld", (long long)value);
  return as_new_valnode(V_INT, strdup(buf));
}

AS_NODE *t_local(const char *name) {
  return as_new_valnode(V_LOCAL, strdup(name));
}

AS_NODE *t_node(ENUM_NODE nodetype, void *lhs, void *rhs) {
  return as_new_node(nodetype, lhs, rhs);
}

AS_NODE *t_stmtlist_with_one(AS_NODE *stmt) {
  AS_NODE *list = as_new_stmtlist_node();
  return as_stmtlist_append(list, stmt);
}

IR_Unit *t_new_unit(void) {
  return ir_create_unit();
}

void t_emit(IR_Unit *unit, IR_Inst inst) {
  (void)ir_emit(unit, inst);
}

void t_bind(IR_Unit *unit, int32_t label_id) {
  (void)ir_bind_label(unit, label_id);
}

int8_t t_emit_bytecode(IR_Unit *unit, uint8_t local_count, uint8_t param_count,
                       OUTPUT_t *out, char **errdetail) {
  return emit_bytecode(unit, local_count, param_count, out, errdetail);
}
