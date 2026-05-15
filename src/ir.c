// Intermediate representation (IR) module implementation.

// Licensed under the MIT License - see LICENSE file for details.

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#include "error.h"
#include "ir.h"
#include "memory.h"

static void ensure_inst_capacity(IR_Function* function, size_t needed) {
  if (function->capacity >= needed) {
    return;
  }

  size_t oldcap = function->capacity;
  size_t newcap = GROW_CAPACITY(oldcap);
  while (newcap < needed) {
    newcap = GROW_CAPACITY(newcap);
  }

  function->code = GROW_ARRAY(IR_Inst, function->code, oldcap, newcap);
  function->capacity = newcap;
}

static void ensure_label_capacity(IR_LabelTable* labels, size_t needed) {
  if (labels->capacity >= needed) {
    return;
  }

  size_t oldcap = labels->capacity;
  size_t newcap = GROW_CAPACITY(oldcap);
  while (newcap < needed) {
    newcap = GROW_CAPACITY(newcap);
  }

  labels->entries = GROW_ARRAY(IR_Label, labels->entries, oldcap, newcap);
  labels->capacity = newcap;
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
  ensure_inst_capacity(&unit->function, idx + 1);
  unit->function.code[idx] = inst;
  unit->function.count++;
  return idx;
}

int32_t ir_new_label(IR_Unit* unit) {
  size_t idx = unit->labels.count;
  ensure_label_capacity(&unit->labels, idx + 1);

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

static void ensure_embedded_capacity(IR_EmbeddedCodeTable* table, size_t needed) {
  if (table->capacity >= needed) return;
  size_t oldcap = table->capacity;
  size_t newcap = GROW_CAPACITY(oldcap);
  while (newcap < needed) newcap = GROW_CAPACITY(newcap);
  table->entries = GROW_ARRAY(IR_EmbeddedCodePayload, table->entries, oldcap, newcap);
  table->capacity = newcap;
}

int32_t ir_add_embedded_code_payload(IR_Unit* unit, IR_EmbeddedCodePayload payload) {
  size_t idx = unit->embedded_code.count;
  ensure_embedded_capacity(&unit->embedded_code, idx + 1);
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
  switch (op) {
    case IR_OP_HALT: return "HALT";
    case IR_OP_PUSH_INT: return "PUSH_INT";
    case IR_OP_PUSH_STRING: return "PUSH_STRING";
    case IR_OP_ADD: return "ADD";
    case IR_OP_SUB: return "SUB";
    case IR_OP_MUL: return "MUL";
    case IR_OP_DIV: return "DIV";
    case IR_OP_NEG: return "NEG";
    case IR_OP_EQ: return "EQ";
    case IR_OP_NEQ: return "NEQ";
    case IR_OP_LT: return "LT";
    case IR_OP_GT: return "GT";
    case IR_OP_LE: return "LE";
    case IR_OP_GE: return "GE";
    case IR_OP_NOT: return "NOT";
    case IR_OP_AND: return "AND";
    case IR_OP_OR: return "OR";
    case IR_OP_LOAD_LOCAL: return "LOAD_LOCAL";
    case IR_OP_STORE_LOCAL: return "STORE_LOCAL";
    case IR_OP_INC_LOCAL: return "INC_LOCAL";
    case IR_OP_DEC_LOCAL: return "DEC_LOCAL";
    case IR_OP_JUMP: return "JUMP";
    case IR_OP_JUMP_IF_FALSE: return "JUMP_IF_FALSE";
    case IR_OP_LABEL: return "LABEL";
    case IR_OP_ITEM_BEGIN: return "ITEM_BEGIN";
    case IR_OP_ITEM_PUSH_LAYER: return "ITEM_PUSH_LAYER";
    case IR_OP_ITEM_PUSH_DEREF: return "ITEM_PUSH_DEREF";
    case IR_OP_ITEM_END: return "ITEM_END";
    case IR_OP_ITEM_DEREF: return "ITEM_DEREF";
    case IR_OP_ITEM_SAVE: return "ITEM_SAVE";
    case IR_OP_CALL: return "CALL";
    case IR_OP_LIBCALL: return "LIBCALL";
    case IR_OP_EXISTS: return "EXISTS";
    case IR_OP_DELETE: return "DELETE";
    case IR_OP_NTHNAME: return "NTHNAME";
    case IR_OP_ROOTNAME: return "ROOTNAME";
    case IR_OP_ITEM_SAVE_CODE: return "ITEM_SAVE_CODE";
    default: return "<unknown>";
  }
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
  if (errdetail != NULL) {
    va_list args;
    va_start(args, fmt);
    int needed = vsnprintf(NULL, 0, fmt, args);
    va_end(args);
    if (needed < 0) {
      *errdetail = NULL;
    } else {
      *errdetail = GROW_ARRAY(char, NULL, 0, (size_t)needed + 1);
      va_start(args, fmt);
      vsnprintf(*errdetail, (size_t)needed + 1, fmt, args);
      va_end(args);
    }
  }
  return errnum;
}

int8_t ir_validate(IR_Unit* unit, uint32_t local_count, char **errdetail) {
  if (errdetail != NULL) {
    *errdetail = NULL;
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
      case IR_OP_LIBCALL: {
        if (inst->a < 0) {
          return ir_validate_error(errdetail, ERR_COMP_SYNTAX,
                                   "Instruction %zu (LIBCALL) has negative library index %d.",
                                   i, inst->a);
        }
        if (inst->b < 0) {
          return ir_validate_error(errdetail, ERR_COMP_SYNTAX,
                                   "Instruction %zu (LIBCALL) has negative function index %d.",
                                   i, inst->b);
        }
        break;
      }
      default:
        break;
    }
  }
  return ERR_NOERROR;
}
