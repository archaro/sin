// Shared sdiss bytecode disassembly helpers.
//
// Licensed under the MIT License - see LICENSE file for details.

#include "sdiss_core.h"

#include <math.h>
#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "floatconv.h"

typedef struct {
  BC_BytecodeMetadata *metadata;
  SDissResult *result;
  SDissOptions options;
  SDissWriteFn write_fn;
  void *write_ctx;
  int header_printed;
} SDissState;

static void write_bytes(SDissState *state, const char *data, size_t len) {
  if (state->write_fn) state->write_fn(state->write_ctx, data, len);
}

#if defined(__GNUC__) || defined(__clang__)
static void outln(SDissState *state, const char *fmt, ...)
    __attribute__((format(printf, 2, 3)));
#else
static void outln(SDissState *state, const char *fmt, ...);
#endif

static void outln(SDissState *state, const char *fmt, ...) {
  char buffer[512];
  va_list args;
  va_start(args, fmt);
  int len = vsnprintf(buffer, sizeof(buffer), fmt, args);
  va_end(args);
  if (len <= 0) return;
  if ((size_t)len < sizeof(buffer)) {
    write_bytes(state, buffer, (size_t)len);
    return;
  }
  write_bytes(state, buffer, sizeof(buffer) - 1);
}

static void print_raw(SDissState *state, const BC_Instruction *inst) {
  if (!state->options.raw) return;
  outln(state, " [raw:");
  for (uint32_t i = 0; i < inst->raw_len; i++) outln(state, " %02X", inst->raw[i]);
  outln(state, "]");
}

static void print_escaped_bytes(SDissState *state, const uint8_t *data, size_t len) {
  for (size_t i = 0; i < len; i++) {
    uint8_t c = data[i];
    if (c == '\n') outln(state, "\\n");
    else if (c == '\r') outln(state, "\\r");
    else if (c == '\t') outln(state, "\\t");
    else if (c == '\\') outln(state, "\\\\");
    else if (c >= 32 && c <= 126) outln(state, "%c", c);
    else outln(state, "\\x%02X", c);
  }
}

static void print_operand_line(SDissState *state, const BC_Instruction *inst) {
  switch (inst->schema->op) {
    case IR_OP_PUSH_INT: {
      outln(state, "INTEGER %" PRId64 "\n", inst->operand.value.i64);
      break;
    }
    case IR_OP_PUSH_FLOAT: {
      double value;
      uint64_t bits = inst->operand.value.u64;
      memcpy(&value, &bits, sizeof(value));
      const char *class_name = "finite";
      if (isnan(value)) class_name = "nan";
      else if (isinf(value)) class_name = signbit(value) ? "-inf" : "+inf";
      else if (value == 0.0) class_name = signbit(value) ? "-0.0" : "+0.0";
      char fbuffer[64];
      if (!sin_format_binary64_buf(value, fbuffer, sizeof(fbuffer))) {
        snprintf(fbuffer, sizeof(fbuffer), "<float-format-error>");
      }
      outln(state, "FLOAT %s (%s bits=0x%016llx)\n", fbuffer, class_name,
            (unsigned long long)bits);
      break;
    }
    case IR_OP_PUSH_BOOL:
      outln(state, "BOOLEAN %u\n", (unsigned int)(inst->operand.value.u8 ? 1 : 0));
      break;
    case IR_OP_PUSH_STRING:
      outln(state, "STRINGLIT: ");
      print_escaped_bytes(state, inst->operand.value.bytes.data, inst->operand.value.bytes.len);
      outln(state, "\n");
      break;
    case IR_OP_LOAD_LOCAL:
    case IR_OP_STORE_LOCAL:
    case IR_OP_INC_LOCAL:
    case IR_OP_DEC_LOCAL:
      outln(state, "%s %u\n", inst->mnemonic, (unsigned int)inst->operand.value.u8);
      break;
    case IR_OP_LIBCALL:
      outln(state, "LIBCALL %u,%u\n",
            (unsigned int)(inst->operand.value.u16 >> 8),
            (unsigned int)(inst->operand.value.u16 & 0xffu));
      break;
    case IR_OP_CALL:
      outln(state, "CALL ARGC %u\n", (unsigned int)inst->operand.value.u16);
      break;
    case IR_OP_JUMP:
    case IR_OP_JUMP_IF_FALSE: {
      int16_t off = inst->operand.value.i16;
      uint32_t abs_offset = (uint32_t)((int64_t)inst->operand.offset + 2 + off);
      outln(state, "%s rel=%d abs=%u\n", inst->mnemonic, off,
            (unsigned int)abs_offset);
      break;
    }
    case IR_OP_ITEM_PUSH_LAYER:
      outln(state, "LAYER: ");
      print_escaped_bytes(state, inst->operand.value.bytes.data, inst->operand.value.bytes.len);
      outln(state, "\n");
      break;
    case IR_OP_ITEM_PUSH_DEREF_LOCAL:
      outln(state, "LOCALVAR %u\n", (unsigned int)inst->operand.value.u8);
      break;
    case IR_OP_ITEM_SAVE_CODE:
      outln(state, "EMBEDDED CODE (%u bytes):\n", (unsigned int)inst->operand.value.bytes.len);
      print_escaped_bytes(state, inst->operand.value.bytes.data, inst->operand.value.bytes.len);
      outln(state, "\n");
      break;
    case IR_OP_BUILD_LIST:
      outln(state, "BUILD LIST COUNT %u\n", (unsigned int)inst->operand.value.u32);
      break;
    default:
      outln(state, "%s\n", inst->mnemonic);
      break;
  }
}

