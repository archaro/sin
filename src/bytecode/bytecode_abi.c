// Canonical bytecode ABI schema and validation.

// Licensed under the MIT License - see LICENSE file for details.

#include "bytecode_abi.h"

#include <stdio.h>
#include <stdlib.h>

const IR_OpSchema g_ir_opcode_schema[] = {
#define RUNTIME_HANDLER_NAME_1(HANDLER) #HANDLER
#define RUNTIME_HANDLER_NAME_0(HANDLER) NULL
#define RUNTIME_HANDLER_NAME_SELECT(REQUIRES_RUNTIME_HANDLER, HANDLER) \
    RUNTIME_HANDLER_NAME_##REQUIRES_RUNTIME_HANDLER(HANDLER)
#define RUNTIME_HANDLER_NAME(REQUIRES_RUNTIME_HANDLER, HANDLER) \
    RUNTIME_HANDLER_NAME_SELECT(REQUIRES_RUNTIME_HANDLER, HANDLER)
#define OP(NAME, SYMBOL, CONTEXTS, REQUIRES_RUNTIME_HANDLER, OPERAND, SIZE, VALIDATOR, HANDLER, STACK_META, CONTROL_CLASS) \
    [IR_OP_##NAME] = {IR_OP_##NAME, #NAME, (uint8_t)(SYMBOL), (uint8_t)(CONTEXTS), \
                      REQUIRES_RUNTIME_HANDLER, OPERAND, SIZE, VALIDATOR, \
                      RUNTIME_HANDLER_NAME(REQUIRES_RUNTIME_HANDLER, HANDLER), \
                      STACK_META, .control_class = CONTROL_CLASS},
#include "opcode_schema.def"
#undef OP
#undef RUNTIME_HANDLER_NAME
#undef RUNTIME_HANDLER_NAME_SELECT
#undef RUNTIME_HANDLER_NAME_0
#undef RUNTIME_HANDLER_NAME_1
};

const size_t g_ir_opcode_schema_count =
    sizeof(g_ir_opcode_schema) / sizeof(g_ir_opcode_schema[0]);

const IR_OpSchema *ir_opcode_schema(IR_Op op) {
  if (op < 0 || op >= (IR_Op)g_ir_opcode_schema_count) return NULL;
  return &g_ir_opcode_schema[op];
}

void ir_opcode_schema_for_each_runtime_opcode(IR_RuntimeOpcodeVisitor visitor,
                                               void *ctx) {
  if (!visitor) return;
  for (size_t i = 0; i < g_ir_opcode_schema_count; i++) {
    const IR_OpSchema *meta = &g_ir_opcode_schema[i];
    if (!meta->requires_runtime_handler || meta->encoded_symbol == 0) continue;
    if (!visitor(meta->encoded_symbol, meta->op, meta, ctx)) return;
  }
}

static bool set_schema_error(char **errdetail, uint8_t opcode,
                             const char *first, const char *second) {
  int needed = snprintf(NULL, 0,
                        "ambiguous opcode encoding '%c' for %s and %s "
                        "(overlapping contexts)",
                        (char)opcode, first, second);
  if (needed < 0) return false;
  char *message = malloc((size_t)needed + 1u);
  if (!message) return false;
  snprintf(message, (size_t)needed + 1u,
           "ambiguous opcode encoding '%c' for %s and %s "
           "(overlapping contexts)",
           (char)opcode, first, second);
  if (errdetail) {
    free(*errdetail);
    *errdetail = message;
  } else {
    free(message);
  }
  return true;
}

bool ir_opcode_schema_validate_unique(const IR_OpSchema *schema, size_t count,
                                      char **errdetail) {
  if (errdetail) {
    free(*errdetail);
    *errdetail = NULL;
  }
  if (!schema && count != 0) return false;
  for (size_t i = 0; i < count; i++) {
    const IR_OpSchema *current = &schema[i];
    if (current->encoded_symbol == 0 || current->context_mask == 0) continue;
    for (size_t j = 0; j < i; j++) {
      const IR_OpSchema *previous = &schema[j];
      if (previous->encoded_symbol != current->encoded_symbol ||
          previous->context_mask == 0 ||
          (previous->context_mask & current->context_mask) == 0) {
        continue;
      }
      (void)set_schema_error(errdetail, current->encoded_symbol,
                             previous->name ? previous->name : "<unnamed>",
                             current->name ? current->name : "<unnamed>");
      return false;
    }
  }
  return true;
}
