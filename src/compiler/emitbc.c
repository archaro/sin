// Bytecode emitter

// Licensed under the MIT License - see LICENSE file for details.
#include "emitbc.h"

#include <limits.h>
#include <stdio.h>
#include <string.h>

#include "compdiag.h"
#include "compiler/ir/opcode_schema.h"
#include "error.h"
#include "memory.h"

typedef struct {
  OUTPUT_t *out;
  size_t used;
  int failed;
} BC_Writer;


static int bw_ensure(BC_Writer *w, size_t extra) {
  if (w->failed) return 0;
  size_t needed = 0;
  if (alloc_add_overflow(w->used, extra, &needed)) { w->failed = 1; return 0; }
  if (needed <= w->out->maxsize) return 1;
  size_t oldcap = w->out->maxsize;
  size_t newcap = 0;
  if (!alloc_grow_capacity(oldcap, needed, &newcap)) {
    w->failed = 1;
    return 0;
  }
  if (!alloc_grow_array((void **)&w->out->bytecode, oldcap, newcap, sizeof(unsigned char))) {
    w->failed = 1;
    return 0;
  }
  w->out->maxsize = newcap;
  w->out->nextbyte = w->out->bytecode + w->used;
  return 1;
}

static int bw_write_u8(BC_Writer *w, uint8_t v) {
  if (!bw_ensure(w, 1)) return 0;
  *w->out->nextbyte++ = v;
  w->used++;
  return 1;
}
static int bw_write_u16(BC_Writer *w, uint16_t v) {
  if (!bw_ensure(w, sizeof(v))) return 0;
  memcpy(w->out->nextbyte, &v, sizeof(v));
  w->out->nextbyte += sizeof(v);
  w->used += sizeof(v);
  return 1;
}
static int bw_write_i16(BC_Writer *w, int16_t v) {
  if (!bw_ensure(w, sizeof(v))) return 0;
  memcpy(w->out->nextbyte, &v, sizeof(v));
  w->out->nextbyte += sizeof(v);
  w->used += sizeof(v);
  return 1;
}
static int bw_write_u64_payload(BC_Writer *w, uint64_t v) {
  if (!bw_ensure(w, sizeof(v))) return 0;
  memcpy(w->out->nextbyte, &v, sizeof(v));
  w->out->nextbyte += sizeof(v);
  w->used += sizeof(v);
  return 1;
}
static int bw_write_i64(BC_Writer *w, int64_t v) {
  uint64_t payload;
  memcpy(&payload, &v, sizeof(payload));
  return bw_write_u64_payload(w, payload);
}
static int bw_write_bytes(BC_Writer *w, const void *src, size_t n) {
  if (!bw_ensure(w, n)) return 0;
  memcpy(w->out->nextbyte, src, n);
  w->out->nextbyte += n;
  w->used += n;
  return 1;
}

