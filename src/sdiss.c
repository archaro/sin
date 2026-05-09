// sdiss - Sinistra bytecode disassembler

// Licensed under the MIT License - see LICENSE file for details.

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdarg.h>
#include <getopt.h>

#include "config.h"
#include "memory.h"
#include "log.h"
#include "value.h"
#include "item.h"
#include "stack.h"

// Things which need to be known
CONFIG_t config;

uint8_t *process_item(uint8_t *opcodeptr, uint8_t *end);
uint8_t *process_dereference(uint8_t *opcodeptr, uint8_t *end);
uint8_t *decode_opcode(uint8_t *opcodeptr, uint8_t *end, int context);

// This is used by a few functions to calculate the current opcode location
uint8_t *bytecode;

static int decode_failed = 0;
static int unknown_opcode_count = 0;
static int warning_count = 0;
static int instruction_count = 0;
static int opt_raw = 0;
static int opt_no_header = 0;
static int opt_quiet = 0;

static void outln(const char *fmt, ...) {
  if (opt_quiet) return;
  va_list args;
  va_start(args, fmt);
  vprintf(fmt, args);
  va_end(args);
}

static void print_raw(uint8_t *start, uint8_t *end) {
  if (!opt_raw || opt_quiet ) return;
  outln(" [raw:");
  for (uint8_t *p = start; p < end; p++) outln(" %02X", *p);
  outln("]");
}

