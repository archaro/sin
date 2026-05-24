// Intermediate representation (IR) module implementation.

// Licensed under the MIT License - see LICENSE file for details.

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#include "compdiag.h"
#include "error.h"
#include "ir.h"
#include "compiler/ir/opcode_schema.h"
#include "memory.h"

static bool ensure_inst_capacity(IR_Function* function, size_t needed) {
  if (function->capacity >= needed) {
    return true;
  }
  size_t oldcap = function->capacity;
  size_t newcap = 0;
  if (!alloc_grow_capacity(oldcap, needed, &newcap)) return false;
  if (!alloc_grow_array((void **)&function->code, oldcap, newcap, sizeof(IR_Inst))) return false;
  function->capacity = newcap;
  return true;
}

static bool ensure_label_capacity(IR_LabelTable* labels, size_t needed) {
  if (labels->capacity >= needed) {
    return true;
  }
  size_t oldcap = labels->capacity;
  size_t newcap = 0;
  if (!alloc_grow_capacity(oldcap, needed, &newcap)) return false;
  if (!alloc_grow_array((void **)&labels->entries, oldcap, newcap, sizeof(IR_Label))) return false;
  labels->capacity = newcap;
  return true;
}

IR_Unit* ir_create_unit(void) {
  return GROW_ARRAY(IR_Unit, NULL, 0, 1);
}

void ir_destroy_unit(IR_Unit* unit) {
  if (unit == NULL) {
    return;
  }

  FREE_ARRAY(IR_Inst, unit->function.code, unit->function.capacity);
  FREE_ARRAY(IR_Label, unit->labels.entries, unit->labels.capacity);
  for (size_t i = 0; i < unit->embedded_code.count; i++) {
    IR_EmbeddedCodePayload* payload = &unit->embedded_code.entries[i];
    FREE_ARRAY(const char*, payload->params, payload->param_count);
    FREE_ARRAY(IR_EmbeddedLocal, payload->locals, payload->local_count);
  }
  FREE_ARRAY(IR_EmbeddedCodePayload, unit->embedded_code.entries, unit->embedded_code.capacity);
  FREE_ARRAY(IR_Unit, unit, 1);
}

size_t ir_emit(IR_Unit* unit, IR_Inst inst) {
  size_t idx = unit->function.count;
  if (!ensure_inst_capacity(&unit->function, idx + 1)) return SIZE_MAX;
  unit->function.code[idx] = inst;
  unit->function.count++;
  return idx;
}

int32_t ir_new_label(IR_Unit* unit) {
  size_t idx = unit->labels.count;
  if (!ensure_label_capacity(&unit->labels, idx + 1)) return -1;

  IR_Label* label = &unit->labels.entries[idx];
  label->id = (int32_t)idx;
  label->position = 0;
  label->bound = false;

  unit->labels.count++;
  return label->id;
}

bool ir_bind_label(IR_Unit* unit, int32_t label_id) {
  if (label_id < 0 || (size_t)label_id >= unit->labels.count) {
    return false;
  }

  IR_Label* label = &unit->labels.entries[label_id];
  label->position = unit->function.count;
  label->bound = true;

  return true;
}

static bool ensure_embedded_capacity(IR_EmbeddedCodeTable* table, size_t needed) {
  if (table->capacity >= needed) return true;
  size_t oldcap = table->capacity;
  size_t newcap = 0;
  if (!alloc_grow_capacity(oldcap, needed, &newcap)) return false;
  if (!alloc_grow_array((void **)&table->entries, oldcap, newcap, sizeof(IR_EmbeddedCodePayload))) return false;
  table->capacity = newcap;
  return true;
}

int32_t ir_add_embedded_code_payload(IR_Unit* unit, IR_EmbeddedCodePayload payload) {
  size_t idx = unit->embedded_code.count;
  if (!ensure_embedded_capacity(&unit->embedded_code, idx + 1)) return -1;
  unit->embedded_code.entries[idx] = payload;
  unit->embedded_code.count++;
  return (int32_t)idx;
}

