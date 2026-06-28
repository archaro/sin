#include "bytecode_verify.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef enum {
  BC_CTX_STMT = 0,
  BC_CTX_ITEM = 1,
  BC_CTX_DEREF = 2
} BC_DecodeContext;

typedef struct {
  uint32_t offset;
  int16_t relative;
  uint8_t opcode;
} BC_JumpRef;

typedef struct {
  const uint8_t *base;
  const uint8_t *end;
  const char *label;
  BC_VerifyOptions options;
  BC_VerifyResult result;
  uint8_t local_count;
  bool validate_local_indices;
  bool *top_level_instruction_starts;
  uint32_t top_level_instruction_start_capacity;
  BC_JumpRef *jumps;
  uint32_t jump_count;
  uint32_t jump_capacity;
} BC_Decoder;

BC_VerifyOptions bc_verify_default_options(void) {
  BC_VerifyOptions options;
  options.mode = BC_VERIFY_MODE_RUNTIME;
  options.strict_trailing_bytes = true;
  return options;
}

BC_VerifyOptions bc_verify_disassembly_options(void) {
  BC_VerifyOptions options;
  options.mode = BC_VERIFY_MODE_DISASSEMBLY;
  options.strict_trailing_bytes = false;
  return options;
}

const char *bc_verify_status_name(BC_VerifyStatus status) {
  switch (status) {
    case BC_VERIFY_OK: return "ok";
    case BC_VERIFY_WARNING: return "warning";
    case BC_VERIFY_ERROR: return "error";
  }
  return "unknown";
}

static uint32_t bc_offset(const BC_Decoder *d, const uint8_t *p) {
  if (!d->base || !p) return 0;
  return (uint32_t)(p - d->base);
}

static int bc_fail(BC_Decoder *d, const uint8_t *p, uint8_t opcode,
                   const char *reason) {
  d->result.status = BC_VERIFY_ERROR;
  d->result.diagnostic.offset = bc_offset(d, p);
  d->result.diagnostic.opcode = opcode;
  snprintf(d->result.diagnostic.message, sizeof(d->result.diagnostic.message),
           "%s: byte %05u opcode 0x%02X (%c): %s",
           d->label ? d->label : "bytecode", d->result.diagnostic.offset,
           opcode, (opcode >= 32 && opcode <= 126) ? opcode : '.', reason);
  return 0;
}

static void bc_warn(BC_Decoder *d, const uint8_t *p, uint8_t opcode,
                    const char *reason) {
  if (d->result.status == BC_VERIFY_OK) d->result.status = BC_VERIFY_WARNING;
  d->result.warning_count++;
  d->result.diagnostic.offset = bc_offset(d, p);
  d->result.diagnostic.opcode = opcode;
  snprintf(d->result.diagnostic.message, sizeof(d->result.diagnostic.message),
           "%s: byte %05u opcode 0x%02X (%c): %s",
           d->label ? d->label : "bytecode", d->result.diagnostic.offset,
           opcode, (opcode >= 32 && opcode <= 126) ? opcode : '.', reason);
}

static int bc_need(BC_Decoder *d, const uint8_t *p, size_t n,
                   uint8_t opcode, const char *what) {
  if (p > d->end || (size_t)(d->end - p) < n) {
    char msg[96];
    snprintf(msg, sizeof(msg), "truncated %s (need %zu byte%s, have %zu)",
             what, n, n == 1 ? "" : "s", p <= d->end ? (size_t)(d->end - p) : 0);
    return bc_fail(d, p, opcode, msg);
  }
  return 1;
}