static void print_escaped_bytes(uint8_t *data, size_t len) {
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

typedef enum {
  DECODE_STMT = 0,
  DECODE_ITEM = 1,
  DECODE_DEREF = 2
} decode_context_t;

void usage() {
  logmsg("Sinistra disassembler.\nSyntax: sdiss <options>\n");
  logmsg("Options:\n");
  logmsg(" -h, --help\t\tThis message.\n");
  logmsg(" -o, --object <file>\tObject code to disassemble.\n");
  logmsg("     --raw\t\tShow raw bytes per instruction.\n");
  logmsg("     --no-header\tSkip locals/params header output.\n");
  logmsg(" -q, --quiet\t\tSuppress disassembly lines.\n");
  logmsg(" -v, --verbose\t\tEnable verbose output (default).\n");
}

static int require_bytes(uint8_t *opcodeptr, uint8_t *end, size_t count,
                         const char *what) {
  if ((size_t)(end - opcodeptr) < count) {
    logerr("Decode error at byte %05u: insufficient bytes for %s "
           "(need %zu, have %zu)\n",
           (unsigned int)(opcodeptr - bytecode), what, count,
           (size_t)(end - opcodeptr));
    decode_failed = 1;
    return 0;
  }
  return 1;
}

int main(int argc, char **argv) {
  FILE *in;
  int filesize = 0;
  uint8_t *opcodeptr;
  bytecode = NULL;

  if (argc < 2) {
    usage();
    exit(EXIT_FAILURE);
  }

  // Are there any interesting options?
  int opt;
  const struct option options[] =
  {
    {"help", no_argument, 0, 'h'},
    {"object", required_argument, 0, 'o'},
    {"raw", no_argument, 0, 1000},
    {"no-header", no_argument, 0, 1001},
    {"quiet", no_argument, 0, 'q'},
    {"verbose", no_argument, 0, 'v'},
    {NULL, 0, 0, '\0'}
  };
  while ((opt = getopt_long(argc, argv, "ho:qv", options, NULL)) != -1) {
    switch(opt) {
      case 'h':
        usage();
        exit(EXIT_SUCCESS);
        break;
      case 'o':
        // Mandatory: Name of the object code file.
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
      case 'q': opt_quiet = 1; break;
      case 'v': opt_quiet = 0; break;
      case 1000: opt_raw = 1; break;
      case 1001: opt_no_header = 1; break;
      default:
        usage();
        return EXIT_FAILURE;
    }
  }

  // Just check to see if we have been given some bytecode.
  if (!bytecode) {
    logerr("No bytecode to process!\n");
    exit(EXIT_FAILURE);
  }

  // We have some bytecode.  Step through it and output some helpful
  // disassembly.  Hopefully helpful, anyway.
  logmsg("Beginning disassembly...\n");
  opcodeptr = bytecode;

  // First, do we have any locals?
  uint8_t *end = bytecode + filesize;
  if (!require_bytes(opcodeptr, end, 2, "locals/params header")) {
    FREE_ARRAY(unsigned char, bytecode, filesize);
    exit(EXIT_FAILURE);
  }
  uint8_t locals = *opcodeptr++;
  uint8_t params = *opcodeptr++;
  if (!opt_no_header && locals > 0) {
    logmsg("Local variables: %d\n", locals);
  } else if (!opt_no_header) {
    logmsg("No local variables.\n");
  }
  if (!opt_no_header && params > 0) {
    logmsg("(Of which, %d are parameters.)\n", locals);
  } else if (!opt_no_header) {
    logmsg("(No parameters.)\n");
  }

  // Locals processed, so now step through the bytecode until the HALT
  // instruction is found.  This is just one big switch.
  while (opcodeptr < end && *opcodeptr != 'h') {
    outln("Byte %05u: ", opcodeptr - bytecode - 1);
    opcodeptr = decode_opcode(opcodeptr, end, DECODE_STMT);
    if (decode_failed) {
      break;
    }
  }
  if (decode_failed) {
    logerr("Disassembly aborted due to malformed bytecode.\n");
  } else if (opcodeptr >= end) {
    logerr("Decode error: unterminated stream (missing HALT 'h' before EOF).\n");
  } else {
    outln("Byte %05u: HALT\n", opcodeptr - bytecode - 1);
  }
  outln("Summary: instructions=%d unknown=%d warnings=%d\n",
        instruction_count, unknown_opcode_count, warning_count);

  // Clean up
  logmsg("Shutting down.\n");
  FREE_ARRAY(unsigned char, bytecode, filesize);
  exit(EXIT_SUCCESS);
}

uint8_t *process_item(uint8_t *opcodeptr, uint8_t *end) {
  // Recursive sub-processor to handle items.  Called whenever an I opcode
  // is encountered.  Returns when an E opcode is encountered.
  while (opcodeptr < end && *opcodeptr != 'E') {
    if (!require_bytes(opcodeptr, end, 1, "item opcode")) return opcodeptr;
    switch (*opcodeptr++) {
      case 'L':
        // Standard layer
        logmsg("Byte %05u: ", opcodeptr - bytecode - 2);
        if (!require_bytes(opcodeptr, end, 1, "layer length")) return opcodeptr;
        uint8_t len = *opcodeptr++;
        if (!require_bytes(opcodeptr, end, len, "layer string")) return opcodeptr;
        char layer[256];
        if (len >= sizeof(layer)) {
          logerr("Decode error at byte %05u: layer name too long (%u)\n",
                 (unsigned int)(opcodeptr - bytecode), len);
          decode_failed = 1;
          return opcodeptr;
        }
        memcpy(layer, opcodeptr, len);
        opcodeptr += len;
        layer[len] = '\0';
        logmsg("LAYER: %s\n", layer);
        break;
      case 'D':
        // Dereference!
        logmsg("Byte %05u: ", opcodeptr - bytecode - 2);
        logmsg("BEGIN DEREFERENCE LAYER\n");
        opcodeptr = process_dereference(opcodeptr, end);
        break;
      case 'F':
        logmsg("Byte %05u: ", opcodeptr - bytecode - 2);
        opcodeptr = decode_opcode(opcodeptr - 1, end, DECODE_ITEM);
        break;
      default:
        logmsg("Unknown opcode in item assembly: 0x%02X (%c)\n",
               *(opcodeptr - 1), (*(opcodeptr - 1) >= 32 && *(opcodeptr - 1) <= 126) ? *(opcodeptr - 1) : '.');
    }
  }
  if (opcodeptr >= end) {
    logerr("Decode error: unterminated item stream (missing 'E' before EOF).\n");
    decode_failed = 1;
    return opcodeptr;
  }
  // End of the item
  logmsg("Byte %05u: ", opcodeptr - bytecode - 1);
  logmsg("END ITEM LAYER ASSEMBLY\n");
  return ++opcodeptr;
}

uint8_t *process_dereference(uint8_t *opcodeptr, uint8_t *end) {
  // Process a dereference layer.  This can recurse through process_item().
  // This can either be a local variable or an item.
  if (!require_bytes(opcodeptr, end, 1, "dereference type")) return opcodeptr;
  uint8_t layertype = *opcodeptr++;
  if (layertype == 'V') {
    logmsg("Byte %05u: ", opcodeptr - bytecode - 2);
    if (!require_bytes(opcodeptr, end, 1, "local variable index")) return opcodeptr;
    uint8_t localvar = *opcodeptr++;
    logmsg("LOCALVAR %d\n", localvar);
  } else if (layertype == 'I') {
    logmsg("Byte %05u: ", opcodeptr - bytecode - 2);
    logmsg("BEGIN ITEM ASSEMBLY\n");
    opcodeptr = process_item(opcodeptr, end);
  } else {
    logmsg("Byte %05u: ", opcodeptr - bytecode - 2);
    logmsg ("Unknown dereference type: %c (%d)\n",  layertype, layertype);
  }
  return opcodeptr;
}

uint8_t *decode_opcode(uint8_t *opcodeptr, uint8_t *end, int context) {
  int16_t offset;
  int64_t ival;
  if (!require_bytes(opcodeptr, end, 1, "opcode")) return opcodeptr;
  uint8_t *inst_start = opcodeptr;
  uint8_t op = *opcodeptr++;
  instruction_count++;
  switch (op) {
    case 'a': logmsg("ADD\n"); break;
    case 'c':
      if (!require_bytes(opcodeptr, end, 1, "SAVE LOCAL operand")) return opcodeptr;
      logmsg("SAVE LOCAL %d\n", *opcodeptr++);
      break;
    case 'd': logmsg("DIVIDE\n"); break;
    case 'e':
      if (!require_bytes(opcodeptr, end, 1, "RETRIEVE LOCAL operand")) return opcodeptr;
      logmsg("RETRIEVE LOCAL %d\n", *opcodeptr++);
      break;
    case 'f':
      if (!require_bytes(opcodeptr, end, 1, "INCREMENT LOCAL operand")) return opcodeptr;
      logmsg("INCREMENT LOCAL %d\n", *opcodeptr++);
      break;
    case 'g':
      if (!require_bytes(opcodeptr, end, 1, "DECREMENT LOCAL operand")) return opcodeptr;
      logmsg("DECREMENT LOCAL %d\n", *opcodeptr++);
      break;
    case 'j':
      if (!require_bytes(opcodeptr, end, 2, "JUMP offset")) return opcodeptr;
      memcpy(&offset, opcodeptr, sizeof(offset));
      opcodeptr += 2;
      outln("JUMP rel=%d abs=%u\n", offset,
            (unsigned int)((opcodeptr - bytecode) + offset));
      break;
    case 'k':
      if (!require_bytes(opcodeptr, end, 2, "JUMP IF FALSE offset")) return opcodeptr;
      memcpy(&offset, opcodeptr, sizeof(offset));
      opcodeptr += 2;
      outln("JUMP IF FALSE rel=%d abs=%u\n", offset,
            (unsigned int)((opcodeptr - bytecode) + offset));
      break;
    case 'l':
      if (!require_bytes(opcodeptr, end, 2, "STRINGLIT length")) return opcodeptr;
      memcpy(&offset, opcodeptr, sizeof(offset));
      opcodeptr += 2;
      if (offset < 0 || !require_bytes(opcodeptr, end, (size_t)offset, "STRINGLIT data")) {
        return opcodeptr;
      }
      outln("STRINGLIT: ");
      print_escaped_bytes(opcodeptr, (size_t)offset);
      opcodeptr += offset;
      outln("\n");
      break;
    case 'm': logmsg("MULTIPLY\n"); break;
    case 'n': logmsg("NEGATE\n"); break;
    case 'o': logmsg("BOOL EQ\n"); break;
    case 'p':
      if (!require_bytes(opcodeptr, end, 8, "INTEGER literal")) return opcodeptr;
      memcpy(&ival, opcodeptr, sizeof(ival));
      logmsg("INTEGER %ld\n", ival);
      opcodeptr += 8;
      break;
    case 'q': logmsg("BOOL NOTEQ\n"); break;
    case 'r': logmsg("BOOL LT\n"); break;
    case 's': logmsg("SUBTRACT\n"); break;
    case 't': logmsg("BOOL GT\n"); break;
    case 'u': logmsg("BOOL LTEQ\n"); break;
    case 'v': logmsg("BOOL GTEQ\n"); break;
    case 'x': logmsg("LOGICAL NOT\n"); break;
    case 'y': logmsg("LOGICAL AND\n"); break;
    case 'z': logmsg("LOGICAL OR\n"); break;
    case 'A':
      if (!require_bytes(opcodeptr, end, 1, "LIBCALL id")) return opcodeptr;
      logmsg("LIBCALL ID %u\n", *opcodeptr++);
      break;
    case 'B': {
      uint16_t len;
      if (!require_bytes(opcodeptr, end, 2, "EMBEDDED CODE length")) return opcodeptr;
      memcpy(&len, opcodeptr, sizeof(len));
      opcodeptr += 2;
      if (!require_bytes(opcodeptr, end, len, "EMBEDDED CODE data")) return opcodeptr;
      logmsg("EMBEDDED CODE (%d bytes):\n", len);
      print_escaped_bytes(opcodeptr, len);
      opcodeptr += len;
      outln("\n");
      break;
    }
    case 'C': logmsg("SAVE ITEM\n"); break;
    case 'F':
      if (context == DECODE_ITEM || context == DECODE_DEREF) {
        logmsg("ITEM DEREF\n");
      } else {
        if (!require_bytes(opcodeptr, end, 1, "CALL arg count")) return opcodeptr;
        logmsg("CALL ARGC %u\n", *opcodeptr++);
      }
      break;
    case 'I':
      logmsg("BEGIN ITEM ASSEMBLY\n");
      opcodeptr = process_item(opcodeptr, end);
      break;
    case 'W': logmsg("DELETE ITEM\n"); break;
    case 'X': logmsg("ITEM EXISTS\n"); break;
    case 'Y': logmsg("NTHNAME\n"); break;
    case 'Z': logmsg("ROOTNAME\n"); break;
    default:
      unknown_opcode_count++;
      warning_count++;
      outln("UNKNOWN OPCODE 0x%02X (%c)\n", op,
             (op >= 32 && op <= 126) ? op : '.');
      break;
  }
  print_raw(inst_start, opcodeptr);
  if (opt_raw && !opt_quiet) outln("\n");
  return opcodeptr;
}
