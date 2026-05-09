#pragma once

#include <stddef.h>
#include <stdint.h>

#include "absyn.h"
#include "emitbc.h"
#include "ir.h"

AS_NODE *t_int(int64_t value);
AS_NODE *t_local(const char *name);
AS_NODE *t_node(ENUM_NODE nodetype, void *lhs, void *rhs);
AS_NODE *t_stmtlist_with_one(AS_NODE *stmt);

IR_Unit *t_new_unit(void);
void t_emit(IR_Unit *unit, IR_Inst inst);
void t_bind(IR_Unit *unit, int32_t label_id);

int8_t t_emit_bytecode(IR_Unit *unit, uint8_t local_count, uint8_t param_count,
                       OUTPUT_t *out, char **errdetail);

uint8_t hex_nibble(char c);
uint8_t *load_hex_fixture(const char *path, size_t *out_len);
