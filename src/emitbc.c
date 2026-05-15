#include "emitbc.h"

#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "error.h"
#include "memory.h"

static int8_t emit_error(char **errdetail, int8_t errnum, const char *fmt, ...) {
  if (errdetail) {
    va_list args;
    va_start(args, fmt);
    int needed = vsnprintf(NULL, 0, fmt, args);
    va_end(args);
    if (needed >= 0) {
      *errdetail = GROW_ARRAY(char, NULL, 0, (size_t)needed + 1);
      va_start(args, fmt);
      vsnprintf(*errdetail, (size_t)needed + 1, fmt, args);
      va_end(args);
    } else {
      *errdetail = NULL;
    }
  }
  return errnum;
}

static int ensure_out(OUTPUT_t *out, size_t extra) {
  size_t used = (size_t)(out->nextbyte - out->bytecode);
  size_t needed = used + extra;
  if (needed <= out->maxsize) return 1;
  size_t oldcap = out->maxsize;
  size_t newcap = GROW_CAPACITY(oldcap);
  while (newcap < needed) newcap = GROW_CAPACITY(newcap);
  out->bytecode = GROW_ARRAY(unsigned char, out->bytecode, oldcap, newcap);
  out->nextbyte = out->bytecode + used;
  out->maxsize = newcap;
  return 1;
}

static void write_u8(OUTPUT_t *out, uint8_t v) { ensure_out(out, 1); *out->nextbyte++ = v; }
static void write_u16(OUTPUT_t *out, uint16_t v) { ensure_out(out, 2); memcpy(out->nextbyte, &v, 2); out->nextbyte += 2; }
static void write_i64(OUTPUT_t *out, int64_t v) { ensure_out(out, 8); memcpy(out->nextbyte, &v, 8); out->nextbyte += 8; }

static int inst_size(const IR_Inst *in) {
  switch (in->op) {
    case IR_OP_LABEL: return 0;
    case IR_OP_PUSH_INT: return 1 + 8;
    case IR_OP_PUSH_STRING: return 1 + 2 + (int)strlen((const char *)(intptr_t)in->imm);
    case IR_OP_LOAD_LOCAL:
    case IR_OP_STORE_LOCAL:
    case IR_OP_INC_LOCAL:
    case IR_OP_DEC_LOCAL:
    case IR_OP_LIBCALL: return 3;
    case IR_OP_CALL: return 3;
    case IR_OP_JUMP:
    case IR_OP_JUMP_IF_FALSE: return 3;
    case IR_OP_ITEM_SAVE_CODE: return 1 + 2;
    default: return 1;
  }
}

static uint8_t map_opcode(IR_Op op) {
  switch (op) {
    case IR_OP_HALT: return 'h';
    case IR_OP_PUSH_INT: return 'p';
    case IR_OP_PUSH_STRING: return 'l';
    case IR_OP_ADD: return 'a'; case IR_OP_SUB: return 's'; case IR_OP_MUL: return 'm'; case IR_OP_DIV: return 'd';
    case IR_OP_NEG: return 'n';
    case IR_OP_EQ: return 'o'; case IR_OP_NEQ: return 'q'; case IR_OP_LT: return 'r'; case IR_OP_GT: return 't'; case IR_OP_LE: return 'u'; case IR_OP_GE: return 'v';
    case IR_OP_NOT: return 'x'; case IR_OP_AND: return 'y'; case IR_OP_OR: return 'z';
    case IR_OP_LOAD_LOCAL: return 'e'; case IR_OP_STORE_LOCAL: return 'c'; case IR_OP_INC_LOCAL: return 'f'; case IR_OP_DEC_LOCAL: return 'g';
    case IR_OP_JUMP: return 'j'; case IR_OP_JUMP_IF_FALSE: return 'k';
    case IR_OP_ITEM_BEGIN: return 'I'; case IR_OP_ITEM_PUSH_LAYER: return 'L'; case IR_OP_ITEM_PUSH_DEREF: return 'D'; case IR_OP_ITEM_END: return 'E'; case IR_OP_ITEM_DEREF: return 'F';
    /*
     * IR_OP_CALL intentionally aliases IR_OP_ITEM_DEREF to opcode 'F'.
     * Both operations dispatch to op_fetchitem in the VM: they pop an item
     * reference from the stack and replace it with the fetched value.
     *
     * Safety comes from IR lowering/validation context, not opcode identity:
     * - ITEM_DEREF appears inside item-assembly flows after IR_OP_ITEM_PUSH_DEREF.
     * - CALL appears after callee + args have been pushed and carries argc as an
     *   immediate 16-bit operand, while ITEM_DEREF has no immediate.
     *
     * Bytecode format note: CALL widened from 1-byte to 2-byte immediate argc.
     */
    case IR_OP_ITEM_SAVE: return 'C'; case IR_OP_EXISTS: return 'X'; case IR_OP_DELETE: return 'W'; case IR_OP_NTHNAME: return 'Y'; case IR_OP_ROOTNAME: return 'Z';
    case IR_OP_CALL: return 'F';
    case IR_OP_LIBCALL: return 'A';
    case IR_OP_ITEM_SAVE_CODE: return 'B';
    default: return 0;
  }
}

