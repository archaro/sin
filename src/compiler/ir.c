// Intermediate representation (IR) module implementation.

// Licensed under the MIT License - see LICENSE file for details.

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#include "compiler/compdiag.h"
#include "error.h"
#include "compiler/ir.h"
#include "memory.h"
#include "list.h"
#include "string_limits.h"

static bool ensure_inst_capacity(IR_Function* function, size_t needed) {
  if (function->capacity >= needed) {
    return true;
  }
  size_t oldcap = function->capacity;
  size_t newcap = 0;
  if (!alloc_grow_capacity(oldcap, needed, &newcap)) return false;
  if (!alloc_grow_array((void **)&function->code, newcap, sizeof(IR_Inst))) return false;
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
  if (!alloc_grow_array((void **)&labels->entries, newcap, sizeof(IR_Label))) return false;
  labels->capacity = newcap;
  return true;
}

IR_Unit* ir_create_unit(void) {
  return calloc(1, sizeof(IR_Unit));
}

void ir_destroy_unit(IR_Unit* unit) {
  if (unit == NULL) {
    return;
  }

  free(unit->function.code);
  free(unit->labels.entries);
  for (size_t i = 0; i < unit->embedded_code.count; i++) {
    IR_EmbeddedCodePayload* payload = &unit->embedded_code.entries[i];
    free(payload->params);
    free(payload->locals);
  }
  free(unit->embedded_code.entries);
  free(unit);
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
  label->span = COMPILER_SOURCE_SPAN_INVALID;

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
  if (!alloc_grow_array((void **)&table->entries, newcap, sizeof(IR_EmbeddedCodePayload))) return false;
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

static void ir_reset_embedded_payload_locals(IR_EmbeddedCodePayload* payload) {
  free(payload->params);
  free(payload->locals);
  payload->param_count = 0;
  payload->params = NULL;
  payload->local_count = 0;
  payload->locals = NULL;
}

bool ir_embedded_locals_from_params(AS_NODE* params, IR_EmbeddedCodePayload* payload) {
  if (!payload) return false;

  payload->param_count = 0;
  payload->params = NULL;
  payload->local_count = 0;
  payload->locals = NULL;

  AS_NODE* cursor = params;
  size_t count = 0;
  while (cursor) {
    if (cursor->nodetype != N_ARGLIST) return false;
    count++;
    cursor = (AS_NODE*)cursor->rhs;
  }

  payload->param_count = count;
  payload->local_count = count;
  if (count > 0) {
    payload->params = alloc_calloc(count, sizeof *payload->params);
    payload->locals = alloc_calloc(count, sizeof *payload->locals);
    if (!payload->params || !payload->locals) {
      ir_reset_embedded_payload_locals(payload);
      return false;
    }
  }

  cursor = params;
  for (size_t i = 0; i < count; i++) {
    AS_NODE* param = (AS_NODE*)cursor->lhs;
    AS_VALUE* value;
    if (!param || param->nodetype != N_VALUE) {
      ir_reset_embedded_payload_locals(payload);
      return false;
    }
    value = (AS_VALUE*)param->lhs;
    if (!value || value->valtype != V_LOCAL || !value->value.s) {
      ir_reset_embedded_payload_locals(payload);
      return false;
    }
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

#if defined(__GNUC__) || defined(__clang__)
static int8_t ir_validate_error_at(char **errdetail, CompilerDiagnostic *diag,
                                   CompilerSourceSpan span, int8_t errnum,
                                   const char *fmt, ...)
    __attribute__((format(printf, 5, 6)));
#else
static int8_t ir_validate_error_at(char **errdetail, CompilerDiagnostic *diag,
                                   CompilerSourceSpan span, int8_t errnum,
                                   const char *fmt, ...);
#endif

static int8_t ir_validate_error_at(char **errdetail, CompilerDiagnostic *diag,
                                   CompilerSourceSpan span, int8_t errnum,
                                   const char *fmt, ...) {
  va_list args;
  int needed;
  char *msg;

  if (errdetail == NULL && diag == NULL) return errnum;

  va_start(args, fmt);
  needed = vsnprintf(NULL, 0, fmt, args);
  va_end(args);
  if (needed < 0) {
    int8_t current = ERR_NOERROR;
    compdiag_set_once_diag(&current, errdetail, diag, errnum, DIAG_PHASE_IR_VALIDATE, "ir", "formatting error");
    if (diag) compiler_diag_set_span(diag, span);
    return errnum;
  }

  msg = malloc((size_t)needed + 1);
  if (!msg) {
    int8_t current = ERR_NOERROR;
    compdiag_set_once_diag(&current, errdetail, diag, errnum, DIAG_PHASE_IR_VALIDATE, "ir", "out of memory");
    if (diag) compiler_diag_set_span(diag, span);
    return errnum;
  }

  va_start(args, fmt);
  vsnprintf(msg, (size_t)needed + 1, fmt, args);
  va_end(args);

  {
    int8_t current = ERR_NOERROR;
    compdiag_set_once_diag(&current, errdetail, diag, errnum, DIAG_PHASE_IR_VALIDATE, "ir", msg);
    if (diag) compiler_diag_set_span(diag, span);
  }
  free(msg);
  return errnum;
}

static int8_t ir_validate_embedded_payload(
    const IR_EmbeddedCodePayload *payload, size_t index, char **errdetail,
    CompilerDiagnostic *diag) {
  if (!payload->source ||
      (payload->param_count > 0 && !payload->params)) {
    return ir_validate_error_at(
        errdetail, diag, payload->span, ERR_COMP_SYNTAX,
        "Embedded code payload %zu has inconsistent source or parameters.",
        index);
  }
  if (strlen(payload->source) > UINT16_MAX) {
    return ir_validate_error_at(errdetail, diag, payload->span, ERR_COMP_SYNTAX,
                             "Embedded code payload %zu source is too long.",
                             index);
  }
  for (size_t p = 0; p < payload->param_count; p++) {
    if (!payload->params[p]) {
      return ir_validate_error_at(
          errdetail, diag, payload->span, ERR_COMP_SYNTAX,
          "Embedded code payload %zu parameter %zu is null.", index, p);
    }
    size_t param_len = strlen(payload->params[p]);
    if (param_len == 0 || param_len > UINT16_MAX) {
      return ir_validate_error_at(
          errdetail, diag, payload->span, ERR_COMP_SYNTAX,
          "Embedded code payload %zu parameter %zu has invalid length %zu.",
          index, p, param_len);
    }
  }
  return ERR_NOERROR;
}

#define ir_validate_error(errdetail_arg, diag_arg, errnum_arg, ...) \
  ir_validate_error_at((errdetail_arg), (diag_arg), current_span, \
                       (errnum_arg), __VA_ARGS__)

int8_t ir_validate_diag(IR_Unit* unit, uint32_t local_count, char **errdetail, CompilerDiagnostic *diag) {
  if (diag) compiler_diag_reset(diag);
  if (errdetail != NULL) {
    compdiag_reset_detail(errdetail);
  }

  CompilerSourceSpan current_span = COMPILER_SOURCE_SPAN_INVALID;

  if (unit == NULL) {
    return ir_validate_error(errdetail, diag, ERR_COMP_SYNTAX, "IR unit is null.");
  }

  if (unit->function.count > unit->function.capacity ||
      unit->labels.count > unit->labels.capacity ||
      unit->embedded_code.count > unit->embedded_code.capacity ||
      (unit->function.count > 0 && unit->function.code == NULL) ||
      (unit->labels.count > 0 && unit->labels.entries == NULL) ||
      (unit->embedded_code.count > 0 && unit->embedded_code.entries == NULL)) {
    return ir_validate_error(errdetail, diag, ERR_COMP_SYNTAX,
                             "IR table has inconsistent count, capacity, or storage.");
  }

  for (size_t i = 0; i < unit->embedded_code.count; i++) {
    int8_t rc = ir_validate_embedded_payload(
        &unit->embedded_code.entries[i], i, errdetail, diag);
    if (rc != ERR_NOERROR) return rc;
  }

  for (size_t i = 0; i < unit->labels.count; i++) {
    IR_Label* label = &unit->labels.entries[i];
    current_span = label->span;
    if (!label->bound) {
      return ir_validate_error(errdetail, diag, ERR_COMP_SYNTAX,
                               "Unbound label .L%d (jump unbound label).", label->id);
    }
    if (label->position > unit->function.count) {
      return ir_validate_error(errdetail, diag, ERR_COMP_SYNTAX,
                               "Label .L%d has invalid position.", label->id);
    }
  }

  for (size_t i = 0; i < unit->function.count; i++) {
    const IR_Inst* inst = &unit->function.code[i];
    current_span = inst->span;
    const IR_OpSchema *meta = ir_opcode_schema(inst->op);
    if (!meta || (meta->encoded_symbol == 0 && inst->op != IR_OP_LABEL)) {
      return ir_validate_error(errdetail, diag, ERR_COMP_SYNTAX,
                               "unsupported IR op %d at instruction %zu.", inst->op, i);
    }
    switch (meta->validator) {
      case VALIDATE_NONE:
        break;
      case VALIDATE_A_U8:
        if (inst->op == IR_OP_LOAD_LOCAL || inst->op == IR_OP_STORE_LOCAL ||
            inst->op == IR_OP_INC_LOCAL || inst->op == IR_OP_DEC_LOCAL ||
            inst->op == IR_OP_ITEM_PUSH_DEREF_LOCAL) {
          if (inst->a < 0 || (uint32_t)inst->a >= local_count) {
            return ir_validate_error(errdetail, diag,
                                     ERR_COMP_LOCALBEFOREDEF,
                                     "Instruction %zu (%s) has out-of-range local index %d (locals=%u).",
                                     i, meta->name, inst->a, local_count);
          }
        } else if (inst->op == IR_OP_PUSH_BOOL) {
          if (inst->a < 0 || inst->a > 1) {
            return ir_validate_error(errdetail, diag, ERR_COMP_SYNTAX,
                                     "Instruction %zu (PUSH_BOOL) has invalid boolean operand %d.",
                                     i, inst->a);
          }
        } else if (inst->a < 0 || inst->a > UINT8_MAX) {
          return ir_validate_error(errdetail, diag, ERR_COMP_SYNTAX,
                                   "Instruction %zu (%s) has invalid u8 operand.",
                                   i, meta->name);
        }
        break;
      case VALIDATE_A_U16:
        if (inst->a < 0) {
          if (inst->op == IR_OP_CALL) {
            return ir_validate_error(errdetail, diag,
                                     ERR_COMP_TOOMANYARGS,
                                     "Instruction %zu (CALL) has negative arity %d.",
                                     i, inst->a);
          }
          return ir_validate_error(errdetail, diag, ERR_COMP_SYNTAX,
                                   "Instruction %zu (%s) has negative operand %d.",
                                   i, meta->name, inst->a);
        }
        if (inst->a > UINT16_MAX) {
          if (inst->op == IR_OP_CALL) {
            return ir_validate_error(errdetail, diag,
                                     ERR_COMP_TOOMANYARGS,
                                     "Instruction %zu (CALL) arity %d exceeds bytecode range.",
                                     i, inst->a);
          }
          return ir_validate_error(errdetail, diag, ERR_COMP_SYNTAX,
                                   "Instruction %zu (%s) operand %d exceeds bytecode range.",
                                   i, meta->name, inst->a);
        }
        break;
      case VALIDATE_A_U32:
        if (inst->a < 0) {
          return ir_validate_error(errdetail, diag, ERR_COMP_SYNTAX,
                                   "Instruction %zu (%s) has negative operand %d.",
                                   i, meta->name, inst->a);
        }
        if (inst->op == IR_OP_BUILD_LIST &&
            (uint32_t)inst->a > SIN_LIST_MAX_ELEMENTS) {
          return ir_validate_error(errdetail, diag, ERR_COMP_SYNTAX,
                                   "Instruction %zu (BUILD_LIST) list count %d exceeds maximum %u.",
                                   i, inst->a,
                                   (unsigned)SIN_LIST_MAX_ELEMENTS);
        }
        break;
      case VALIDATE_A_B_U8:
        if (inst->a < 0 || inst->a > UINT8_MAX ||
            inst->b < 0 || inst->b > UINT8_MAX) {
          return ir_validate_error(errdetail, diag, ERR_COMP_SYNTAX,
                                   "Instruction %zu (%s) has invalid u8 operands (%d,%d).",
                                   i, meta->name, inst->a, inst->b);
        }
        break;
      case VALIDATE_LIBCALL_PAIR:
        if (inst->a < 0 || inst->a > UINT8_MAX ||
            inst->b < 0 || inst->b > UINT8_MAX) {
          return ir_validate_error(errdetail, diag, ERR_COMP_SYNTAX,
                                   "Instruction %zu (LIBCALL) has invalid pair (%d,%d).",
                                   i, inst->a, inst->b);
        }
        break;
      case VALIDATE_LABEL_ID: {
        if (inst->a < 0 || (size_t)inst->a >= unit->labels.count) {
          return ir_validate_error(errdetail, diag, ERR_COMP_SYNTAX,
                                   "Instruction %zu (%s) references invalid label id %d (jump invalid label id).",
                                   i, meta->name, inst->a);
        }
        if (!unit->labels.entries[inst->a].bound) {
          return ir_validate_error(errdetail, diag, ERR_COMP_SYNTAX,
                                   "Instruction %zu (%s) references unbound label .L%d (jump unbound label).",
                                   i, meta->name, inst->a);
        }
        if ((inst->op == IR_OP_JUMP || inst->op == IR_OP_JUMP_IF_FALSE) &&
            unit->labels.entries[inst->a].position >=
                unit->function.count) {
          return ir_validate_error(errdetail, diag, ERR_COMP_SYNTAX,
                                   "Instruction %zu (%s) references label .L%d with invalid position.",
                                   i, meta->name, inst->a);
        }
        break;
      }
      case VALIDATE_NON_NULL_IMM: {
        if (inst->imm == 0) {
          return ir_validate_error(errdetail, diag, ERR_COMP_SYNTAX,
                                   "Instruction %zu (%s) has null string payload.",
                                   i, meta->name);
        }
        size_t len = strlen((const char *)(intptr_t)inst->imm);
        size_t limit = meta->size_policy == SIZE_ITEM_PUSH_LAYER
                           ? (size_t)UINT8_MAX
                           : SIN_MAX_STRING_BYTES;
        if (len > limit) {
          return ir_validate_error(errdetail, diag, ERR_COMP_SYNTAX,
                                   "Instruction %zu (%s) string payload is too long (%zu).",
                                   i, meta->name, len);
        }
        break;
      }
      case VALIDATE_EMBEDDED_INDEX: {
        if (inst->a < 0 || (size_t)inst->a >= unit->embedded_code.count) {
          return ir_validate_error(errdetail, diag, ERR_COMP_SYNTAX,
                                   "Instruction %zu references invalid embedded code index %d.",
                                   i, inst->a);
        }
        break;
      }
    }
  }
  return ERR_NOERROR;
}
#undef ir_validate_error
int8_t ir_validate(IR_Unit* unit, uint32_t local_count, char **errdetail) {
  return ir_validate_diag(unit, local_count, errdetail, NULL);
}