static const IR_OpSchema *bc_schema_for(uint8_t opcode, BC_DecodeContext ctx) {
  const IR_OpSchema *fallback = NULL;
  for (size_t i = 0; i < g_ir_opcode_schema_count; i++) {
    const IR_OpSchema *s = &g_ir_opcode_schema[i];
    if (s->encoded_symbol != opcode || opcode == 0) continue;
    if (opcode == 'F') {
      if (ctx == BC_CTX_STMT && s->op == IR_OP_CALL) return s;
      if (ctx != BC_CTX_STMT && s->op == IR_OP_ITEM_DEREF) return s;
    }
    if (!fallback) fallback = s;
  }
  return fallback;
}

static int bc_decode_one(BC_Decoder *d, const uint8_t **cursor,
                         BC_DecodeContext ctx);

static int bc_validate_local_index(BC_Decoder *d, const uint8_t *p,
                                   uint8_t opcode, uint8_t index) {
  if (!d->validate_local_indices || index < d->local_count) return 1;
  char msg[96];
  snprintf(msg, sizeof(msg),
           "local index %u out of range for local count %u",
           (unsigned)index, (unsigned)d->local_count);
  return bc_fail(d, p, opcode, msg);
}

static int bc_record_jump(BC_Decoder *d, const uint8_t *operand_start,
                          uint8_t opcode) {
  if (d->jump_count == d->jump_capacity) {
    uint32_t new_capacity = d->jump_capacity == 0 ? 8 : d->jump_capacity * 2;
    BC_JumpRef *new_jumps = realloc(d->jumps, new_capacity * sizeof(*new_jumps));
    if (!new_jumps) return bc_fail(d, operand_start, opcode, "out of memory recording jump target");
    d->jumps = new_jumps;
    d->jump_capacity = new_capacity;
  }
  uint16_t raw = (uint16_t)operand_start[0] | ((uint16_t)operand_start[1] << 8);
  d->jumps[d->jump_count++] = (BC_JumpRef){
      .offset = bc_offset(d, operand_start),
      .relative = (int16_t)raw,
      .opcode = opcode,
  };
  return 1;
}

static int bc_validate_recorded_jumps(BC_Decoder *d) {
  uint32_t bytecode_len = (uint32_t)(d->end - d->base);
  for (uint32_t i = 0; i < d->jump_count; i++) {
    const BC_JumpRef *jump = &d->jumps[i];
    int64_t target = (int64_t)jump->offset + (int64_t)jump->relative;
    const uint8_t *jump_p = d->base + jump->offset - 1;
    if (target < 2) {
      return bc_fail(d, jump_p, jump->opcode, "jump target before bytecode body");
    }
    if (target >= (int64_t)bytecode_len) {
      return bc_fail(d, jump_p, jump->opcode, "jump target past bytecode body");
    }
    if ((uint32_t)target == d->result.halt_offset) continue;
    if ((uint32_t)target >= d->top_level_instruction_start_capacity ||
        !d->top_level_instruction_starts[target]) {
      return bc_fail(d, jump_p, jump->opcode,
                     "jump target is not a top-level instruction boundary");
    }
  }
  return 1;
}

static int bc_decode_item(BC_Decoder *d, const uint8_t **cursor) {
  while (*cursor < d->end) {
    uint8_t op = **cursor;
    if (op == 'E') return bc_decode_one(d, cursor, BC_CTX_ITEM);
    if (op == 'L') {
      if (!bc_decode_one(d, cursor, BC_CTX_ITEM)) return 0;
      continue;
    }
    if (op == 'D') {
      if (!bc_decode_one(d, cursor, BC_CTX_ITEM)) return 0;
      if (!bc_decode_one(d, cursor, BC_CTX_DEREF)) return 0;
      continue;
    }
    return bc_fail(d, *cursor, op, "unknown item-layer opcode");
  }
  return bc_fail(d, *cursor, 0, "unterminated item stream (missing ITEM_END)");
}