int8_t emit_bytecode(IR_Unit *ir, uint8_t local_count, uint8_t param_count,
                     OUTPUT_t *out, char **errdetail) {
  if (errdetail) *errdetail = NULL;
  if (!ir || !out) return emit_error(errdetail, ERR_COMP_SYNTAX, "emit_bytecode: null input");

  size_t *pos = GROW_ARRAY(size_t, NULL, 0, ir->function.count > 0 ? ir->function.count : 1);
  size_t pc = 2;
  for (size_t i = 0; i < ir->function.count; i++) {
    int isz;
    const IR_Inst *in = &ir->function.code[i];
    pos[i] = pc;
    isz = inst_size(in);
    if (in->op == IR_OP_ITEM_SAVE_CODE && in->a >= 0 && (size_t)in->a < ir->embedded_code.count) {
      const IR_EmbeddedCodePayload *payload = &ir->embedded_code.entries[in->a];
      if (payload->source != NULL) {
        isz += (int)strlen(payload->source);
      }
    }
    pc += (size_t)isz;
  }

  ensure_out(out, 2);
  out->nextbyte = out->bytecode;
  write_u8(out, local_count);
  write_u8(out, param_count);

  for (size_t i = 0; i < ir->function.count; i++) {
    IR_Inst *in = &ir->function.code[i];
    if (in->op == IR_OP_LABEL) continue;
    uint8_t op = map_opcode(in->op);
    if (op == 0) {
      FREE_ARRAY(size_t, pos, ir->function.count > 0 ? ir->function.count : 1);
      return emit_error(errdetail, ERR_COMP_SYNTAX, "unsupported IR op %d", in->op);
    }
    write_u8(out, op);
    switch (in->op) {
      case IR_OP_PUSH_INT: write_i64(out, in->imm); break;
      case IR_OP_PUSH_STRING: {
        const char *s = (const char *)(intptr_t)in->imm;
        size_t len = strlen(s);
        if (len > UINT16_MAX) {
          FREE_ARRAY(size_t, pos, ir->function.count > 0 ? ir->function.count : 1);
          return emit_error(errdetail, ERR_COMP_SYNTAX, "string literal too long: %zu", len);
        }
        write_u16(out, (uint16_t)len);
        ensure_out(out, len);
        memcpy(out->nextbyte, s, len);
        out->nextbyte += len;
        break;
      }
      case IR_OP_LOAD_LOCAL:
      case IR_OP_STORE_LOCAL:
      case IR_OP_INC_LOCAL:
      case IR_OP_DEC_LOCAL:
        write_u8(out, (uint8_t)in->a);
        break;
      case IR_OP_LIBCALL:
        write_u8(out, (uint8_t)in->a);
        write_u8(out, (uint8_t)in->b);
        break;
      case IR_OP_CALL:
        write_u16(out, (uint16_t)in->a);
        break;
      case IR_OP_JUMP:
      case IR_OP_JUMP_IF_FALSE: {
        if (in->a < 0 || (size_t)in->a >= ir->labels.count) {
          FREE_ARRAY(size_t, pos, ir->function.count > 0 ? ir->function.count : 1);
          return emit_error(errdetail, ERR_COMP_SYNTAX, "jump invalid label id %d", in->a);
        }
        IR_Label *label = &ir->labels.entries[in->a];
        if (!label->bound) {
          FREE_ARRAY(size_t, pos, ir->function.count > 0 ? ir->function.count : 1);
          return emit_error(errdetail, ERR_COMP_SYNTAX, "jump unbound label %d", in->a);
        }
        size_t from = pos[i] + 1;
        size_t to = pos[label->position];
        long diff = (long)to - (long)from;
        if (diff < INT16_MIN || diff > INT16_MAX) {
          FREE_ARRAY(size_t, pos, ir->function.count > 0 ? ir->function.count : 1);
          return emit_error(errdetail, ERR_COMP_SYNTAX, "jump offset out of range: %ld", diff);
        }
        write_u16(out, (uint16_t)(int16_t)diff);
        break;
      }
      case IR_OP_ITEM_PUSH_LAYER: {
        const char *s = (const char *)(intptr_t)in->imm;
        if (!s) {
          FREE_ARRAY(size_t, pos, ir->function.count > 0 ? ir->function.count : 1);
          return emit_error(errdetail, ERR_COMP_SYNTAX, "null layer name");
        }
        size_t len = strlen(s);
        if (len > UINT8_MAX) {
          FREE_ARRAY(size_t, pos, ir->function.count > 0 ? ir->function.count : 1);
          return emit_error(errdetail, ERR_COMP_SYNTAX, "layer name too long: %zu", len);
        }
        write_u8(out, (uint8_t)len);
        ensure_out(out, len);
        memcpy(out->nextbyte, s, len);
        out->nextbyte += len;
        break;
      }
      case IR_OP_ITEM_SAVE_CODE: {
        if (in->a < 0 || (size_t)in->a >= ir->embedded_code.count) {
          FREE_ARRAY(size_t, pos, ir->function.count > 0 ? ir->function.count : 1);
          return emit_error(errdetail, ERR_COMP_SYNTAX, "invalid embedded code payload index %d", in->a);
        }
        const IR_EmbeddedCodePayload *payload = &ir->embedded_code.entries[in->a];
        if (!payload->source) {
          FREE_ARRAY(size_t, pos, ir->function.count > 0 ? ir->function.count : 1);
          return emit_error(errdetail, ERR_COMP_SYNTAX, "embedded code payload %d has null source", in->a);
        }
        size_t len = strlen(payload->source);
        if (len > UINT16_MAX) {
          FREE_ARRAY(size_t, pos, ir->function.count > 0 ? ir->function.count : 1);
          return emit_error(errdetail, ERR_COMP_SYNTAX, "embedded code too long: %zu", len);
        }
        write_u16(out, (uint16_t)len);
        ensure_out(out, len);
        memcpy(out->nextbyte, payload->source, len);
        out->nextbyte += len;
        break;
      }
      default: break;
    }
  }

  FREE_ARRAY(size_t, pos, ir->function.count > 0 ? ir->function.count : 1);
  return ERR_NOERROR;
}
