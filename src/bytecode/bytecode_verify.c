#include "bytecode_verify.h"
#include "bytecode_wire.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "libcall.h"
#include "memory.h"
#include "stack.h"
#include "string_limits.h"
#include "runtime/list.h"

#define BC_CTX_STMT BC_CONTEXT_STATEMENT
#define BC_CTX_ITEM BC_CONTEXT_ITEM_EXPRESSION
#define BC_CTX_DEREF BC_CONTEXT_DEREFERENCE
#define BC_MAX_ASSIGNCODE_PARAMS 1024u
typedef BC_Context BC_DecodeContext;

typedef struct {
  uint32_t offset;
  int16_t relative;
  uint8_t opcode;
} BC_JumpRef;

typedef struct {
  uint32_t offset;
  uint32_t next_offset;
  uint32_t operand_u32;
  IR_Op op;
  uint8_t opcode;
} BC_InstructionMeta;

#define BC_ANALYSIS_MEMORY_BUDGET (16u * 1024u * 1024u)

typedef struct {
  const uint8_t *base;
  const uint8_t *end;
  const char *label;
  BC_VerifyOptions options;
  BC_VerifyResult result;
  uint8_t local_count;
  bool legacy;
  bool validate_local_indices;
  uint8_t *top_level_instruction_starts;
  uint32_t top_level_instruction_count;
  BC_InstructionMeta *instructions;
  uint32_t instruction_capacity;
  BC_JumpRef *jumps;
  uint32_t jump_count;
  uint32_t jump_capacity;
  BC_DecodeInstructionCallback callback;
  void *callback_ctx;
  uint32_t event_depth;
  uint32_t item_expression_depth;
  size_t analysis_bytes;
  bool recording_top_level;
} BC_Decoder;

static BC_VerifyOptions bc_verify_executable_options(void) {
  return (BC_VerifyOptions){
      .validate_local_indices = true,
      .validate_control_flow = true,
      .validate_stack_effects = true,
  };
}