bool ir_embedded_locals_from_params(AS_NODE* params, IR_EmbeddedCodePayload* payload) {
  AS_NODE* cursor = params;
  size_t count = 0;
  while (cursor) {
    if (cursor->nodetype != N_ARGLIST) return false;
    count++;
    cursor = (AS_NODE*)cursor->rhs;
  }

  payload->param_count = count;
  payload->params = GROW_ARRAY(const char*, NULL, 0, count > 0 ? count : 1);
  payload->locals = GROW_ARRAY(IR_EmbeddedLocal, NULL, 0, count > 0 ? count : 1);
  payload->local_count = count;

  cursor = params;
  for (size_t i = 0; i < count; i++) {
    AS_NODE* param = (AS_NODE*)cursor->lhs;
    AS_VALUE* value;
    if (!param || param->nodetype != N_VALUE) return false;
    value = (AS_VALUE*)param->lhs;
    if (!value || value->valtype != V_LOCAL || !value->value.s) return false;
    payload->params[i] = value->value.s;
    payload->locals[i].name = value->value.s;
    payload->locals[i].index = (uint8_t)i;
    payload->locals[i].param = true;
    cursor = (AS_NODE*)cursor->rhs;
  }

  return true;
}

const char* ir_op_name(IR_Op op) {
  const IR_OpSchema *meta = ir_opcode_schema(op);
  if (!meta || !meta->name) return "<unknown>";
  return meta->name;
}

void ir_dump(FILE* out, IR_Unit* unit) {
  if (out == NULL || unit == NULL) {
    return;
  }

  fprintf(out, "IR unit: %zu instructions, %zu labels\n",
          unit->function.count, unit->labels.count);

  for (size_t i = 0; i < unit->labels.count; i++) {
    IR_Label* label = &unit->labels.entries[i];
    if (label->bound) {
      fprintf(out, "  .L%d = %zu\n", label->id, label->position);
    } else {
      fprintf(out, "  .L%d = <unbound>\n", label->id);
    }
  }

  for (size_t i = 0; i < unit->function.count; i++) {
    const IR_Inst* inst = &unit->function.code[i];
    fprintf(out, "%04zu  %-14s a=%" PRId32 " b=%" PRId32 " imm=%" PRId64 "\n",
            i, ir_op_name(inst->op), inst->a, inst->b, inst->imm);
  }
}

static int8_t ir_validate_error(char **errdetail, int8_t errnum, const char *fmt, ...) {
  va_list args;
  int needed;
  char *msg;

  if (errdetail == NULL) return errnum;

  va_start(args, fmt);
  needed = vsnprintf(NULL, 0, fmt, args);
  va_end(args);
  if (needed < 0) {
    int8_t current = ERR_NOERROR;
    compdiag_set_once(&current, errdetail, errnum, "ir", "formatting error");
    return errnum;
  }

  msg = GROW_ARRAY(char, NULL, 0, (size_t)needed + 1);
  if (!msg) {
    int8_t current = ERR_NOERROR;
    compdiag_set_once(&current, errdetail, errnum, "ir", "out of memory");
    return errnum;
  }

  va_start(args, fmt);
  vsnprintf(msg, (size_t)needed + 1, fmt, args);
  va_end(args);

  {
    int8_t current = ERR_NOERROR;
    compdiag_set_once(&current, errdetail, errnum, "ir", msg);
  }
  FREE_ARRAY(char, msg, (size_t)needed + 1);
  return errnum;
}