static int bc_decode_deref(BC_Decoder *d, const uint8_t **cursor) {
  const uint8_t *start = *cursor;
  if (!bc_need(d, start, 1, 0, "dereference type")) return 0;
  uint8_t type = *(*cursor)++;
  if (type == 'V') {
    if (!bc_need(d, *cursor, 1, type, "dereference local index")) return 0;
    const uint8_t *index_p = *cursor;
    uint8_t index = *(*cursor)++;
    return bc_validate_local_index(d, index_p, type, index);
  }
  if (type == 'I' || type == 'R') return bc_decode_item(d, cursor);
  return bc_fail(d, start, type, "unknown dereference type");
}

static int bc_decode_one(BC_Decoder *d, const uint8_t **cursor,
                         BC_DecodeContext ctx) {
  const uint8_t *start = *cursor;
  if (!bc_need(d, start, 1, 0, "opcode")) return 0;
  uint8_t op = *(*cursor)++;
  const IR_OpSchema *schema = bc_schema_for(op, ctx);
  if (!schema) return bc_fail(d, start, op, "unknown opcode");
  d->result.instruction_count++;

  switch (schema->op) {
    case IR_OP_HALT:
    case IR_OP_ADD: case IR_OP_SUB: case IR_OP_MUL: case IR_OP_DIV: case IR_OP_NEG:
    case IR_OP_EQ: case IR_OP_NEQ: case IR_OP_LT: case IR_OP_GT: case IR_OP_LE: case IR_OP_GE:
    case IR_OP_NOT: case IR_OP_AND: case IR_OP_OR:
    case IR_OP_ITEM_DEREF: case IR_OP_ITEM_SAVE: case IR_OP_EXISTS: case IR_OP_DELETE:
    case IR_OP_NTHNAME: case IR_OP_ROOTNAME: case IR_OP_ITEM_END:
      return 1;
    case IR_OP_PUSH_BOOL:
    case IR_OP_LIBCALL_TOKEN:
      if (!bc_need(d, *cursor, 1, op, schema->name)) return 0;
      (*cursor)++;
      return 1;
    case IR_OP_LOAD_LOCAL: case IR_OP_STORE_LOCAL: case IR_OP_INC_LOCAL: case IR_OP_DEC_LOCAL: {
      if (!bc_need(d, *cursor, 1, op, schema->name)) return 0;
      const uint8_t *index_p = *cursor;
      uint8_t index = *(*cursor)++;
      return bc_validate_local_index(d, index_p, op, index);
    }
    case IR_OP_CALL:
      if (!bc_need(d, *cursor, 2, op, schema->name)) return 0;
      *cursor += 2;
      return 1;
    case IR_OP_JUMP: case IR_OP_JUMP_IF_FALSE: {
      if (!bc_need(d, *cursor, 2, op, schema->name)) return 0;
      const uint8_t *operand_start = *cursor;
      if (ctx == BC_CTX_STMT && !bc_record_jump(d, operand_start, op)) return 0;
      *cursor += 2;
      return 1;
    }
    case IR_OP_PUSH_INT:
    case IR_OP_PUSH_FLOAT:
      if (!bc_need(d, *cursor, 8, op, schema->name)) return 0;
      *cursor += 8;
      return 1;
    case IR_OP_PUSH_STRING:
    case IR_OP_ITEM_SAVE_CODE: {
      if (!bc_need(d, *cursor, 2, op, "length")) return 0;
      uint16_t len;
      memcpy(&len, *cursor, sizeof(len));
      *cursor += 2;
      if (!bc_need(d, *cursor, len, op, schema->name)) return 0;
      *cursor += len;
      return 1;
    }
    case IR_OP_ITEM_PUSH_LAYER: {
      if (!bc_need(d, *cursor, 1, op, "layer length")) return 0;
      uint8_t len = *(*cursor)++;
      if (!bc_need(d, *cursor, len, op, "layer string")) return 0;
      *cursor += len;
      return 1;
    }
    case IR_OP_ITEM_PUSH_DEREF:
      return bc_decode_deref(d, cursor);
    case IR_OP_ITEM_BEGIN:
    case IR_OP_ITEM_BEGIN_REL:
      return bc_decode_item(d, cursor);
    case IR_OP_LABEL:
      break;
  }
  return bc_fail(d, start, op, "unsupported opcode schema entry");
}

