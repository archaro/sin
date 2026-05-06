// IR to bytecode emitter (scaffold)
//
// Licensed under the MIT License - see LICENSE file for details.

#include "emitbc.h"

#include <limits.h>
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

static uint32_t inst_size(const IR_INST *in, char **errdetail) {
  switch (in->op) {
    case IR_OP_PUSH_INT: return 1 + 8;
    case IR_OP_PUSH_STR:
      if (errdetail) *errdetail = "IR_OP_PUSH_STR emission is not implemented yet";
      return 0;
    case IR_OP_LOAD_LOCAL:
    case IR_OP_STORE_LOCAL:
    case IR_OP_INC_LOCAL:
    case IR_OP_DEC_LOCAL:
      return 1 + 1;
    case IR_OP_ADD:
    case IR_OP_SUB:
    case IR_OP_MUL:
    case IR_OP_DIV:
    case IR_OP_EQ:
    case IR_OP_NE:
    case IR_OP_GT:
    case IR_OP_GE:
    case IR_OP_LT:
    case IR_OP_LE:
    case IR_OP_AND:
    case IR_OP_OR:
    case IR_OP_NOT:
    case IR_OP_EXISTS:
    case IR_OP_DELETE:
    case IR_OP_NTHNAME:
    case IR_OP_ROOTNAME:
    case IR_OP_HALT:
      return 1;
    case IR_OP_JUMP:
    case IR_OP_JUMPFALSE:
      return 1 + 2;
    default:
      if (errdetail) *errdetail = "unimplemented IR opcode in inst_size";
      return 0;
  }
}

static bool emit_inst(const IR_INST *in,
                      const uint32_t *inst_pc,
                      uint32_t inst_count,
                      uint32_t pc,
                      BC_BUF *out,
                      char **errdetail) {
  (void)errdetail;
  switch (in->op) {
    case IR_OP_PUSH_INT:
      return bc_write_u8(out, 'p') && bc_write_i64(out, in->operand.i64);
    case IR_OP_LOAD_LOCAL:
      return bc_write_u8(out, 'e') && bc_write_u8(out, in->operand.local);
    case IR_OP_STORE_LOCAL:
      return bc_write_u8(out, 'c') && bc_write_u8(out, in->operand.local);
    case IR_OP_INC_LOCAL:
      return bc_write_u8(out, 'f') && bc_write_u8(out, in->operand.local);
    case IR_OP_DEC_LOCAL:
      return bc_write_u8(out, 'g') && bc_write_u8(out, in->operand.local);
    case IR_OP_ADD:
      return bc_write_u8(out, 'a');
    case IR_OP_SUB:
      return bc_write_u8(out, 's');
    case IR_OP_MUL:
      return bc_write_u8(out, 'm');
    case IR_OP_DIV:
      return bc_write_u8(out, 'd');
    case IR_OP_EQ:
      return bc_write_u8(out, 'o');
    case IR_OP_NE:
      return bc_write_u8(out, 'q');
    case IR_OP_GT:
      return bc_write_u8(out, 't');
    case IR_OP_GE:
      return bc_write_u8(out, 'v');
    case IR_OP_LT:
      return bc_write_u8(out, 'r');
    case IR_OP_LE:
      return bc_write_u8(out, 'u');
    case IR_OP_AND:
      return bc_write_u8(out, 'y');
    case IR_OP_OR:
      return bc_write_u8(out, 'z');
    case IR_OP_NOT:
      return bc_write_u8(out, 'x');
    case IR_OP_EXISTS:
      return bc_write_u8(out, 'X');
    case IR_OP_DELETE:
      return bc_write_u8(out, 'W');
    case IR_OP_NTHNAME:
      return bc_write_u8(out, 'Y');
    case IR_OP_ROOTNAME:
      return bc_write_u8(out, 'Z');
    case IR_OP_JUMP:
    case IR_OP_JUMPFALSE: {
      uint32_t target = in->operand.label;
      if (target >= inst_count) {
        if (errdetail) *errdetail = "jump target instruction index out of range";
        return false;
      }
      uint32_t op_pc = pc + 1;  // pc after opcode byte, matches interpreter nextop
      int64_t rel = (int64_t)inst_pc[target] - (int64_t)op_pc;
      if (rel < INT16_MIN || rel > INT16_MAX) {
        if (errdetail) *errdetail = "jump offset exceeds int16 range";
        return false;
      }
      return bc_write_u8(out, in->op == IR_OP_JUMP ? 'j' : 'k')
             && bc_write_i16(out, (int16_t)rel);
    }
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

  uint32_t *inst_pc = GROW_ARRAY(uint32_t, NULL, 0, ir->count ? ir->count : 1);
  if (!inst_pc) {
    if (errdetail) *errdetail = "oom allocating instruction pc table";
    emitbc_free(out);
    return false;
  }

  uint32_t pc = 2;
  for (uint32_t i = 0; i < ir->count; ++i) {
    inst_pc[i] = pc;
    uint32_t sz = inst_size(&ir->code[i], errdetail);
    if (sz == 0) {
      FREE_ARRAY(uint32_t, inst_pc, ir->count ? ir->count : 1);
      emitbc_free(out);
      return false;
    }
    pc += sz;
  }

  pc = 2;
  for (uint32_t i = 0; i < ir->count; ++i) {
    if (!emit_inst(&ir->code[i], inst_pc, ir->count, pc, out, errdetail)) {
      FREE_ARRAY(uint32_t, inst_pc, ir->count ? ir->count : 1);
      emitbc_free(out);
      return false;
    }
    pc += inst_size(&ir->code[i], NULL);
  }
  FREE_ARRAY(uint32_t, inst_pc, ir->count ? ir->count : 1);

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
