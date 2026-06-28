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
  uint8_t opcode;
  IR_Op op;
  uint32_t next_offset;
  uint16_t operand_u16;
} BC_InstructionMeta;

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
  BC_InstructionMeta *instructions;
  BC_JumpRef *jumps;
  uint32_t jump_count;
  uint32_t jump_capacity;
  BC_DecodeInstructionCallback callback;
  void *callback_ctx;
  uint32_t event_depth;
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

BC_VerifyOptions bc_verify_compiler_options(void) {
  BC_VerifyOptions options;
  options.mode = BC_VERIFY_MODE_COMPILER;
  options.strict_trailing_bytes = true;
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

static const char *bc_disassembly_mnemonic(IR_Op op) {
  switch (op) {
    case IR_OP_HALT: return "HALT";
    case IR_OP_ADD: return "ADD";
    case IR_OP_SUB: return "SUBTRACT";
    case IR_OP_MUL: return "MULTIPLY";
    case IR_OP_DIV: return "DIVIDE";
    case IR_OP_NEG: return "NEGATE";
    case IR_OP_EQ: return "BOOL EQ";
    case IR_OP_NEQ: return "BOOL NOTEQ";
    case IR_OP_LT: return "BOOL LT";
    case IR_OP_GT: return "BOOL GT";
    case IR_OP_LE: return "BOOL LTEQ";
    case IR_OP_GE: return "BOOL GTEQ";
    case IR_OP_NOT: return "LOGICAL NOT";
    case IR_OP_AND: return "LOGICAL AND";
    case IR_OP_OR: return "LOGICAL OR";
    case IR_OP_LOAD_LOCAL: return "RETRIEVE LOCAL";
    case IR_OP_STORE_LOCAL: return "SAVE LOCAL";
    case IR_OP_INC_LOCAL: return "INCREMENT LOCAL";
    case IR_OP_DEC_LOCAL: return "DECREMENT LOCAL";
    case IR_OP_PUSH_INT: return "INTEGER";
    case IR_OP_PUSH_FLOAT: return "FLOAT";
    case IR_OP_PUSH_BOOL: return "BOOLEAN";
    case IR_OP_PUSH_STRING: return "STRINGLIT";
    case IR_OP_JUMP: return "JUMP";
    case IR_OP_JUMP_IF_FALSE: return "JUMP IF FALSE";
    case IR_OP_CALL: return "CALL";
    case IR_OP_LIBCALL_TOKEN: return "LIBCALL_TOKEN";
    case IR_OP_ITEM_BEGIN: return "BEGIN ITEM ASSEMBLY";
    case IR_OP_ITEM_BEGIN_REL: return "BEGIN RELATIVE ITEM ASSEMBLY";
    case IR_OP_ITEM_PUSH_LAYER: return "LAYER";
    case IR_OP_ITEM_PUSH_DEREF: return "BEGIN DEREFERENCE LAYER";
    case IR_OP_ITEM_PUSH_DEREF_LOCAL: return "LOCALVAR";
    case IR_OP_ITEM_END: return "END ITEM LAYER ASSEMBLY";
    case IR_OP_ITEM_DEREF: return "ITEM DEREF";
    case IR_OP_ITEM_SAVE: return "SAVE ITEM";
    case IR_OP_ITEM_SAVE_CODE: return "EMBEDDED CODE";
    case IR_OP_EXISTS: return "ITEM EXISTS";
    case IR_OP_DELETE: return "DELETE ITEM";
    case IR_OP_NTHNAME: return "NTHNAME";
    case IR_OP_ROOTNAME: return "ROOTNAME";
    case IR_OP_LABEL: return "LABEL";
  }
  return "UNKNOWN";
}

static int bc_emit_event(BC_Decoder *d, const uint8_t *start, const uint8_t *cursor,
                         uint8_t opcode, const IR_OpSchema *schema,
                         const BC_Operand *operand, BC_DecodeContext ctx) {
  if (!d->callback) return 1;
  BC_Instruction inst;
  memset(&inst, 0, sizeof(inst));
  inst.offset = bc_offset(d, start);
  inst.opcode = opcode;
  inst.mnemonic = bc_disassembly_mnemonic(schema->op);
  inst.schema = schema;
  if (operand) inst.operand = *operand;
  inst.raw = start;
  inst.raw_len = (uint32_t)(cursor - start);
  inst.context = ctx == BC_CTX_STMT ? BC_EVENT_CONTEXT_STMT : (ctx == BC_CTX_ITEM ? BC_EVENT_CONTEXT_ITEM : BC_EVENT_CONTEXT_DEREF);
  inst.depth = d->event_depth;
  if (!d->callback(&inst, d->callback_ctx)) {
    return bc_fail(d, start, opcode, "decoder callback aborted");
  }
  return 1;
}

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


#define BC_MAX_ABSTRACT_STACK_DEPTH 1024

static bool bc_stack_effect(const BC_InstructionMeta *meta, int *pops,
                            int *pushes) {
  *pops = 0;
  *pushes = 0;
  switch (meta->op) {
    case IR_OP_PUSH_INT: case IR_OP_PUSH_FLOAT: case IR_OP_PUSH_BOOL:
    case IR_OP_PUSH_STRING: case IR_OP_LOAD_LOCAL: case IR_OP_ITEM_BEGIN:
    case IR_OP_ITEM_BEGIN_REL: case IR_OP_LIBCALL_TOKEN:
      *pushes = 1;
      return true;
    case IR_OP_ADD: case IR_OP_SUB: case IR_OP_MUL: case IR_OP_DIV:
    case IR_OP_EQ: case IR_OP_NEQ: case IR_OP_LT: case IR_OP_GT:
    case IR_OP_LE: case IR_OP_GE: case IR_OP_AND: case IR_OP_OR:
    case IR_OP_NTHNAME:
      *pops = 2;
      *pushes = 1;
      return true;
    case IR_OP_NEG: case IR_OP_NOT: case IR_OP_EXISTS: case IR_OP_ROOTNAME:
    case IR_OP_ITEM_DEREF:
      *pops = 1;
      *pushes = 1;
      return true;
    case IR_OP_STORE_LOCAL: case IR_OP_JUMP_IF_FALSE: case IR_OP_DELETE:
    case IR_OP_ITEM_SAVE_CODE:
      *pops = 1;
      return true;
    case IR_OP_ITEM_SAVE:
      *pops = 2;
      return true;
    case IR_OP_CALL:
      *pops = (int)meta->operand_u16 + 1;
      *pushes = 1;
      return true;
    case IR_OP_HALT:
      return true;
    case IR_OP_INC_LOCAL: case IR_OP_DEC_LOCAL: case IR_OP_JUMP:
      return true;
    case IR_OP_LABEL: case IR_OP_ITEM_PUSH_LAYER: case IR_OP_ITEM_PUSH_DEREF:
    case IR_OP_ITEM_PUSH_DEREF_LOCAL:
    case IR_OP_ITEM_END:
      return true;
  }
  return false;
}

static int bc_enqueue_stack_depth(BC_Decoder *d, int *depths, uint32_t *work,
                                  uint32_t *work_count, uint32_t offset,
                                  int depth, const uint8_t *from,
                                  uint8_t opcode) {
  if (offset >= d->top_level_instruction_start_capacity ||
      !d->top_level_instruction_starts[offset]) {
    return bc_fail(d, from, opcode,
                   "control-flow edge does not target an instruction boundary");
  }
  if (depths[offset] == -1) {
    depths[offset] = depth;
    work[(*work_count)++] = offset;
    return 1;
  }
  /* HALT has no successor and does not consume the operand stack, so paths
   * may terminate with different residual depths without making later
   * execution ambiguous. */
  if (d->instructions[offset].op == IR_OP_HALT) return 1;
  if (depths[offset] != depth) {
    char msg[128];
    snprintf(msg, sizeof(msg), "conflicting stack depths at byte %u (%d vs %d)",
             offset, depths[offset], depth);
    return bc_fail(d, d->base + offset, d->instructions[offset].opcode, msg);
  }
  return 1;
}

static int bc_verify_stack_flow(BC_Decoder *d, uint8_t params) {
  uint32_t cap = d->top_level_instruction_start_capacity;
  int *depths = malloc((size_t)cap * sizeof(*depths));
  uint32_t *work = malloc((size_t)cap * sizeof(*work));
  if (!depths || !work) {
    free(depths);
    free(work);
    return bc_fail(d, d->base, 0, "out of memory verifying stack flow");
  }
  for (uint32_t i = 0; i < cap; i++) depths[i] = -1;
  uint32_t work_count = 0;
  depths[2] = params;
  work[work_count++] = 2;

  while (work_count > 0) {
    uint32_t offset = work[--work_count];
    BC_InstructionMeta *meta = &d->instructions[offset];
    int pops, pushes;
    if (!bc_stack_effect(meta, &pops, &pushes)) {
      free(depths);
      free(work);
      return bc_fail(d, d->base + offset, meta->opcode, "missing stack-effect metadata");
    }
    int in_depth = depths[offset];
    if (in_depth < pops) {
      char msg[96];
      snprintf(msg, sizeof(msg), "stack underflow (depth %d, needs %d)", in_depth, pops);
      free(depths);
      free(work);
      return bc_fail(d, d->base + offset, meta->opcode, msg);
    }
    int out_depth = in_depth - pops + pushes;
    if (out_depth > BC_MAX_ABSTRACT_STACK_DEPTH) {
      free(depths);
      free(work);
      return bc_fail(d, d->base + offset, meta->opcode,
                     "abstract stack depth exceeds verifier limit");
    }
    if (meta->op == IR_OP_HALT) continue;
    if (meta->op == IR_OP_JUMP || meta->op == IR_OP_JUMP_IF_FALSE) {
      int16_t rel = (int16_t)meta->operand_u16;
      uint32_t operand_offset = offset + 1;
      uint32_t target = (uint32_t)((int64_t)operand_offset + rel);
      if (!bc_enqueue_stack_depth(d, depths, work, &work_count, target, out_depth,
                                  d->base + offset, meta->opcode)) {
        free(depths);
        free(work);
        return 0;
      }
      if (meta->op == IR_OP_JUMP) continue;
    }
    if (meta->next_offset < (uint32_t)(d->end - d->base)) {
      if (!bc_enqueue_stack_depth(d, depths, work, &work_count, meta->next_offset,
                                  out_depth, d->base + offset, meta->opcode)) {
        free(depths);
        free(work);
        return 0;
      }
    }
  }
  free(depths);
  free(work);
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
    if (!bc_validate_local_index(d, index_p, type, index)) return 0;
    const IR_OpSchema *schema = bc_schema_for(type, BC_CTX_DEREF);
    if (schema) {
      BC_Operand operand;
      memset(&operand, 0, sizeof(operand));
      operand.kind = BC_OPERAND_U8; operand.offset = bc_offset(d, index_p); operand.width = 1; operand.value.u8 = index;
      if (!bc_emit_event(d, start, *cursor, type, schema, &operand, BC_CTX_DEREF)) return 0;
    }
    return 1;
  }
  if (type == 'I' || type == 'R') {
    d->event_depth++;
    int ok = bc_decode_item(d, cursor);
    d->event_depth--;
    return ok;
  }
  return bc_fail(d, start, type, "unknown dereference type");
}

