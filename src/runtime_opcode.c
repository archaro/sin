// Runtime opcode declarations and dispatch binding

// Licensed under the MIT License - see LICENSE file for details.

#include <assert.h>
#include <stdint.h>

#include "runtime_opcode.h"
#include "bytecode_verify.h"
#include "compiler/ir/opcode_schema.h"

#define RUNTIME_OPCODE_TABLE(OP) \
  OP(0, op_nop) \
  OP('a', op_add) \
  OP('b', op_pushbool) \
  OP('c', op_savelocal) \
  OP('d', op_divide) \
  OP('e', op_getlocal) \
  OP('f', op_inclocal) \
  OP('g', op_declocal) \
  OP('j', op_jump) \
  OP('k', op_jumpfalse) \
  OP('l', op_pushstr) \
  OP('m', op_multiply) \
  OP('n', op_negate) \
  OP('o', op_equal) \
  OP('p', op_pushint) \
  OP('P', op_pushfloat) \
  OP('q', op_notequal) \
  OP('r', op_lessthan) \
  OP('s', op_subtract) \
  OP('t', op_greaterthan) \
  OP('u', op_lessthanorequal) \
  OP('v', op_greaterthanorequal) \
  OP('x', op_logicalnot) \
  OP('y', op_logicaland) \
  OP('z', op_logicalor) \
  OP('M', op_libcall_token) \
  OP('B', op_assigncodeitem) \
  OP('C', op_assignitem) \
  OP('F', op_fetchitem) \
  OP('I', op_assembleitem) \
  OP('R', op_assembleitem_rel) \
  OP('W', op_delete) \
  OP('X', op_exists) \
  OP('Y', op_nthname) \
  OP('Z', op_rootname)

#define DECLARE_RUNTIME_OPCODE(opcode_byte, handler_fn) \
  uint8_t *handler_fn(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item);
RUNTIME_OPCODE_TABLE(DECLARE_RUNTIME_OPCODE)
#undef DECLARE_RUNTIME_OPCODE
uint8_t *op_undefined(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item);

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

void runtime_opcode_bind_table(RuntimeContext *ctx) {
  for (int o = 0; o < 256; o++) {
    ctx->opcode[o] = op_undefined;
  }
#define BIND_RUNTIME_OPCODE(opcode_byte, handler_fn) \
  ctx->opcode[(uint8_t)(opcode_byte)] = handler_fn;
  RUNTIME_OPCODE_TABLE(BIND_RUNTIME_OPCODE)
#undef BIND_RUNTIME_OPCODE

#ifndef NDEBUG
  RuntimeBindingCheckCtx binding_ctx = {ctx->opcode};
  ir_opcode_schema_for_each_runtime_opcode(assert_runtime_binding, &binding_ctx);
  assert(bc_opcode_lookup('h', BC_CONTEXT_STATEMENT) != NULL);
#endif
}