bool bc_decode_item_expression(const uint8_t *item_payload,
                               const uint8_t *bytecode_end,
                               BC_ItemExprKind kind,
                               const uint8_t **after_item,
                               BC_VerifyError *diagnostic) {
  BC_Decoder d;
  memset(&d, 0, sizeof(d));
  d.base = item_payload;
  d.end = bytecode_end;
  d.label = kind == BC_ITEM_EXPR_RELATIVE ? "relative item expression" : "item expression";
  d.options = bc_verify_default_options();
  d.result.status = BC_VERIFY_OK;
  d.result.halt_offset = UINT32_MAX;

  const uint8_t *cursor = item_payload;
  if (!bc_decode_item(&d, &cursor)) {
    if (diagnostic) *diagnostic = d.result.diagnostic;
    if (after_item) *after_item = cursor;
    return false;
  }
  if (after_item) *after_item = cursor;
  if (diagnostic) memset(diagnostic, 0, sizeof(*diagnostic));
  return true;
}

BC_VerifyResult bc_verify_bytecode(const uint8_t *bytecode,
                                   uint32_t bytecode_len,
                                   const char *source_label,
                                   const BC_VerifyOptions *options) {
  BC_Decoder d;
  memset(&d, 0, sizeof(d));
  d.base = bytecode;
  d.end = bytecode ? bytecode + bytecode_len : NULL;
  d.label = source_label;
  d.options = options ? *options : bc_verify_default_options();
  d.result.status = BC_VERIFY_OK;
  d.result.halt_offset = UINT32_MAX;

  if (!bytecode && bytecode_len > 0) {
    bc_fail(&d, NULL, 0, "null bytecode pointer");
    return d.result;
  }
  if (bytecode_len < 2) {
    bc_fail(&d, d.base, 0, "missing two-byte locals/params header");
    return d.result;
  }

  uint8_t locals = bytecode[0];
  uint8_t params = bytecode[1];
  d.local_count = locals;
  d.validate_local_indices = true;
  if (params > locals) {
    bc_fail(&d, bytecode + 1, params, "parameter count exceeds local count");
    return d.result;
  }

  d.top_level_instruction_starts = calloc((size_t)bytecode_len + 1, sizeof(bool));
  d.top_level_instruction_start_capacity = bytecode_len + 1;
  if (!d.top_level_instruction_starts) {
    bc_fail(&d, d.base, 0, "out of memory recording instruction starts");
    return d.result;
  }

  const uint8_t *cursor = bytecode + 2;
  while (cursor < d.end) {
    const uint8_t *start = cursor;
    d.top_level_instruction_starts[bc_offset(&d, start)] = true;
    if (!bc_decode_one(&d, &cursor, BC_CTX_STMT)) {
      free(d.top_level_instruction_starts);
      free(d.jumps);
      return d.result;
    }
    if (*start == 'h') {
      d.result.halt_offset = bc_offset(&d, start);
      if (!bc_validate_recorded_jumps(&d)) {
        free(d.top_level_instruction_starts);
        free(d.jumps);
        return d.result;
      }
      if (cursor < d.end) {
        if (d.options.strict_trailing_bytes || d.options.mode != BC_VERIFY_MODE_DISASSEMBLY) {
          bc_fail(&d, cursor, *cursor, "trailing bytes after HALT");
        } else {
          bc_warn(&d, cursor, *cursor, "trailing bytes after HALT");
        }
      }
      free(d.top_level_instruction_starts);
      free(d.jumps);
      return d.result;
    }
  }

  bc_fail(&d, d.end, 0, "missing terminating HALT opcode");
  free(d.top_level_instruction_starts);
  free(d.jumps);
  return d.result;
}