static void bc_record_instruction_meta(BC_Decoder *d, const uint8_t *start,
                                       const uint8_t *cursor, uint8_t opcode,
                                       IR_Op op, uint16_t operand_u16) {
  uint32_t offset = bc_offset(d, start);
  if (d->instructions && offset < d->top_level_instruction_start_capacity) {
    d->instructions[offset] = (BC_InstructionMeta){
        opcode, op, bc_offset(d, cursor), operand_u16};
  }
}

static int bc_decode_one(BC_Decoder *d, const uint8_t **cursor,
                         BC_DecodeContext ctx) {
  const uint8_t *start = *cursor;
  BC_Operand operand;
  memset(&operand, 0, sizeof(operand));
  operand.kind = BC_OPERAND_NONE;
  uint16_t operand_u16 = 0;
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
      bc_record_instruction_meta(d, start, *cursor, op, schema->op, operand_u16);
      return bc_emit_event(d, start, *cursor, op, schema, &operand, ctx);
    case IR_OP_PUSH_BOOL:
    case IR_OP_LIBCALL_TOKEN:
      if (!bc_need(d, *cursor, 1, op, schema->name)) return 0;
      operand.kind = BC_OPERAND_U8; operand.offset = bc_offset(d, *cursor); operand.width = 1; operand.value.u8 = **cursor;
      (*cursor)++;
      bc_record_instruction_meta(d, start, *cursor, op, schema->op, operand_u16);
      return bc_emit_event(d, start, *cursor, op, schema, &operand, ctx);
    case IR_OP_LOAD_LOCAL: case IR_OP_STORE_LOCAL: case IR_OP_INC_LOCAL:
    case IR_OP_DEC_LOCAL: case IR_OP_ITEM_PUSH_DEREF_LOCAL: {
      if (!bc_need(d, *cursor, 1, op, schema->name)) return 0;
      const uint8_t *index_p = *cursor;
      uint8_t index = **cursor;
      operand.kind = BC_OPERAND_U8; operand.offset = bc_offset(d, *cursor); operand.width = 1; operand.value.u8 = index;
      (*cursor)++;
      if (!bc_validate_local_index(d, index_p, op, index)) return 0;
      bc_record_instruction_meta(d, start, *cursor, op, schema->op, operand_u16);
      return bc_emit_event(d, start, *cursor, op, schema, &operand, ctx);
    }
    case IR_OP_CALL:
      if (!bc_need(d, *cursor, 2, op, schema->name)) return 0;
      operand_u16 = (uint16_t)(*cursor)[0] | ((uint16_t)(*cursor)[1] << 8);
      operand.kind = BC_OPERAND_U16; operand.offset = bc_offset(d, *cursor); operand.width = 2; operand.value.u16 = operand_u16;
      *cursor += 2;
      break;
    case IR_OP_JUMP: case IR_OP_JUMP_IF_FALSE: {
      if (!bc_need(d, *cursor, 2, op, schema->name)) return 0;
      const uint8_t *operand_start = *cursor;
      operand_u16 = (uint16_t)(*cursor)[0] | ((uint16_t)(*cursor)[1] << 8);
      operand.kind = BC_OPERAND_I16; operand.offset = bc_offset(d, *cursor); operand.width = 2; operand.value.i16 = (int16_t)operand_u16;
      if (ctx == BC_CTX_STMT && !bc_record_jump(d, operand_start, op)) return 0;
      *cursor += 2;
      break;
    }
    case IR_OP_PUSH_INT:
    case IR_OP_PUSH_FLOAT:
      if (!bc_need(d, *cursor, 8, op, schema->name)) return 0;
      operand.offset = bc_offset(d, *cursor); operand.width = 8;
      memcpy(&operand.value.u64, *cursor, 8);
      operand.kind = schema->op == IR_OP_PUSH_INT ? BC_OPERAND_I64 : BC_OPERAND_F64_BITS;
      *cursor += 8;
      bc_record_instruction_meta(d, start, *cursor, op, schema->op, operand_u16);
      return bc_emit_event(d, start, *cursor, op, schema, &operand, ctx);
    case IR_OP_PUSH_STRING:
    case IR_OP_ITEM_SAVE_CODE: {
      if (!bc_need(d, *cursor, 2, op, "length")) return 0;
      uint16_t len;
      memcpy(&len, *cursor, sizeof(len));
      const uint8_t *data_start = *cursor + 2;
      *cursor += 2;
      if (!bc_need(d, *cursor, len, op, schema->name)) return 0;
      operand.kind = schema->op == IR_OP_PUSH_STRING ? BC_OPERAND_CSTR_U16 : BC_OPERAND_EMBEDDED_SOURCE;
      operand.offset = bc_offset(d, data_start); operand.width = len; operand.value.bytes.data = data_start; operand.value.bytes.len = len;
      *cursor += len;
      bc_record_instruction_meta(d, start, *cursor, op, schema->op, operand_u16);
      return bc_emit_event(d, start, *cursor, op, schema, &operand, ctx);
    }
    case IR_OP_ITEM_PUSH_LAYER: {
      if (!bc_need(d, *cursor, 1, op, "layer length")) return 0;
      uint8_t len = **cursor;
      (*cursor)++;
      if (!bc_need(d, *cursor, len, op, "layer string")) return 0;
      operand.kind = BC_OPERAND_CSTR_U8; operand.offset = bc_offset(d, *cursor); operand.width = len; operand.value.bytes.data = *cursor; operand.value.bytes.len = len;
      *cursor += len;
      bc_record_instruction_meta(d, start, *cursor, op, schema->op, operand_u16);
      return bc_emit_event(d, start, *cursor, op, schema, &operand, ctx);
    }
    case IR_OP_ITEM_PUSH_DEREF:
      d->event_depth++;
      if (!bc_decode_deref(d, cursor)) { d->event_depth--; return 0; }
      d->event_depth--;
      bc_record_instruction_meta(d, start, *cursor, op, schema->op, operand_u16);
      return bc_emit_event(d, start, *cursor, op, schema, &operand, ctx);
    case IR_OP_ITEM_BEGIN:
    case IR_OP_ITEM_BEGIN_REL:
      d->event_depth++;
      if (!bc_decode_item(d, cursor)) { d->event_depth--; return 0; }
      d->event_depth--;
      bc_record_instruction_meta(d, start, *cursor, op, schema->op, operand_u16);
      return bc_emit_event(d, start, *cursor, op, schema, &operand, ctx);
    case IR_OP_LABEL:
      break;
  }
  bc_record_instruction_meta(d, start, *cursor, op, schema->op, operand_u16);
  return bc_emit_event(d, start, *cursor, op, schema, &operand, ctx);
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

