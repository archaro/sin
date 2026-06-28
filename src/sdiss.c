// sdiss - a bytecode disassembler

// Licensed under the MIT License - see LICENSE file for details.

#include <getopt.h>
#include <math.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bytecode_verify.h"
#include "config.h"
#include "floatconv.h"
#include "log.h"
#include "memory.h"
#include "version.h"

CONFIG_t config;

static uint8_t *bytecode;
static int opt_raw = 0, opt_no_header = 0;

typedef struct {
  BC_BytecodeMetadata *metadata;
  int header_printed;
  int instruction_count;
  int unknown_opcode_count;
  int warning_count;
} SDissState;

static void outln(const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  vprintf(fmt, args);
  va_end(args);
}

static void print_raw(const BC_Instruction *inst) {
  if (!opt_raw) return;
  outln(" [raw:");
  for (uint32_t i = 0; i < inst->raw_len; i++) outln(" %02X", inst->raw[i]);
  outln("]");
}

static void print_escaped_bytes(const uint8_t *data, size_t len) {
  for (size_t i = 0; i < len; i++) {
    uint8_t c = data[i];
    if (c == '\n') outln("\\n");
    else if (c == '\r') outln("\\r");
    else if (c == '\t') outln("\\t");
    else if (c == '\\') outln("\\\\");
    else if (c >= 32 && c <= 126) outln("%c", c);
    else outln("\\x%02X", c);
  }
}

void usage() {
  logmsg("Sinistra disassembler version %s.\nSyntax: sdiss <options>\n", SINVERSION);
  logmsg("Options:\n");
  logmsg(" -h, --help\t\tThis message.\n");
  logmsg(" -o, --object <file>\tObject code to disassemble.\n");
  logmsg("     --raw\t\tShow raw bytes per instruction.\n");
  logmsg("     --no-header\tSkip locals/params header output.\n");
}

static void print_operand_line(const BC_Instruction *inst) {
  switch (inst->schema->op) {
    case IR_OP_PUSH_INT: {
      int64_t v;
      memcpy(&v, &inst->operand.value.u64, sizeof(v));
      outln("INTEGER %ld\n", (long)v);
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
      outln("FLOAT %s (%s bits=0x%016llx)\n", fbuffer, class_name,
            (unsigned long long)bits);
      break;
    }
    case IR_OP_PUSH_BOOL:
      outln("BOOLEAN %u\n", (unsigned int)(inst->operand.value.u8 ? 1 : 0));
      break;
    case IR_OP_PUSH_STRING:
      outln("STRINGLIT: ");
      print_escaped_bytes(inst->operand.value.bytes.data, inst->operand.value.bytes.len);
      outln("\n");
      break;
    case IR_OP_LOAD_LOCAL:
    case IR_OP_STORE_LOCAL:
    case IR_OP_INC_LOCAL:
    case IR_OP_DEC_LOCAL:
      outln("%s %u\n", inst->mnemonic, (unsigned int)inst->operand.value.u8);
      break;
    case IR_OP_LIBCALL_TOKEN:
      outln("LIBCALL_TOKEN %u\n", (unsigned int)inst->operand.value.u8);
      break;
    case IR_OP_CALL:
      outln("CALL ARGC %u\n", (unsigned int)inst->operand.value.u16);
      break;
    case IR_OP_JUMP:
    case IR_OP_JUMP_IF_FALSE: {
      int16_t off = inst->operand.value.i16;
      outln("%s rel=%d abs=%u\n", inst->mnemonic, off,
            (unsigned int)(inst->operand.offset + 2 + off));
      break;
    }
    case IR_OP_ITEM_PUSH_LAYER:
      outln("LAYER: ");
      print_escaped_bytes(inst->operand.value.bytes.data, inst->operand.value.bytes.len);
      outln("\n");
      break;
    case IR_OP_ITEM_PUSH_DEREF_LOCAL:
      outln("LOCALVAR %u\n", (unsigned int)inst->operand.value.u8);
      break;
    case IR_OP_ITEM_SAVE_CODE:
      outln("EMBEDDED CODE (%u bytes):\n", (unsigned int)inst->operand.value.bytes.len);
      print_escaped_bytes(inst->operand.value.bytes.data, inst->operand.value.bytes.len);
      outln("\n");
      break;
    default:
      outln("%s\n", inst->mnemonic);
      break;
  }
}

