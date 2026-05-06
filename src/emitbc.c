// IR to bytecode emitter (scaffold)
//
// Licensed under the MIT License - see LICENSE file for details.

#include "emitbc.h"

#include <string.h>

#include "memory.h"

static bool bc_reserve(BC_BUF *out, size_t addl) {
  if (out->len + addl <= out->cap) {
    return true;
  }
  size_t oldcap = out->cap;
  size_t newcap = out->cap ? out->cap : 16;
  while (newcap < out->len + addl) {
    newcap *= 2;
  }
  out->data = GROW_ARRAY(uint8_t, out->data, oldcap, newcap);
  out->cap = newcap;
  return out->data != NULL;
}

static bool bc_write_u8(BC_BUF *out, uint8_t b) {
  if (!bc_reserve(out, 1)) return false;
  out->data[out->len++] = b;
  return true;
}

static bool bc_write_i16(BC_BUF *out, int16_t v) {
  if (!bc_reserve(out, 2)) return false;
  memcpy(out->data + out->len, &v, 2);
  out->len += 2;
  return true;
}

static bool bc_write_i64(BC_BUF *out, int64_t v) {
  if (!bc_reserve(out, 8)) return false;
  memcpy(out->data + out->len, &v, 8);
  out->len += 8;
  return true;
}

// TODO: complete switch coverage for all IR ops from docs/ir-bytecode-mapping.md.
static bool emit_inst(const IR_INST *in, BC_BUF *out, char **errdetail) {
  (void)errdetail;
  switch (in->op) {
    case IR_OP_PUSH_INT:
      return bc_write_u8(out, 'p') && bc_write_i64(out, in->operand.i64);
    case IR_OP_LOAD_LOCAL:
      return bc_write_u8(out, 'e') && bc_write_u8(out, in->operand.local);
    case IR_OP_STORE_LOCAL:
      return bc_write_u8(out, 'c') && bc_write_u8(out, in->operand.local);
    case IR_OP_ADD:
      return bc_write_u8(out, 'a');
    case IR_OP_SUB:
      return bc_write_u8(out, 's');
    case IR_OP_MUL:
      return bc_write_u8(out, 'm');
    case IR_OP_DIV:
      return bc_write_u8(out, 'd');
    case IR_OP_JUMP:
      // Placeholder until label/pc fixup is implemented.
      return bc_write_u8(out, 'j') && bc_write_i16(out, 0);
    case IR_OP_JUMPFALSE:
      // Placeholder until label/pc fixup is implemented.
      return bc_write_u8(out, 'k') && bc_write_i16(out, 0);
    case IR_OP_HALT:
      return bc_write_u8(out, 'h');
    default:
      if (errdetail) {
        *errdetail = "unimplemented IR opcode in emit_inst";
      }
      return false;
  }
}

bool emit_bytecode_from_ir(const IR_CTX *ir,
                           uint8_t numlocals,
                           uint8_t numparams,
                           BC_BUF *out,
                           char **errdetail) {
  if (errdetail) *errdetail = NULL;
  if (!ir || !out) {
    if (errdetail) *errdetail = "emit_bytecode_from_ir invalid arguments";
    return false;
  }

  out->data = NULL;
  out->len = 0;
  out->cap = 0;

  if (!bc_write_u8(out, numlocals) || !bc_write_u8(out, numparams)) {
    if (errdetail) *errdetail = "oom while writing bytecode header";
    emitbc_free(out);
    return false;
  }

  for (uint32_t i = 0; i < ir->count; ++i) {
    if (!emit_inst(&ir->code[i], out, errdetail)) {
      emitbc_free(out);
      return false;
    }
  }

  if (ir->count == 0 || ir->code[ir->count - 1].op != IR_OP_HALT) {
    if (!bc_write_u8(out, 'h')) {
      if (errdetail) *errdetail = "oom while writing halt opcode";
      emitbc_free(out);
      return false;
    }
  }

  return true;
}

void emitbc_free(BC_BUF *buf) {
  if (!buf) return;
  if (buf->data) {
    FREE_ARRAY(uint8_t, buf->data, buf->cap);
  }
  buf->data = NULL;
  buf->len = 0;
  buf->cap = 0;
}