static void print_header_once(SDissState *state) {
  if (state->header_printed) return;
  state->header_printed = 1;
  if (state->options.no_header || !state->metadata) return;
  if (state->metadata->locals > 0) outln(state, "Local variables: %d\n", state->metadata->locals);
  else outln(state, "No local variables.\n");
  if (state->metadata->params > 0) outln(state, "(Of which, %d are parameters.)\n", state->metadata->params);
  else outln(state, "(No parameters.)\n");
}

static bool on_instruction(const BC_Instruction *inst, void *ctx) {
  SDissState *state = ctx;
  print_header_once(state);
  if (inst->context == BC_EVENT_CONTEXT_STMT && inst->schema->op != IR_OP_HALT) {
    state->result->instruction_count++;
  }
  outln(state, "Byte %05u: ", inst->offset);
  print_operand_line(state, inst);
  print_raw(state, inst);
  if (state->options.raw) outln(state, "\n");
  return true;
}

SDissResult sdiss_disassemble_bytes(const uint8_t *bytecode,
                                    uint32_t bytecode_len,
                                    const SDissOptions *options,
                                    SDissWriteFn write_fn,
                                    void *write_ctx) {
  SDissResult sdiss_result = {0};
  BC_VerifyOptions verify_options = bc_verify_disassembly_options();
  BC_BytecodeMetadata metadata = {0};
  SDissState state = {0};
  state.metadata = &metadata;
  state.result = &sdiss_result;
  if (options) state.options = *options;
  state.write_fn = write_fn;
  state.write_ctx = write_ctx;

  BC_VerifyResult result = bc_decode_bytecode_events(bytecode, bytecode_len,
                                                     "disassembly", &verify_options,
                                                     &metadata, on_instruction, &state);
  print_header_once(&state);
  sdiss_result.status = result.status;
  sdiss_result.diagnostic = result.diagnostic;
  if (result.status == BC_VERIFY_WARNING) sdiss_result.warning_count = (int)result.warning_count;
  outln(&state, "Summary: instructions=%d unknown=%d warnings=%d\n",
        sdiss_result.instruction_count, sdiss_result.unknown_opcode_count,
        sdiss_result.warning_count);
  return sdiss_result;
}