BC_VerifyResult bc_decode_bytecode_events(const uint8_t *bytecode,
                                          uint32_t bytecode_len,
                                          const char *source_label,
                                          const BC_VerifyOptions *options,
                                          BC_BytecodeMetadata *metadata,
                                          BC_DecodeInstructionCallback callback,
                                          void *callback_ctx) {
  BC_Decoder d;
  memset(&d, 0, sizeof(d));
  d.base = bytecode;
  d.end = bytecode ? bytecode + bytecode_len : NULL;
  d.label = source_label;
  d.options = options ? *options : bc_verify_default_options();
  d.callback = callback;
  d.callback_ctx = callback_ctx;
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
  if (metadata) { metadata->locals = locals; metadata->params = params; }
  d.local_count = locals;
  d.validate_local_indices = true;
  if (params > locals) {
    bc_fail(&d, bytecode + 1, params, "parameter count exceeds local count");
    return d.result;
  }

  d.top_level_instruction_starts = calloc((size_t)bytecode_len + 1, sizeof(bool));
  d.instructions = calloc((size_t)bytecode_len + 1, sizeof(*d.instructions));
  d.top_level_instruction_start_capacity = bytecode_len + 1;
  if (!d.top_level_instruction_starts || !d.instructions) {
    bc_fail(&d, d.base, 0, "out of memory recording instruction starts");
    free(d.top_level_instruction_starts);
    free(d.instructions);
    return d.result;
  }

  const uint8_t *cursor = bytecode + 2;
  while (cursor < d.end) {
    const uint8_t *start = cursor;
    d.top_level_instruction_starts[bc_offset(&d, start)] = true;
    if (!bc_decode_one(&d, &cursor, BC_CTX_STMT)) {
      free(d.top_level_instruction_starts);
      free(d.instructions);
      free(d.jumps);
      return d.result;
    }
    if (*start == 'h') {
      d.result.halt_offset = bc_offset(&d, start);
      if (d.options.mode != BC_VERIFY_MODE_DISASSEMBLY) {
        if (!bc_validate_recorded_jumps(&d)) {
          free(d.top_level_instruction_starts);
          free(d.instructions);
          free(d.jumps);
          return d.result;
        }
        if (!bc_verify_stack_flow(&d, params)) {
          free(d.top_level_instruction_starts);
          free(d.instructions);
          free(d.jumps);
          return d.result;
        }
      }
      if (cursor < d.end) {
        if (d.options.strict_trailing_bytes || d.options.mode != BC_VERIFY_MODE_DISASSEMBLY) {
          bc_fail(&d, cursor, *cursor, "trailing bytes after HALT");
        } else {
          bc_warn(&d, cursor, *cursor, "trailing bytes after HALT");
        }
      }
      free(d.top_level_instruction_starts);
      free(d.instructions);
      free(d.jumps);
      return d.result;
    }
  }

  bc_fail(&d, d.end, 0, "missing terminating HALT opcode");
  free(d.top_level_instruction_starts);
  free(d.instructions);
  free(d.jumps);
  return d.result;
}

BC_VerifyResult bc_verify_bytecode(const uint8_t *bytecode,
                                   uint32_t bytecode_len,
                                   const char *source_label,
                                   const BC_VerifyOptions *options) {
  return bc_decode_bytecode_events(bytecode, bytecode_len, source_label, options,
                                   NULL, NULL, NULL);
}