static void print_header_once(SDissState *state) {
  if (state->header_printed) return;
  state->header_printed = 1;
  if (opt_no_header || !state->metadata) return;
  if (state->metadata->locals > 0) logmsg("Local variables: %d\n", state->metadata->locals);
  else logmsg("No local variables.\n");
  if (state->metadata->params > 0) logmsg("(Of which, %d are parameters.)\n", state->metadata->params);
  else logmsg("(No parameters.)\n");
}

static bool on_instruction(const BC_Instruction *inst, void *ctx) {
  SDissState *state = ctx;
  print_header_once(state);
  if (inst->context == BC_EVENT_CONTEXT_STMT && inst->schema->op != IR_OP_HALT) {
    state->instruction_count++;
  }
  outln("Byte %05u: ", inst->offset == 0 ? 0 : inst->offset - 1);
  print_operand_line(inst);
  print_raw(inst);
  if (opt_raw) outln("\n");
  return true;
}

int main(int argc, char **argv) {
  FILE *in = NULL;
  int filesize = 0;
  bytecode = NULL;
  if (argc < 2) {
    usage();
    exit(EXIT_FAILURE);
  }
  int opt;
  const struct option options[] = {
    {"help", no_argument, 0, 'h'},
    {"object", required_argument, 0, 'o'},
    {"raw", no_argument, 0, 1000},
    {"no-header", no_argument, 0, 1001},
    {NULL, 0, 0, '\0'}
  };
  while ((opt = getopt_long(argc, argv, "ho:", options, NULL)) != -1) {
    switch (opt) {
      case 'h':
        usage();
        exit(EXIT_SUCCESS);
      case 'o':
        in = fopen(optarg, "rb");
        if (!in) {
          logerr("Unable to open input file: %s\n", optarg);
          exit(EXIT_FAILURE);
        }
        fseek(in, 0, SEEK_END);
        filesize = ftell(in);
        fseek(in, 0, SEEK_SET);
        bytecode = GROW_ARRAY(unsigned char, bytecode, 0, filesize);
        fread(bytecode, filesize, sizeof(char), in);
        fclose(in);
        logmsg("Bytecode loaded: %d bytes.\n", filesize);
        break;
      case 1000:
        opt_raw = 1;
        break;
      case 1001:
        opt_no_header = 1;
        break;
      default:
        usage();
        return EXIT_FAILURE;
    }
  }
  if (!bytecode) {
    logerr("No bytecode to process!\n");
    exit(EXIT_FAILURE);
  }
  logmsg("Beginning disassembly...\n");

  BC_VerifyOptions verify_options = bc_verify_disassembly_options();
  BC_BytecodeMetadata metadata = {0};
  SDissState state = {0};
  state.metadata = &metadata;
  BC_VerifyResult result = bc_decode_bytecode_events(bytecode, (uint32_t)filesize,
                                                     "disassembly", &verify_options,
                                                     &metadata, on_instruction, &state);
  print_header_once(&state);

  if (result.status == BC_VERIFY_ERROR) {
    logerr("%s\n", result.diagnostic.message);
    logerr("Disassembly aborted due to malformed bytecode.\n");
  } else if (result.status == BC_VERIFY_WARNING) {
    state.warning_count = (int)result.warning_count;
    logerr("%s\n", result.diagnostic.message);
  }

  outln("Summary: instructions=%d unknown=%d warnings=%d\n",
        state.instruction_count, state.unknown_opcode_count, state.warning_count);
  logmsg("Shutting down.\n");
  FREE_ARRAY(unsigned char, bytecode, filesize);
  return result.status == BC_VERIFY_ERROR ? EXIT_FAILURE : EXIT_SUCCESS;
}