int8_t ir_validate(IR_Unit* unit, uint32_t local_count, char **errdetail) {
  if (errdetail != NULL) {
    compdiag_reset_detail(errdetail);
  }

  if (unit == NULL) {
    return ir_validate_error(errdetail, ERR_COMP_SYNTAX, "IR unit is null.");
  }

  for (size_t i = 0; i < unit->labels.count; i++) {
    IR_Label* label = &unit->labels.entries[i];
    if (!label->bound) {
      return ir_validate_error(errdetail, ERR_COMP_SYNTAX,
                               "Unbound label .L%d.", label->id);
    }
  }

  for (size_t i = 0; i < unit->function.count; i++) {
    const IR_Inst* inst = &unit->function.code[i];
    switch (inst->op) {
      case IR_OP_JUMP:
      case IR_OP_JUMP_IF_FALSE:
      case IR_OP_LABEL: {
        if (inst->a < 0 || (size_t)inst->a >= unit->labels.count) {
          return ir_validate_error(errdetail, ERR_COMP_SYNTAX,
                                   "Instruction %zu (%s) references invalid label id %d.",
                                   i, ir_op_name(inst->op), inst->a);
        }
        if (!unit->labels.entries[inst->a].bound) {
          return ir_validate_error(errdetail, ERR_COMP_SYNTAX,
                                   "Instruction %zu (%s) references unbound label .L%d.",
                                   i, ir_op_name(inst->op), inst->a);
        }
        break;
      }
      case IR_OP_LOAD_LOCAL:
      case IR_OP_STORE_LOCAL:
      case IR_OP_INC_LOCAL:
      case IR_OP_DEC_LOCAL: {
        if (inst->a < 0 || (uint32_t)inst->a >= local_count) {
          return ir_validate_error(errdetail, ERR_COMP_LOCALBEFOREDEF,
                                   "Instruction %zu (%s) has out-of-range local index %d (locals=%u).",
                                   i, ir_op_name(inst->op), inst->a, local_count);
        }
        break;
      }
      case IR_OP_CALL: {
        if (inst->a < 0) {
          return ir_validate_error(errdetail, ERR_COMP_TOOMANYARGS,
                                   "Instruction %zu (CALL) has negative arity %d.",
                                   i, ir_op_name(inst->op), inst->a);
        }
        break;
      }
      case IR_OP_LIBCALL_TOKEN:
        if (inst->a < 0) {
          return ir_validate_error(errdetail, ERR_COMP_SYNTAX,
                                   "Instruction %zu (LIBCALL_TOKEN) has negative token %d.",
                                   i, inst->a);
        }
        break;
      default:
        break;
    }
  }
  return ERR_NOERROR;
}
const IR_OpSchema g_ir_opcode_schema[] = {
#define OP(NAME, SYMBOL, REQUIRES_RUNTIME_HANDLER, OPERAND, SIZE, VALIDATOR) \
    [IR_OP_##NAME] = {IR_OP_##NAME, #NAME, (uint8_t)(SYMBOL), REQUIRES_RUNTIME_HANDLER, OPERAND, SIZE, VALIDATOR},
#include "compiler/ir/opcode_schema.def"
#undef OP
};
const size_t g_ir_opcode_schema_count = sizeof(g_ir_opcode_schema) / sizeof(g_ir_opcode_schema[0]);

const IR_OpSchema* ir_opcode_schema(IR_Op op) {
  if (op < 0 || op >= (IR_Op)g_ir_opcode_schema_count) return NULL;
  return &g_ir_opcode_schema[op];
}

void ir_opcode_schema_for_each_runtime_opcode(IR_RuntimeOpcodeVisitor visitor, void *ctx) {
  if (!visitor) return;
  for (size_t i = 0; i < g_ir_opcode_schema_count; i++) {
    const IR_OpSchema *meta = &g_ir_opcode_schema[i];
    if (!meta->requires_runtime_handler || meta->encoded_symbol == 0) continue;
    if (!visitor(meta->encoded_symbol, meta->op, meta, ctx)) return;
  }
}

int8_t ir_opcode_schema_validate_unique(char **errdetail) {
  bool seen[256] = {0};
  IR_Op seen_by[256] = {0};
  if (errdetail) compdiag_reset_detail(errdetail);
  for (size_t i = 0; i < g_ir_opcode_schema_count; i++) {
    const IR_OpSchema *meta = &g_ir_opcode_schema[i];
    if (meta->op != (IR_Op)i) continue;
    if (meta->encoded_symbol == 0) continue;
    if (seen[meta->encoded_symbol]) {
      int8_t errnum = ERR_NOERROR;
      compdiag_setf_once(&errnum, errdetail, ERR_COMP_SYNTAX, "opcode_schema",
                         "ambiguous opcode encoding '%c' for %s and %s",
                         (char)meta->encoded_symbol, ir_op_name(seen_by[meta->encoded_symbol]), meta->name);
      return errnum;
    }
    seen[meta->encoded_symbol] = true;
    seen_by[meta->encoded_symbol] = meta->op;
  }
  return ERR_NOERROR;
}