int8_t emit_bytecode(IR_Unit *ir, uint8_t local_count, uint8_t param_count,
                     OUTPUT_t *out, char **errdetail) {
  if (errdetail) compdiag_reset_detail(errdetail);
  if (!ir || !out) {
    int8_t errnum = ERR_NOERROR;
    compdiag_set_once(&errnum, errdetail, ERR_COMP_SYNTAX, "emitbc", "null input");
    return errnum;
  }

  size_t pos_count = ir->function.count > 0 ? ir->function.count : 1;
  size_t *pos = NULL;
  if (!alloc_grow_array((void **)&pos, 0, pos_count, sizeof(size_t))) {
    int8_t errnum = ERR_NOERROR;
    compdiag_set_once(&errnum, errdetail, ERR_COMP_SYNTAX, "emitbc", "out of memory allocating position map");
    return errnum;
  }
  size_t pc = 2;
  for (size_t i = 0; i < ir->function.count; i++) {
    const IR_Inst *in = &ir->function.code[i];
    int isz = 0;
    const IR_OpSchema *meta = ir_opcode_schema(in->op);
    pos[i] = pc;
    if (!meta) continue;
    switch (meta->size_policy) {
      case SIZE_FIXED_0: isz = 0; break;
      case SIZE_FIXED_1: isz = 1; break;
      case SIZE_FIXED_2: isz = 2; break;
      case SIZE_FIXED_3: isz = 3; break;
      case SIZE_PUSH_INT: isz = 1 + 8; break;
      case SIZE_PUSH_STRING: isz = 1 + 2 + (int)strlen((const char *)(intptr_t)in->imm); break;
      case SIZE_ITEM_PUSH_LAYER: isz = 1 + 1 + (int)strlen((const char *)(intptr_t)in->imm); break;
      case SIZE_ITEM_SAVE_CODE:
        isz = 1 + 2;
        if (in->a >= 0 && (size_t)in->a < ir->embedded_code.count) {
          const IR_EmbeddedCodePayload *payload = &ir->embedded_code.entries[in->a];
          if (payload->param_count > 0) {
            isz += 1;
            for (size_t p = 0; p < payload->param_count; p++) {
              isz += 2 + (int)strlen(payload->params[p]);
            }
            isz += 2;
          }
          if (payload->source != NULL) isz += (int)strlen(payload->source);
        }
        break;
    }
    pc += (size_t)isz;
  }

  BC_Writer w = {.out = out, .used = 0, .failed = 0};
  if (!bw_ensure(&w, 2)) goto oom;
  out->nextbyte = out->bytecode;
  if (!bw_write_u8(&w, local_count) || !bw_write_u8(&w, param_count)) goto oom;

  for (size_t i = 0; i < ir->function.count; i++) {
    IR_Inst *in = &ir->function.code[i];
    if (in->op == IR_OP_LABEL) continue;
    const IR_OpSchema *meta = ir_opcode_schema(in->op);
    if (!meta || meta->encoded_symbol == 0) {
      FREE_ARRAY(size_t, pos, ir->function.count > 0 ? ir->function.count : 1);
      {
        int8_t errnum = ERR_NOERROR;
        compdiag_setf_once(&errnum, errdetail, ERR_COMP_SYNTAX, "emitbc", "unsupported IR op %d", in->op);
        return errnum;
      }
    }
    if (!bw_write_u8(&w, meta->encoded_symbol)) goto oom;
    switch (meta->validator) {
      case VALIDATE_NONE: break;
      case VALIDATE_A_U8:
        if (in->a < 0 || in->a > UINT8_MAX) {
          FREE_ARRAY(size_t, pos, ir->function.count > 0 ? ir->function.count : 1);
          int8_t errnum = ERR_NOERROR;
          compdiag_setf_once(&errnum, errdetail, ERR_COMP_SYNTAX, "emitbc", "%s operand a out of range for u8: %d", meta->name, in->a);
          return errnum;
        }
        break;
      case VALIDATE_A_U16:
        if (in->a < 0 || in->a > UINT16_MAX) {
          FREE_ARRAY(size_t, pos, ir->function.count > 0 ? ir->function.count : 1);
          int8_t errnum = ERR_NOERROR;
          compdiag_setf_once(&errnum, errdetail, ERR_COMP_SYNTAX, "emitbc", "%s operand a out of range for u16: %d", meta->name, in->a);
          return errnum;
        }
        break;
      case VALIDATE_A_B_U8:
        if (in->a < 0 || in->a > UINT8_MAX || in->b < 0 || in->b > UINT8_MAX) {
          FREE_ARRAY(size_t, pos, ir->function.count > 0 ? ir->function.count : 1);
          int8_t errnum = ERR_NOERROR;
          compdiag_setf_once(&errnum, errdetail, ERR_COMP_SYNTAX, "emitbc", "%s operands out of range for u8: a=%d b=%d", meta->name, in->a, in->b);
          return errnum;
        }
        break;
      case VALIDATE_LABEL_ID:
      case VALIDATE_NON_NULL_IMM:
      case VALIDATE_EMBEDDED_INDEX:
        break;
    }
    switch (in->op) {
      case IR_OP_PUSH_INT: if (!bw_write_i64(&w, in->imm)) goto oom; break;
      case IR_OP_PUSH_BOOL:
        if (!bw_write_u8(&w, (uint8_t)in->a)) goto oom;
        break;
      case IR_OP_PUSH_STRING: {
        const char *s = (const char *)(intptr_t)in->imm;
        size_t len = strlen(s);
        if (len > UINT16_MAX) {
          FREE_ARRAY(size_t, pos, ir->function.count > 0 ? ir->function.count : 1);
          int8_t errnum = ERR_NOERROR;
          compdiag_setf_once(&errnum, errdetail, ERR_COMP_SYNTAX, "emitbc", "string literal too long: %zu", len);
          return errnum;
        }
        if (!bw_write_u16(&w, (uint16_t)len) || !bw_write_bytes(&w, s, len)) goto oom;
        break;
      }
      case IR_OP_LOAD_LOCAL:
      case IR_OP_STORE_LOCAL:
      case IR_OP_INC_LOCAL:
      case IR_OP_DEC_LOCAL:
      case IR_OP_LIBCALL_TOKEN:
        if (!bw_write_u8(&w, (uint8_t)in->a)) goto oom;
        break;
      case IR_OP_CALL:
        if (!bw_write_u16(&w, (uint16_t)in->a)) goto oom;
        break;
      case IR_OP_JUMP:
      case IR_OP_JUMP_IF_FALSE: {
        if (in->a < 0 || (size_t)in->a >= ir->labels.count) {
          FREE_ARRAY(size_t, pos, ir->function.count > 0 ? ir->function.count : 1);
          int8_t errnum = ERR_NOERROR;
          compdiag_setf_once(&errnum, errdetail, ERR_COMP_SYNTAX, "emitbc", "jump invalid label id %d", in->a);
          return errnum;
        }
        IR_Label *label = &ir->labels.entries[in->a];
        if (!label->bound) {
          FREE_ARRAY(size_t, pos, ir->function.count > 0 ? ir->function.count : 1);
          int8_t errnum = ERR_NOERROR;
          compdiag_setf_once(&errnum, errdetail, ERR_COMP_SYNTAX, "emitbc", "jump unbound label %d", in->a);
          return errnum;
        }
        size_t from = pos[i] + 1;
        size_t to = pos[label->position];
        long diff = (long)to - (long)from;
        if (diff < INT16_MIN || diff > INT16_MAX) {
          FREE_ARRAY(size_t, pos, ir->function.count > 0 ? ir->function.count : 1);
          int8_t errnum = ERR_NOERROR;
          compdiag_setf_once(&errnum, errdetail, ERR_COMP_SYNTAX, "emitbc", "jump offset out of range: %ld", diff);
          return errnum;
        }
        if (!bw_write_i16(&w, (int16_t)diff)) goto oom;
        break;
      }
      case IR_OP_ITEM_PUSH_LAYER: {
        const char *s = (const char *)(intptr_t)in->imm;
        if (!s) {
          FREE_ARRAY(size_t, pos, ir->function.count > 0 ? ir->function.count : 1);
          int8_t errnum = ERR_NOERROR;
          compdiag_set_once(&errnum, errdetail, ERR_COMP_SYNTAX, "emitbc", "null layer name");
          return errnum;
        }
        size_t len = strlen(s);
        if (len > UINT8_MAX) {
          FREE_ARRAY(size_t, pos, ir->function.count > 0 ? ir->function.count : 1);
          int8_t errnum = ERR_NOERROR;
          compdiag_setf_once(&errnum, errdetail, ERR_COMP_SYNTAX, "emitbc", "layer name too long: %zu", len);
          return errnum;
        }
        if (!bw_write_u8(&w, (uint8_t)len) || !bw_write_bytes(&w, s, len)) goto oom;
        break;
      }
      case IR_OP_ITEM_SAVE_CODE: {
        if (in->a < 0 || (size_t)in->a >= ir->embedded_code.count) {
          FREE_ARRAY(size_t, pos, ir->function.count > 0 ? ir->function.count : 1);
          int8_t errnum = ERR_NOERROR;
          compdiag_setf_once(&errnum, errdetail, ERR_COMP_SYNTAX, "emitbc", "invalid embedded code payload index %d", in->a);
          return errnum;
        }
        const IR_EmbeddedCodePayload *payload = &ir->embedded_code.entries[in->a];
        if (payload->param_count > 0) {
          if (!bw_write_u8(&w, 'P')) goto oom;
          for (size_t p = 0; p < payload->param_count; p++) {
            const char *name = payload->params[p];
            size_t nlen = strlen(name);
            if (nlen > UINT16_MAX) {
              FREE_ARRAY(size_t, pos, ir->function.count > 0 ? ir->function.count : 1);
              int8_t errnum = ERR_NOERROR;
              compdiag_setf_once(&errnum, errdetail, ERR_COMP_SYNTAX, "emitbc", "embedded param too long: %zu", nlen);
              return errnum;
            }
            if (!bw_write_u16(&w, (uint16_t)nlen) || !bw_write_bytes(&w, name, nlen)) goto oom;
          }
          if (!bw_write_u16(&w, 0)) goto oom;
        }
        if (!payload->source) {
          FREE_ARRAY(size_t, pos, ir->function.count > 0 ? ir->function.count : 1);
          int8_t errnum = ERR_NOERROR;
          compdiag_setf_once(&errnum, errdetail, ERR_COMP_SYNTAX, "emitbc", "embedded code payload %d has null source", in->a);
          return errnum;
        }
        size_t len = strlen(payload->source);
        if (len > UINT16_MAX) {
          FREE_ARRAY(size_t, pos, ir->function.count > 0 ? ir->function.count : 1);
          int8_t errnum = ERR_NOERROR;
          compdiag_setf_once(&errnum, errdetail, ERR_COMP_SYNTAX, "emitbc", "embedded code too long: %zu", len);
          return errnum;
        }
        if (!bw_write_u16(&w, (uint16_t)len) || !bw_write_bytes(&w, payload->source, len)) goto oom;
        break;
      }
      default: break;
    }
  }

  FREE_ARRAY(size_t, pos, ir->function.count > 0 ? ir->function.count : 1);
  return ERR_NOERROR;
oom:
  FREE_ARRAY(size_t, pos, ir->function.count > 0 ? ir->function.count : 1);
  {
    int8_t errnum = ERR_NOERROR;
    compdiag_set_once(&errnum, errdetail, ERR_COMP_SYNTAX, "emitbc", "bytecode writer out of memory");
    return errnum;
  }
}
