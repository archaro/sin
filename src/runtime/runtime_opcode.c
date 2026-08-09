// Runtime opcode declarations and dispatch binding

// Licensed under the MIT License - see LICENSE file for details.

#include <assert.h>
#include <stdint.h>

#include "runtime_opcode.h"
#include "bytecode_verify.h"
#include "bytecode/bytecode_abi.h"

#define DECLARE_RUNTIME_OPCODE_1(handler_fn) \
  uint8_t *handler_fn(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item);
#define DECLARE_RUNTIME_OPCODE_0(handler_fn)
#define DECLARE_RUNTIME_OPCODE(REQUIRES_RUNTIME_HANDLER, handler_fn) \
  DECLARE_RUNTIME_OPCODE_##REQUIRES_RUNTIME_HANDLER(handler_fn)
#define DECLARE_RUNTIME_OPCODE_SELECT(REQUIRES_RUNTIME_HANDLER, handler_fn) \
  DECLARE_RUNTIME_OPCODE(REQUIRES_RUNTIME_HANDLER, handler_fn)
#define OP(enum_name, encoded_symbol, contexts, requires_runtime_handler, operand_kind, size_policy, validator, handler_fn, stack_meta, control_class) \
  DECLARE_RUNTIME_OPCODE_SELECT(requires_runtime_handler, handler_fn)
#include "bytecode/opcode_schema.def"
#undef OP
#undef DECLARE_RUNTIME_OPCODE_SELECT
#undef DECLARE_RUNTIME_OPCODE
#undef DECLARE_RUNTIME_OPCODE_0
#undef DECLARE_RUNTIME_OPCODE_1

uint8_t *op_nop(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item);
uint8_t *op_undefined(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item);

#ifndef NDEBUG
typedef struct {
  OP_t *table;
} RuntimeBindingCheckCtx;

static bool assert_runtime_binding(uint8_t opbyte, IR_Op op, const IR_OpSchema *schema, void *raw_ctx) {
  (void)op;
  (void)schema;
  RuntimeBindingCheckCtx *check_ctx = (RuntimeBindingCheckCtx *)raw_ctx;
  assert(check_ctx->table[opbyte] != op_undefined
         && "Missing interpreter handler for schema-defined runtime opcode");
  return true;
}
#endif

void runtime_opcode_bind_table(RuntimeContext *ctx) {
  for (int o = 0; o < 256; o++) {
    ctx->opcode[o] = op_undefined;
  }
  ctx->opcode[0] = op_nop;

#define BIND_RUNTIME_OPCODE_1(opcode_byte, handler_fn) \
  ctx->opcode[(uint8_t)(opcode_byte)] = handler_fn;
#define BIND_RUNTIME_OPCODE_0(opcode_byte, handler_fn)
#define BIND_RUNTIME_OPCODE(REQUIRES_RUNTIME_HANDLER, opcode_byte, handler_fn) \
  BIND_RUNTIME_OPCODE_##REQUIRES_RUNTIME_HANDLER(opcode_byte, handler_fn)
#define BIND_RUNTIME_OPCODE_SELECT(REQUIRES_RUNTIME_HANDLER, opcode_byte, handler_fn) \
  BIND_RUNTIME_OPCODE(REQUIRES_RUNTIME_HANDLER, opcode_byte, handler_fn)
#define OP(enum_name, encoded_symbol, contexts, requires_runtime_handler, operand_kind, size_policy, validator, handler_fn, stack_meta, control_class) \
  BIND_RUNTIME_OPCODE_SELECT(requires_runtime_handler, encoded_symbol, handler_fn)
#include "bytecode/opcode_schema.def"
#undef OP
#undef BIND_RUNTIME_OPCODE_SELECT
#undef BIND_RUNTIME_OPCODE
#undef BIND_RUNTIME_OPCODE_0
#undef BIND_RUNTIME_OPCODE_1

#ifndef NDEBUG
  RuntimeBindingCheckCtx binding_ctx = {ctx->opcode};
  ir_opcode_schema_for_each_runtime_opcode(assert_runtime_binding, &binding_ctx);
  assert(bc_opcode_lookup('h', BC_CONTEXT_STATEMENT) != NULL);
#endif
}