BC_VerifyOptions bc_verify_disassembly_options(void) {
  return (BC_VerifyOptions){
      .validate_local_indices = true,
      .validate_control_flow = false,
      .validate_stack_effects = false,
  };
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

static bool bc_analysis_resize_fits(const BC_Decoder *d, size_t old_count,
                                    size_t new_count, size_t element_size,
                                    size_t *new_total) {
  size_t old_bytes = 0;
  size_t new_bytes = 0;
  if (alloc_mul_overflow(old_count, element_size, &old_bytes) ||
      alloc_mul_overflow(new_count, element_size, &new_bytes) ||
      old_bytes > d->analysis_bytes) {
    return false;
  }
  size_t other_bytes = d->analysis_bytes - old_bytes;
  if (new_bytes > BC_ANALYSIS_MEMORY_BUDGET - other_bytes) return false;
  *new_total = other_bytes + new_bytes;
  return true;
}

static bool bc_is_top_level_instruction_start(const BC_Decoder *d,
                                              uint32_t offset) {
  uint32_t bytecode_len = (uint32_t)(d->end - d->base);
  return offset < bytecode_len &&
         (d->top_level_instruction_starts[offset >> 3] &
          (uint8_t)(1u << (offset & 7u))) != 0;
}

static uint32_t bc_instruction_index_for_offset(const BC_Decoder *d,
                                                uint32_t offset) {
  uint32_t low = 0;
  uint32_t high = d->top_level_instruction_count;
  while (low < high) {
    uint32_t mid = low + (high - low) / 2u;
    uint32_t candidate = d->instructions[mid].offset;
    if (candidate < offset) {
      low = mid + 1u;
    } else {
      high = mid;
    }
  }
  return low < d->top_level_instruction_count &&
                 d->instructions[low].offset == offset
             ? low
             : UINT32_MAX;
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

static const char *bc_disassembly_mnemonic(IR_Op op) {
  switch (op) {
    case IR_OP_HALT: return "HALT";
    case IR_OP_RETURN: return "RETURN";
    case IR_OP_PUSH_NIL: return "NIL";
    case IR_OP_ADD: return "ADD";
    case IR_OP_SUB: return "SUBTRACT";
    case IR_OP_MUL: return "MULTIPLY";
    case IR_OP_DIV: return "DIVIDE";
    case IR_OP_MOD: return "MODULO";
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
    case IR_OP_DISCARD: return "DISCARD";
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
    case IR_OP_LIBCALL: return "LIBCALL";
    case IR_OP_ITEM_BEGIN: return "BEGIN ITEM ASSEMBLY";
    case IR_OP_ITEM_BEGIN_REL: return "BEGIN RELATIVE ITEM ASSEMBLY";
    case IR_OP_ITEM_PUSH_LAYER: return "LAYER";
    case IR_OP_ITEM_PUSH_DEREF: return "BEGIN DEREFERENCE LAYER";
    case IR_OP_ITEM_PUSH_DEREF_LOCAL: return "LOCALVAR";
    case IR_OP_ITEM_END: return "END ITEM LAYER ASSEMBLY";
    case IR_OP_ITEM_DEREF: return "ITEM DEREF";
    case IR_OP_ITEM_SAVE: return "SAVE ITEM";
    case IR_OP_ITEM_SAVE_CODE: return "EMBEDDED CODE";
    case IR_OP_BUILD_LIST: return "BUILD LIST";
    case IR_OP_MAKE_ITEMREF: return "MAKE ITEMREF";
    case IR_OP_LABEL: return "LABEL";
  }
  return "UNKNOWN";
}

static BC_OperandKind bc_operand_encoding_from_ir(const IR_OpSchema *s) {
  if (!s) return BC_OPERAND_NONE;
  switch (s->size_policy) {
    case SIZE_FIXED_0:
    case SIZE_FIXED_1: return BC_OPERAND_NONE;
    case SIZE_FIXED_2:
      return s->operand_kind == OPERAND_IMM_CSTR ? BC_OPERAND_CSTR_U8 : BC_OPERAND_U8;
    case SIZE_FIXED_3:
      return s->operand_kind == OPERAND_LABEL_ID ? BC_OPERAND_I16 : BC_OPERAND_U16;
    case SIZE_FIXED_5: return BC_OPERAND_U32;
    case SIZE_PUSH_INT: return BC_OPERAND_I64;
    case SIZE_PUSH_FLOAT: return BC_OPERAND_F64_BITS;
    case SIZE_PUSH_STRING: return BC_OPERAND_CSTR_U16;
    case SIZE_ITEM_PUSH_LAYER: return BC_OPERAND_CSTR_U8;
    case SIZE_ITEM_SAVE_CODE: return BC_OPERAND_EMBEDDED_SOURCE;
  }
  return BC_OPERAND_NONE;
}

static bool bc_valid_context(IR_Op op, BC_Context ctx) {
  switch (op) {
    case IR_OP_ITEM_PUSH_LAYER:
    case IR_OP_ITEM_PUSH_DEREF:
    case IR_OP_ITEM_PUSH_DEREF_LOCAL:
    case IR_OP_ITEM_END:
      return ctx == BC_CONTEXT_ITEM_EXPRESSION || ctx == BC_CONTEXT_DEREFERENCE;
    case IR_OP_ITEM_DEREF:
      return ctx == BC_CONTEXT_DEREFERENCE;
    case IR_OP_LABEL:
      return false;
    default:
      return ctx == BC_CONTEXT_STATEMENT;
  }
}

static BC_OpcodeSchema bc_make_schema(const IR_OpSchema *s) {
  BC_OpcodeSchema out;
  memset(&out, 0, sizeof(out));
  out.ir = s;
  out.opcode = s ? s->encoded_symbol : 0;
  out.mnemonic = s ? bc_disassembly_mnemonic(s->op) : "UNKNOWN";
  out.operand_encoding = bc_operand_encoding_from_ir(s);
  if (s) {
    out.valid_in_statement = bc_valid_context(s->op, BC_CONTEXT_STATEMENT);
    out.valid_in_item_expression = bc_valid_context(s->op, BC_CONTEXT_ITEM_EXPRESSION);
    out.valid_in_dereference = bc_valid_context(s->op, BC_CONTEXT_DEREFERENCE);
    out.stack_effect = (BC_StackEffect){s->stack_pops, s->stack_pushes,
                                        s->stack_policy != IR_STACK_FIXED};
    out.control_flow = s->control_class;
    out.terminates = out.control_flow == IR_CONTROL_TERMINATING;
    out.valid_top_level = out.valid_in_statement;
    out.item_assembly_only = out.valid_in_item_expression || out.valid_in_dereference;
  }
  return out;
}

const BC_OpcodeSchema *bc_opcode_for_ir(IR_Op op) {
  enum { BC_IR_OP_CACHE_COUNT = IR_OP_MAKE_ITEMREF + 1 };
  static BC_OpcodeSchema cache[BC_IR_OP_CACHE_COUNT];
  static bool initialized[BC_IR_OP_CACHE_COUNT];
  const IR_OpSchema *s = ir_opcode_schema(op);
  if (!s || op < 0 || (int)op >= BC_IR_OP_CACHE_COUNT) return NULL;
  if (!initialized[op]) {
    cache[op] = bc_make_schema(s);
    initialized[op] = true;
  }
  return &cache[op];
}

const BC_OpcodeSchema *bc_opcode_lookup(uint8_t opcode, BC_Context context) {
  const BC_OpcodeSchema *fallback = NULL;
  for (size_t i = 0; i < g_ir_opcode_schema_count; i++) {
    const IR_OpSchema *s = &g_ir_opcode_schema[i];
    if (s->encoded_symbol != opcode || opcode == 0) continue;
    const BC_OpcodeSchema *bc = bc_opcode_for_ir(s->op);
    if (!bc) continue;
    if (opcode == 'F') {
      if (context == BC_CONTEXT_STATEMENT && s->op == IR_OP_CALL) return bc;
      if (context == BC_CONTEXT_DEREFERENCE && s->op == IR_OP_ITEM_DEREF) return bc;
    }
    if (bc_valid_context(s->op, context)) return bc;
    if (!fallback) fallback = bc;
  }
  return fallback && bc_valid_context(fallback->ir->op, context) ? fallback : NULL;
}

const char *bc_opcode_mnemonic(const BC_OpcodeSchema *schema) {
  return schema ? schema->mnemonic : "UNKNOWN";
}

BC_StackEffect bc_opcode_stack_effect(const BC_OpcodeSchema *schema,
                                      uint32_t operand_u32) {
  BC_StackEffect effect = schema ? schema->stack_effect : (BC_StackEffect){0, 0, false};
  if (schema && schema->ir && schema->ir->stack_policy == IR_STACK_CALL) {
    effect.pops = (int)operand_u32 + 1;
    effect.operand_dependent = false;
  } else if (schema && schema->ir &&
             schema->ir->stack_policy == IR_STACK_LIBCALL) {
    uint8_t args = 0;
    if (libcall_pair_arg_count((uint8_t)(operand_u32 >> 8), (uint8_t)operand_u32, &args)) {
      effect.pops = args;
    }
    effect.operand_dependent = false;
  } else if (schema && schema->ir &&
             schema->ir->stack_policy == IR_STACK_BUILD_LIST) {
    effect.pops = (int)operand_u32;
    effect.operand_dependent = false;
  }
  return effect;
}

uint8_t bc_opcode_byte(IR_Op op) {
  const BC_OpcodeSchema *schema = bc_opcode_for_ir(op);
  return schema ? schema->opcode : 0;
}

BC_OperandKind bc_opcode_operand_encoding(IR_Op op) {
  const BC_OpcodeSchema *schema = bc_opcode_for_ir(op);
  return schema ? schema->operand_encoding : BC_OPERAND_NONE;
}

static int bc_decode_one(BC_Decoder *d, const uint8_t **cursor,
                         BC_DecodeContext ctx);

static int bc_emit_event(BC_Decoder *d, const uint8_t *start, const uint8_t *cursor,
                         uint8_t opcode, const IR_OpSchema *schema,
                         const BC_Operand *operand, BC_DecodeContext ctx) {
  if (!d->callback) return 1;
  BC_Instruction inst;
  memset(&inst, 0, sizeof(inst));
  inst.offset = bc_offset(d, start);
  inst.opcode = opcode;
  inst.mnemonic = bc_opcode_mnemonic(bc_opcode_for_ir(schema->op));
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
  if (!d->options.validate_control_flow) return 1;
  if (d->jump_count == d->jump_capacity) {
    size_t new_capacity = 0;
    size_t new_total = 0;
    if (!alloc_grow_capacity(d->jump_capacity, (size_t)d->jump_count + 1u,
                             &new_capacity) ||
        new_capacity > UINT32_MAX ||
        !bc_analysis_resize_fits(d, d->jump_capacity, new_capacity,
                                 sizeof *d->jumps, &new_total)) {
      return bc_fail(d, operand_start, opcode, "verification analysis memory budget exceeded");
    }
    if (!alloc_grow_array((void **)&d->jumps, new_capacity,
                          sizeof *d->jumps)) {
      return bc_fail(d, operand_start, opcode, "out of memory recording jump target");
    }
    d->jump_capacity = (uint32_t)new_capacity;
    d->analysis_bytes = new_total;
  }
  uint16_t raw = bc_wire_load_u16(operand_start);
  d->jumps[d->jump_count++] = (BC_JumpRef){
      .offset = bc_offset(d, operand_start),
      .relative = bc_wire_i16_from_bits(raw),
      .opcode = opcode,
  };
  return 1;
}


static bool bc_stack_effect(const BC_InstructionMeta *meta, int *pops,
                            int *pushes) {
  const BC_OpcodeSchema *schema = bc_opcode_for_ir(meta->op);
  if (!schema) return false;
  BC_StackEffect effect = bc_opcode_stack_effect(schema, meta->operand_u32);
  *pops = effect.pops;
  *pushes = effect.pushes;
  return true;
}

static int bc_enqueue_stack_depth(BC_Decoder *d, int *depths, uint32_t *work,
                                  uint32_t *work_count, uint32_t index,
                                  int depth, const uint8_t *from,
                                  uint8_t opcode) {
  if (index >= d->top_level_instruction_count) {
    return bc_fail(d, from, opcode,
                   "control-flow edge does not target an instruction boundary");
  }
  if (depths[index] == -1) {
    depths[index] = depth;
    work[(*work_count)++] = index;
    return 1;
  }
  /* HALT has no successor and does not consume the operand stack, so paths
   * may terminate with different residual depths without making later
   * execution ambiguous. */
  if (d->instructions[index].op == IR_OP_HALT) return 1;
  if (depths[index] != depth) {
    char msg[128];
    snprintf(msg, sizeof(msg), "conflicting stack depths at byte %u (%d vs %d)",
             d->instructions[index].offset, depths[index], depth);
    return bc_fail(d, d->base + d->instructions[index].offset,
                   d->instructions[index].opcode, msg);
  }
  return 1;
}

static int bc_verify_stack_flow(BC_Decoder *d, uint8_t params) {
  uint32_t cap = d->top_level_instruction_count;
  size_t depth_bytes = 0, work_bytes = 0, flow_bytes = 0;
  if (alloc_mul_overflow(cap, sizeof(int), &depth_bytes) ||
      alloc_mul_overflow(cap, sizeof(uint32_t), &work_bytes) ||
      alloc_add_overflow(depth_bytes, work_bytes, &flow_bytes) ||
      flow_bytes > BC_ANALYSIS_MEMORY_BUDGET - d->analysis_bytes) {
    return bc_fail(d, d->base, 0, "verification analysis memory budget exceeded");
  }
  if (cap == 0) return 1;
  int *depths = alloc_malloc(depth_bytes);
  uint32_t *work = alloc_malloc(work_bytes);
  if (!depths || !work) {
    free(depths);
    free(work);
    return bc_fail(d, d->base, 0, "out of memory verifying stack flow");
  }
  for (uint32_t i = 0; i < cap; i++) depths[i] = -1;
  uint32_t work_count = 0;
  depths[0] = params;
  work[work_count++] = 0;

  while (work_count > 0) {
    uint32_t index = work[--work_count];
    uint32_t offset = d->instructions[index].offset;
    BC_InstructionMeta *meta = &d->instructions[index];
    int pops, pushes;
    if (!bc_stack_effect(meta, &pops, &pushes)) {
      free(depths);
      free(work);
      return bc_fail(d, d->base + offset, meta->opcode, "missing stack-effect metadata");
    }
    int in_depth = depths[index];
    if (in_depth < pops) {
      char msg[96];
      snprintf(msg, sizeof(msg), "stack underflow (depth %d, needs %d)", in_depth, pops);
      free(depths);
      free(work);
      return bc_fail(d, d->base + offset, meta->opcode, msg);
    }
    int out_depth = in_depth - pops + pushes;
    int reserved_local_slots = (int)d->local_count - (int)params;
    if (out_depth > STACK_SIZE - reserved_local_slots) {
      char msg[128];
      snprintf(msg, sizeof(msg),
               "stack depth %d plus %d reserved local slots exceeds VM capacity %d",
               out_depth, reserved_local_slots, STACK_SIZE);
      free(depths);
      free(work);
      return bc_fail(d, d->base + offset, meta->opcode, msg);
    }
    const BC_OpcodeSchema *schema = bc_opcode_for_ir(meta->op);
    if (schema && meta->op == IR_OP_RETURN) {
      if (in_depth != params + 1) {
        free(depths);
        free(work);
        return bc_fail(d, d->base + offset, meta->opcode,
                       "RETURN requires exactly one value above parameter baseline");
      }
      continue;
    }
    if (schema && schema->control_flow == IR_CONTROL_TERMINATING) continue;
    if (schema && (schema->control_flow == IR_CONTROL_JUMP ||
                   schema->control_flow == IR_CONTROL_CONDITIONAL)) {
      int16_t rel = bc_wire_i16_from_bits((uint16_t)meta->operand_u32);
      uint32_t operand_offset = offset + 1;
      uint32_t target = (uint32_t)((int64_t)operand_offset + rel);
      uint32_t target_index = bc_instruction_index_for_offset(d, target);
      if (!bc_enqueue_stack_depth(d, depths, work, &work_count, target_index,
                                  out_depth, d->base + offset, meta->opcode)) {
        free(depths);
        free(work);
        return 0;
      }
      if (schema->control_flow == IR_CONTROL_JUMP) continue;
    }
    if (meta->next_offset < (uint32_t)(d->end - d->base)) {
      if (!bc_enqueue_stack_depth(d, depths, work, &work_count, index + 1u,
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
    if (!bc_is_top_level_instruction_start(d, (uint32_t)target)) {
      return bc_fail(d, jump_p, jump->opcode,
                     "jump target is not a top-level instruction boundary");
    }
  }
  return 1;
}

static int bc_decode_item_contents(BC_Decoder *d, const uint8_t **cursor) {
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

static int bc_decode_item(BC_Decoder *d, const uint8_t **cursor) {
  if (d->item_expression_depth >= BC_MAX_ITEM_EXPRESSION_DEPTH) {
    uint8_t opcode = *cursor < d->end ? **cursor : 0;
    char msg[96];
    snprintf(msg, sizeof(msg),
             "item-expression nesting exceeds maximum depth %u",
             BC_MAX_ITEM_EXPRESSION_DEPTH);
    return bc_fail(d, *cursor, opcode, msg);
  }
  d->item_expression_depth++;
  int ok = bc_decode_item_contents(d, cursor);
  d->item_expression_depth--;
  return ok;
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
    const BC_OpcodeSchema *bc_schema = bc_opcode_lookup(type, BC_CTX_DEREF);
    if (bc_schema && bc_schema->ir) {
      BC_Operand operand;
      memset(&operand, 0, sizeof(operand));
      operand.kind = BC_OPERAND_U8; operand.offset = bc_offset(d, index_p); operand.width = 1; operand.value.u8 = index;
      if (!bc_emit_event(d, start, *cursor, type, bc_schema->ir, &operand, BC_CTX_DEREF)) return 0;
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

static void bc_release_analysis_storage(BC_Decoder *d) {
  free(d->top_level_instruction_starts);
  free(d->instructions);
  if (d->options.validate_control_flow) free(d->jumps);
}

static void bc_record_instruction_meta(BC_Decoder *d, const uint8_t *start,
                                       const uint8_t *cursor, uint8_t opcode,
                                       IR_Op op, uint32_t operand_u32) {
  if (!d->options.validate_stack_effects || !d->recording_top_level ||
      d->event_depth != 0) {
    return;
  }
  uint32_t offset = bc_offset(d, start);
  if (d->top_level_instruction_count >= d->instruction_capacity) {
    size_t cap = 0;
    size_t new_total = 0;
    if (!alloc_grow_capacity(d->instruction_capacity,
                             (size_t)d->top_level_instruction_count + 1u,
                             &cap) ||
        cap > UINT32_MAX ||
        !bc_analysis_resize_fits(d, d->instruction_capacity, cap,
                                 sizeof *d->instructions, &new_total)) {
      bc_fail(d, start, opcode, "verification analysis memory budget exceeded");
      return;
    }
    if (!alloc_grow_array((void **)&d->instructions, cap,
                          sizeof *d->instructions)) {
      bc_fail(d, start, opcode,
              "out of memory recording instruction metadata");
      return;
    }
    d->instruction_capacity = (uint32_t)cap;
    d->analysis_bytes = new_total;
  }
  d->instructions[d->top_level_instruction_count++] = (BC_InstructionMeta){
      .offset = offset,
      .next_offset = bc_offset(d, cursor),
      .operand_u32 = operand_u32,
      .op = op,
      .opcode = opcode,
  };
}

static int bc_decode_one(BC_Decoder *d, const uint8_t **cursor,
                         BC_DecodeContext ctx) {
  const uint8_t *start = *cursor;
  BC_Operand operand;
  memset(&operand, 0, sizeof(operand));
  operand.kind = BC_OPERAND_NONE;
  uint32_t operand_u32 = 0;
  if (!bc_need(d, start, 1, 0, "opcode")) return 0;
  uint8_t op = *(*cursor)++;
  const BC_OpcodeSchema *bc_schema = bc_opcode_lookup(op, ctx);
  if (!bc_schema || !bc_schema->ir) {
    return bc_fail(d, start, op,
                   "invalid opcode; recompile from Sinistra source");
  }
  const IR_OpSchema *schema = bc_schema->ir;
  d->result.instruction_count++;

  switch (schema->op) {
    case IR_OP_HALT:
    case IR_OP_RETURN:
    case IR_OP_PUSH_NIL:
    case IR_OP_ADD: case IR_OP_SUB: case IR_OP_MUL: case IR_OP_DIV: case IR_OP_MOD: case IR_OP_NEG:
    case IR_OP_EQ: case IR_OP_NEQ: case IR_OP_LT: case IR_OP_GT: case IR_OP_LE: case IR_OP_GE:
    case IR_OP_NOT: case IR_OP_AND: case IR_OP_OR:
    case IR_OP_DISCARD:
    case IR_OP_ITEM_DEREF: case IR_OP_ITEM_SAVE: case IR_OP_ITEM_END:
    case IR_OP_MAKE_ITEMREF:
      bc_record_instruction_meta(d, start, *cursor, op, schema->op, operand_u32);
      return bc_emit_event(d, start, *cursor, op, schema, &operand, ctx);
    case IR_OP_PUSH_BOOL:
    case IR_OP_LIBCALL:
      if (!bc_need(d, *cursor, 1, op, schema->name)) return 0;
      operand.kind = BC_OPERAND_U8; operand.offset = bc_offset(d, *cursor); operand.width = 1; operand.value.u8 = **cursor;
      if (schema->op == IR_OP_LIBCALL) {
        operand_u32 = (uint32_t)**cursor << 8;
        (*cursor)++;
        if (!bc_need(d, *cursor, 1, op, schema->name)) return 0;
        operand_u32 |= **cursor;
        operand.kind = BC_OPERAND_U16;
        operand.width = 2;
        operand.value.u16 = (uint16_t)operand_u32;
        if (!libcall_pair_arg_count((uint8_t)(operand_u32 >> 8),
                                    (uint8_t)operand_u32, NULL)) {
          char detail[64];
          snprintf(detail, sizeof(detail), "unknown libcall pair (%u,%u)",
                   (unsigned)(operand_u32 >> 8), (unsigned)(operand_u32 & 0xffu));
          return bc_fail(d, start, op, detail);
        }
      }
      (*cursor)++;
      bc_record_instruction_meta(d, start, *cursor, op, schema->op, operand_u32);
      return bc_emit_event(d, start, *cursor, op, schema, &operand, ctx);
    case IR_OP_LOAD_LOCAL: case IR_OP_STORE_LOCAL: case IR_OP_INC_LOCAL:
    case IR_OP_DEC_LOCAL: case IR_OP_ITEM_PUSH_DEREF_LOCAL: {
      if (!bc_need(d, *cursor, 1, op, schema->name)) return 0;
      const uint8_t *index_p = *cursor;
      uint8_t index = **cursor;
      operand.kind = BC_OPERAND_U8; operand.offset = bc_offset(d, *cursor); operand.width = 1; operand.value.u8 = index;
      (*cursor)++;
      if (!bc_validate_local_index(d, index_p, op, index)) return 0;
      bc_record_instruction_meta(d, start, *cursor, op, schema->op, operand_u32);
      return bc_emit_event(d, start, *cursor, op, schema, &operand, ctx);
    }
    case IR_OP_CALL:
      if (!bc_need(d, *cursor, 2, op, schema->name)) return 0;
      operand_u32 = bc_wire_load_u16(*cursor);
      operand.kind = BC_OPERAND_U16; operand.offset = bc_offset(d, *cursor); operand.width = 2; operand.value.u16 = (uint16_t)operand_u32;
      *cursor += 2;
      break;
    case IR_OP_JUMP: case IR_OP_JUMP_IF_FALSE: {
      if (!bc_need(d, *cursor, 2, op, schema->name)) return 0;
      const uint8_t *operand_start = *cursor;
      operand.value.i16 = bc_wire_load_i16(*cursor);
      operand_u32 = bc_wire_load_u16(*cursor);
      operand.kind = BC_OPERAND_I16;
      operand.offset = bc_offset(d, *cursor);
      operand.width = 2;
      if (ctx == BC_CTX_STMT && !bc_record_jump(d, operand_start, op)) return 0;
      *cursor += 2;
      break;
    }
    case IR_OP_PUSH_INT:
    case IR_OP_PUSH_FLOAT:
      if (!bc_need(d, *cursor, 8, op, schema->name)) return 0;
      operand.offset = bc_offset(d, *cursor); operand.width = 8;
      if (schema->op == IR_OP_PUSH_INT) operand.value.i64 = bc_wire_load_i64(*cursor);
      else operand.value.u64 = bc_wire_load_u64(*cursor);
      operand.kind = schema->op == IR_OP_PUSH_INT ? BC_OPERAND_I64 : BC_OPERAND_F64_BITS;
      *cursor += 8;
      bc_record_instruction_meta(d, start, *cursor, op, schema->op, operand_u32);
      return bc_emit_event(d, start, *cursor, op, schema, &operand, ctx);
    case IR_OP_PUSH_STRING: {
      if (!bc_need(d, *cursor, 2, op, "length")) return 0;
      uint16_t len = bc_wire_load_u16(*cursor);
      const uint8_t *data_start = *cursor + 2;
      *cursor += 2;
      if (!bc_need(d, *cursor, len, op, schema->name)) return 0;
      operand.kind = BC_OPERAND_CSTR_U16;
      operand.offset = bc_offset(d, data_start); operand.width = len; operand.value.bytes.data = data_start; operand.value.bytes.len = len;
      *cursor += len;
      bc_record_instruction_meta(d, start, *cursor, op, schema->op, operand_u32);
      return bc_emit_event(d, start, *cursor, op, schema, &operand, ctx);
    }
    case IR_OP_ITEM_SAVE_CODE: {
      if (*cursor >= d->end || **cursor != 'P') {
        if (!d->legacy) {
          return bc_fail(d, *cursor, op,
                         "embedded code is missing canonical parameter marker");
        }
      } else {
        uint32_t param_count = 0;
        uint32_t total_param_len = 0;
        (*cursor)++;
        while (1) {
          if (!bc_need(d, *cursor, 2, op,
                       "embedded parameter length")) return 0;
          uint16_t param_len = bc_wire_load_u16(*cursor);
          *cursor += 2;
          if (param_len == 0) break;
          if (param_count >= BC_MAX_ASSIGNCODE_PARAMS) {
            return bc_fail(d, *cursor, op,
                           "embedded parameter count exceeds maximum 1024");
          }
          if (total_param_len + param_len > SIN_MAX_STRING_BYTES) {
            return bc_fail(d, *cursor, op,
                           "embedded parameter bytes exceed maximum string size");
          }
          if (!bc_need(d, *cursor, param_len, op,
                       "embedded parameter name")) return 0;
          *cursor += param_len;
          param_count++;
          total_param_len += param_len;
        }
      }
      if (!bc_need(d, *cursor, 2, op, "embedded source length")) return 0;
      uint16_t len = bc_wire_load_u16(*cursor);
      *cursor += 2;
      const uint8_t *data_start = *cursor;
      if (!bc_need(d, *cursor, len, op, "embedded source")) return 0;
      operand.kind = BC_OPERAND_EMBEDDED_SOURCE;
      operand.offset = bc_offset(d, data_start);
      operand.width = len;
      operand.value.bytes.data = data_start;
      operand.value.bytes.len = len;
      *cursor += len;
      bc_record_instruction_meta(d, start, *cursor, op, schema->op,
                                 operand_u32);
      return bc_emit_event(d, start, *cursor, op, schema, &operand, ctx);
    }
    case IR_OP_ITEM_PUSH_LAYER: {
      if (!bc_need(d, *cursor, 1, op, "layer length")) return 0;
      uint8_t len = **cursor;
      (*cursor)++;
      if (!bc_need(d, *cursor, len, op, "layer string")) return 0;
      operand.kind = BC_OPERAND_CSTR_U8; operand.offset = bc_offset(d, *cursor); operand.width = len; operand.value.bytes.data = *cursor; operand.value.bytes.len = len;
      *cursor += len;
      bc_record_instruction_meta(d, start, *cursor, op, schema->op, operand_u32);
      return bc_emit_event(d, start, *cursor, op, schema, &operand, ctx);
    }
    case IR_OP_BUILD_LIST: {
      if (!bc_need(d, *cursor, 4, op, schema->name)) return 0;
      uint32_t count = bc_wire_load_u32(*cursor);
      if (count > SIN_LIST_MAX_ELEMENTS) return bc_fail(d, start, op, "list count exceeds maximum");
      operand.kind = BC_OPERAND_U32; operand.offset = bc_offset(d, *cursor); operand.width = 4; operand.value.u32 = count; operand_u32 = count; *cursor += 4;
      bc_record_instruction_meta(d, start, *cursor, op, schema->op, operand_u32);
      return bc_emit_event(d, start, *cursor, op, schema, &operand, ctx);
    }
    case IR_OP_ITEM_PUSH_DEREF:
      d->event_depth++;
      if (!bc_decode_deref(d, cursor)) { d->event_depth--; return 0; }
      d->event_depth--;
      bc_record_instruction_meta(d, start, *cursor, op, schema->op, operand_u32);
      return bc_emit_event(d, start, *cursor, op, schema, &operand, ctx);
    case IR_OP_ITEM_BEGIN:
    case IR_OP_ITEM_BEGIN_REL:
      d->event_depth++;
      if (!bc_decode_item(d, cursor)) { d->event_depth--; return 0; }
      d->event_depth--;
      bc_record_instruction_meta(d, start, *cursor, op, schema->op, operand_u32);
      return bc_emit_event(d, start, *cursor, op, schema, &operand, ctx);
    case IR_OP_LABEL:
      break;
  }
  bc_record_instruction_meta(d, start, *cursor, op, schema->op, operand_u32);
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
  d.options = bc_verify_executable_options();
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
  d.options = options ? *options : bc_verify_executable_options();
  d.callback = callback;
  d.callback_ctx = callback_ctx;
  d.result.status = BC_VERIFY_OK;
  d.result.halt_offset = UINT32_MAX;

  if (!bytecode && bytecode_len > 0) {
    bc_fail(&d, NULL, 0, "null bytecode pointer");
    return d.result;
  }
  BC_FormatHeader header;
  BC_FormatStatus format_status = bc_decode_header(bytecode, bytecode_len, &header);
  if (format_status != BC_FORMAT_OK) {
    const char *reason = format_status == BC_FORMAT_TRUNCATED ? "truncated bytecode header" :
      format_status == BC_FORMAT_UNSUPPORTED_VERSION ? "unsupported bytecode version" :
      "invalid bytecode header";
    bc_fail(&d, d.base, 0, reason);
    return d.result;
  }
  uint8_t locals = header.locals;
  uint8_t params = header.params;
  if (metadata) {
    metadata->locals = locals; metadata->params = params;
    metadata->version = header.version; metadata->instruction_offset = header.instruction_offset;
    metadata->instructions = header.instructions; metadata->legacy = header.legacy;
  }
  d.local_count = locals;
  d.legacy = header.legacy;
  d.validate_local_indices = d.options.validate_local_indices;
  if (params > locals) {
    bc_fail(&d, bytecode + 1, params, "parameter count exceeds local count");
    return d.result;
  }

  bool needs_instruction_starts = d.options.validate_control_flow ||
                                  d.options.validate_stack_effects;
  if (needs_instruction_starts) {
    size_t start_bytes = ((size_t)bytecode_len + 7u) / 8u;
    if (start_bytes > BC_ANALYSIS_MEMORY_BUDGET) {
      bc_fail(&d, d.base, 0, "verification analysis memory budget exceeded");
      return d.result;
    }
    d.top_level_instruction_starts = alloc_calloc(start_bytes, 1);
    if (d.top_level_instruction_starts) d.analysis_bytes = start_bytes;
  }
  if (needs_instruction_starts && !d.top_level_instruction_starts) {
    bc_fail(&d, d.base, 0, "out of memory recording instruction starts");
    bc_release_analysis_storage(&d);
    return d.result;
  }

  const uint8_t *cursor = header.instructions;
  uint8_t last_opcode = 0;
  uint32_t last_start = 0;
  while (cursor < d.end) {
    const uint8_t *start = cursor;
    if (needs_instruction_starts) {
      uint32_t offset = bc_offset(&d, start);
      d.top_level_instruction_starts[offset >> 3] |=
          (uint8_t)(1u << (offset & 7u));
    }
    d.recording_top_level = true;
    if (!bc_decode_one(&d, &cursor, BC_CTX_STMT)) {
      bc_release_analysis_storage(&d);
      return d.result;
    }
    d.recording_top_level = false;
    if (d.result.status == BC_VERIFY_ERROR) {
      bc_release_analysis_storage(&d);
      return d.result;
    }
    last_opcode = *start;
    last_start = bc_offset(&d, start);
    if (*start == 'h') {
      d.result.halt_offset = bc_offset(&d, start);
    }
  }

  if (d.result.status != BC_VERIFY_ERROR && cursor > header.instructions && last_opcode != 'h') {
    bc_fail(&d, d.base + last_start, last_opcode, "final physical instruction must be HALT");
  }
  if (d.result.status != BC_VERIFY_ERROR && d.options.validate_control_flow &&
      !bc_validate_recorded_jumps(&d)) {
    bc_release_analysis_storage(&d);
    return d.result;
  }
  if (d.result.status != BC_VERIFY_ERROR && d.options.validate_stack_effects &&
      !bc_verify_stack_flow(&d, params)) {
    bc_release_analysis_storage(&d);
    return d.result;
  }

  if (d.result.status == BC_VERIFY_OK && d.result.halt_offset == UINT32_MAX)
    bc_fail(&d, d.end, 0, "missing terminating HALT opcode");
  bc_release_analysis_storage(&d);
  return d.result;
}

BC_VerifyResult bc_verify_bytecode(const uint8_t *bytecode,
                                   uint32_t bytecode_len,
                                   const char *source_label,
                                   const BC_VerifyOptions *options) {
  return bc_decode_bytecode_events(bytecode, bytecode_len, source_label, options,
                                   NULL, NULL, NULL);
}

BC_VerifyResult bc_verify_executable_bytecode(const uint8_t *bytecode,
                                              uint32_t bytecode_len,
                                              const char *source_label) {
  BC_VerifyOptions options = bc_verify_executable_options();
  return bc_decode_bytecode_events(bytecode, bytecode_len, source_label,
                                   &options, NULL, NULL, NULL);
}
