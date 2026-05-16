// sdiss - a bytecode disassembler

// Licensed under the MIT License - see LICENSE file for details.

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdarg.h>
#include <getopt.h>

#include "version.h"
#include "config.h"
#include "memory.h"
#include "log.h"
#include "value.h"
#include "item.h"
#include "stack.h"

CONFIG_t config;

uint8_t *bytecode;

typedef enum { DECODE_STMT = 0,
               DECODE_ITEM = 1,
               DECODE_DEREF = 2 } decode_context_t;
typedef enum { PARSE_OK = 0,
               PARSE_EOF = 1,
               PARSE_ERR = 2 } parse_status_t;
typedef enum { OPERAND_NONE = 0,
               OPERAND_U8,
               OPERAND_I16,
               OPERAND_I64,
               OPERAND_BLOB_I16 } operand_kind_t;

typedef struct {
  uint8_t opcode;
  const char *mnemonic;
  operand_kind_t operand;
  uint8_t *(*handler)(uint8_t *, uint8_t *, decode_context_t);
} opcode_desc_t;

static int decode_failed = 0, unknown_opcode_count = 0, warning_count =
  0, instruction_count = 0;
static int opt_raw = 0, opt_no_header = 0;

static void outln(const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  vprintf(fmt, args);
  va_end(args);
}
static void print_raw(uint8_t *start, uint8_t *end) {
  if (!opt_raw) return;
  outln(" [raw:");
  for (uint8_t * p = start; p < end; p++) outln(" %02X", *p);
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

void usage() {
  logmsg("Sinistra disassembler version %s.\nSyntax: sdiss <options>\n", SINVERSION);
  logmsg("Options:\n");
  logmsg(" -h, --help\t\tThis message.\n");
  logmsg(" -o, --object <file>\tObject code to disassemble.\n");
  logmsg("     --raw\t\tShow raw bytes per instruction.\n");
  logmsg("     --no-header\tSkip locals/params header output.\n");
}

static int require_bytes(uint8_t *p, uint8_t *end, size_t count,
                         const char *what) {
  if ((size_t) (end - p) < count) {
    logerr
      ("Decode error at byte %05u: insufficient bytes for %s (need %zu, have %zu)\n",
       (unsigned int) (p - bytecode), what, count, (size_t) (end - p));
    decode_failed = 1;
    return 0;
  }
  return 1;
}
static parse_status_t read_u8(uint8_t **p, uint8_t *end, uint8_t *out,
                              const char *what) {
  if (!require_bytes(*p, end, 1, what)) return PARSE_EOF;
  *out = *(*p)++;
  return PARSE_OK;
}
static parse_status_t read_i16(uint8_t **p, uint8_t *end, int16_t *out,
                               const char *what) {
  if (!require_bytes(*p, end, 2, what)) return PARSE_EOF;
  memcpy(out, *p, 2);
  *p += 2;
  return PARSE_OK;
}
static parse_status_t read_i64(uint8_t **p, uint8_t *end, int64_t *out,
                               const char *what) {
  if (!require_bytes(*p, end, 8, what)) return PARSE_EOF;
  memcpy(out, *p, 8);
  *p += 8;
  return PARSE_OK;
}
static parse_status_t read_blob(uint8_t **p, uint8_t *end, size_t len,
                                uint8_t **out, const char *what) {
  if (!require_bytes(*p, end, len, what)) return PARSE_EOF;
  *out = *p;
  *p += len;
  return PARSE_OK;
}

static parse_status_t process_item(uint8_t ** p, uint8_t * end);
static parse_status_t process_dereference(uint8_t ** p, uint8_t * end);
static uint8_t *decode_opcode(uint8_t * p, uint8_t * end,
                              decode_context_t context);

static parse_status_t process_item(uint8_t **p, uint8_t *end) {
  while (*p < end && **p != 'E') {
    uint8_t op;
    if (read_u8(p, end, &op, "item opcode") != PARSE_OK) return PARSE_EOF;
    switch (op) {
    case 'L':{
        logmsg("Byte %05u: ", (unsigned int) (*p - bytecode - 2));
        uint8_t len;
        if (read_u8(p, end, &len, "layer length") !=
            PARSE_OK) return PARSE_EOF;
        uint8_t *bytes;
        if (read_blob(p, end, len, &bytes, "layer string") !=
            PARSE_OK) return PARSE_EOF;
        char layer[256];
        if (len >= sizeof(layer)) {
          logerr("Decode error at byte %05u: layer name too long (%u)\n",
                 (unsigned int) (*p - bytecode), len);
          decode_failed = 1;
          return PARSE_ERR;
        }
        memcpy(layer, bytes, len);
        layer[len] = '\0';
        logmsg("LAYER: %s\n", layer);
        break;
      }
    case 'D':
      logmsg("Byte %05u: ", (unsigned int) (*p - bytecode - 2));
      logmsg("BEGIN DEREFERENCE LAYER\n");
      if (process_dereference(p, end) != PARSE_OK) return PARSE_ERR;
      break;
    case 'F':
      logmsg("Byte %05u: ", (unsigned int) (*p - bytecode - 2));
      *p = decode_opcode(*p - 1, end, DECODE_ITEM);
      if (decode_failed) return PARSE_ERR;
      break;
    default:
      logmsg("Unknown opcode in item assembly: 0x%02X (%c)\n", op,
             (op >= 32 && op <= 126) ? op : '.');
    }
  }
  if (*p >= end) {
    logerr
      ("Decode error: unterminated item stream (missing 'E' before EOF).\n");
    decode_failed = 1;
    return PARSE_EOF;
  }
  logmsg("Byte %05u: ", (unsigned int) (*p - bytecode - 1));
  logmsg("END ITEM LAYER ASSEMBLY\n");
  (*p)++;
  return PARSE_OK;
}
static parse_status_t process_dereference(uint8_t **p, uint8_t *end) {
  uint8_t t;
  if (read_u8(p, end, &t, "dereference type") != PARSE_OK) return PARSE_EOF;
  if (t == 'V') {
    logmsg("Byte %05u: ", (unsigned int) (*p - bytecode - 2));
    uint8_t lv;
    if (read_u8(p, end, &lv, "local variable index") !=
        PARSE_OK) return PARSE_EOF;
    logmsg("LOCALVAR %d\n", lv);
  } else if (t == 'I') {
    logmsg("Byte %05u: ", (unsigned int) (*p - bytecode - 2));
    logmsg("BEGIN ITEM ASSEMBLY\n");
    return process_item(p, end);
  } else {
    logmsg("Byte %05u: ", (unsigned int) (*p - bytecode - 2));
    logmsg("Unknown dereference type: %c (%d)\n", t, t);
  } return PARSE_OK;
}

#define SIMPLE_HANDLER(name, text) static uint8_t* name(uint8_t*p,uint8_t*e,decode_context_t c){(void)e;(void)c; logmsg(text"\n"); return p;}
SIMPLE_HANDLER(h_add, "ADD") SIMPLE_HANDLER(h_div,
                                            "DIVIDE") SIMPLE_HANDLER(h_mul,
                                                                     "MULTIPLY")
SIMPLE_HANDLER(h_neg, "NEGATE") SIMPLE_HANDLER(h_eq,
                                               "BOOL EQ")
SIMPLE_HANDLER(h_neq, "BOOL NOTEQ") SIMPLE_HANDLER(h_lt,
                                                   "BOOL LT")
SIMPLE_HANDLER(h_sub, "SUBTRACT") SIMPLE_HANDLER(h_gt,
                                                 "BOOL GT")
SIMPLE_HANDLER(h_lte, "BOOL LTEQ") SIMPLE_HANDLER(h_gte,
                                                  "BOOL GTEQ")
SIMPLE_HANDLER(h_not, "LOGICAL NOT") SIMPLE_HANDLER(h_and,
                                                    "LOGICAL AND")
SIMPLE_HANDLER(h_or, "LOGICAL OR") SIMPLE_HANDLER(h_save_item,
                                                  "SAVE ITEM")
SIMPLE_HANDLER(h_delete_item, "DELETE ITEM") SIMPLE_HANDLER(h_item_exists,
                                                            "ITEM EXISTS")
SIMPLE_HANDLER(h_nthname, "NTHNAME") SIMPLE_HANDLER(h_rootname, "ROOTNAME")
     static uint8_t *h_u8_local(const char *name, uint8_t *p, uint8_t *e) {
  uint8_t v;
  if (read_u8(&p, e, &v, name) != PARSE_OK) return p;
  logmsg("%s %d\n", name, v);
  return p;
     }
static uint8_t *h_store_local(uint8_t *p, uint8_t *e, decode_context_t c) {
  (void) c;
  return h_u8_local("SAVE LOCAL", p, e);
}
static uint8_t *h_load_local(uint8_t *p, uint8_t *e, decode_context_t c) {
  (void) c;
  return h_u8_local("RETRIEVE LOCAL", p, e);
}
static uint8_t *h_inc_local(uint8_t *p, uint8_t *e, decode_context_t c) {
  (void) c;
  return h_u8_local("INCREMENT LOCAL", p, e);
}
static uint8_t *h_dec_local(uint8_t *p, uint8_t *e, decode_context_t c) {
  (void) c;
  return h_u8_local("DECREMENT LOCAL", p, e);
}
static uint8_t *h_libcall(uint8_t *p, uint8_t *e, decode_context_t c) {
  (void) c;
  uint8_t argc;
  uint8_t id;
  if (read_u8(&p, e, &argc, "LIBCALL arg count") != PARSE_OK) return p;
  if (read_u8(&p, e, &id, "LIBCALL id") != PARSE_OK) return p;
  logmsg("LIBCALL ARGC %u ID %u\n", argc, id);
  return p;
}
static uint8_t *h_jump(uint8_t *p, uint8_t *e, decode_context_t c) {
  (void) c;
  int16_t off;
  if (read_i16(&p, e, &off, "JUMP offset") != PARSE_OK) return p;
  outln("JUMP rel=%d abs=%u\n", off, (unsigned int) ((p - bytecode) + off));
  return p;
}
static uint8_t *h_jif(uint8_t *p, uint8_t *e, decode_context_t c) {
  (void) c;
  int16_t off;
  if (read_i16(&p, e, &off, "JUMP IF FALSE offset") != PARSE_OK) return p;
  outln("JUMP IF FALSE rel=%d abs=%u\n", off,
        (unsigned int) ((p - bytecode) + off));
  return p;
}
static uint8_t *h_str(uint8_t *p, uint8_t *e, decode_context_t c) {
  (void) c;
  int16_t len;
  uint8_t *b;
  if (read_i16(&p, e, &len, "STRINGLIT length") != PARSE_OK) return p;
  if (len < 0
      || read_blob(&p, e, (size_t) len, &b,
                   "STRINGLIT data") != PARSE_OK) return p;
  outln("STRINGLIT: ");
  print_escaped_bytes(b, (size_t) len);
  outln("\n");
  return p;
}
static uint8_t *h_int(uint8_t *p, uint8_t *e, decode_context_t c) {
  (void) c;
  int64_t v;
  if (read_i64(&p, e, &v, "INTEGER literal") != PARSE_OK) return p;
  logmsg("INTEGER %ld\n", v);
  return p;
}
static uint8_t *h_emb(uint8_t *p, uint8_t *e, decode_context_t c) {
  (void) c;
  int16_t len;
  uint8_t *b;
  if (read_i16(&p, e, &len, "EMBEDDED CODE length") != PARSE_OK) return p;
  if (len < 0
      || read_blob(&p, e, (size_t) len, &b,
                   "EMBEDDED CODE data") != PARSE_OK) return p;
  logmsg("EMBEDDED CODE (%d bytes):\n", len);
  print_escaped_bytes(b, (size_t) len);
  outln("\n");
  return p;
}
static uint8_t *h_f(uint8_t *p, uint8_t *e, decode_context_t c) {
  if (c == DECODE_ITEM || c == DECODE_DEREF) {
    logmsg("ITEM DEREF\n");
    return p;
  }
  int16_t argc;
  if (read_i16(&p, e, &argc, "CALL arg count") != PARSE_OK) return p;
  logmsg("CALL ARGC %d\n", argc);
  return p;
}
static uint8_t *h_I(uint8_t *p, uint8_t *e, decode_context_t c) {
  (void) c;
  logmsg("BEGIN ITEM ASSEMBLY\n");
  process_item(&p, e);
  return p;
}

/* Synchronization points:
 * - Keep opcode values aligned with src/emitbc.c:map_opcode.
 * - Keep mnemonics/semantics aligned with src/interpret.c VM execution. */
static const opcode_desc_t OPCODES[] = {
  {'a', "ADD", OPERAND_NONE, h_add}, {'c', "SAVE LOCAL", OPERAND_U8,
                                      h_store_local}, {'d', "DIVIDE",
                                                       OPERAND_NONE, h_div},
    {'e', "RETRIEVE LOCAL", OPERAND_U8, h_load_local}, {'f',
                                                        "INCREMENT LOCAL",
                                                        OPERAND_U8,
                                                        h_inc_local}, {'g',
                                                                       "DECREMENT LOCAL",
                                                                       OPERAND_U8,
                                                                       h_dec_local},
    {'j', "JUMP", OPERAND_I16, h_jump}, {'k', "JUMP IF FALSE", OPERAND_I16,
                                         h_jif}, {'l', "STRINGLIT",
                                                  OPERAND_BLOB_I16, h_str},
    {'m', "MULTIPLY", OPERAND_NONE, h_mul}, {'n', "NEGATE", OPERAND_NONE,
                                             h_neg}, {'o', "BOOL EQ",
                                                      OPERAND_NONE, h_eq},
    {'p', "INTEGER", OPERAND_I64, h_int}, {'q', "BOOL NOTEQ", OPERAND_NONE,
                                           h_neq}, {'r', "BOOL LT",
                                                    OPERAND_NONE, h_lt}, {'s',
                                                                          "SUBTRACT",
                                                                          OPERAND_NONE,
                                                                          h_sub},
    {'t', "BOOL GT", OPERAND_NONE, h_gt}, {'u', "BOOL LTEQ", OPERAND_NONE,
                                           h_lte}, {'v', "BOOL GTEQ",
                                                    OPERAND_NONE, h_gte},
    {'x', "LOGICAL NOT", OPERAND_NONE, h_not}, {'y', "LOGICAL AND",
                                                OPERAND_NONE, h_and}, {'z',
                                                                       "LOGICAL OR",
                                                                       OPERAND_NONE,
                                                                       h_or},
    {'A', "LIBCALL", OPERAND_U8, h_libcall}, {'B', "ITEM SAVE CODE",
                                              OPERAND_BLOB_I16, h_emb}, {'C',
                                                                         "SAVE ITEM",
                                                                         OPERAND_NONE,
                                                                         h_save_item},
    {'F', "CALL/ITEM DEREF", OPERAND_U8, h_f}, {'I', "BEGIN ITEM",
                                                OPERAND_NONE, h_I}, {'W',
                                                                     "DELETE ITEM",
                                                                     OPERAND_NONE,
                                                                     h_delete_item},
    {'X', "ITEM EXISTS", OPERAND_NONE, h_item_exists}, {'Y', "NTHNAME",
                                                        OPERAND_NONE,
                                                        h_nthname}, {'Z',
                                                                     "ROOTNAME",
                                                                     OPERAND_NONE,
                                                                     h_rootname},
};

static uint8_t *decode_opcode(uint8_t *p, uint8_t *end, decode_context_t c) {
  if (!require_bytes(p, end, 1, "opcode")) return p;
  uint8_t *start = p;
  uint8_t op = *p++;
  instruction_count++;
  for (size_t i = 0; i < sizeof(OPCODES) / sizeof(OPCODES[0]); i++) {
    if (OPCODES[i].opcode == op) {
      p = OPCODES[i].handler(p, end, c);
      print_raw(start, p);
      if (opt_raw) outln("\n");
      return p;
    }
  }
  unknown_opcode_count++;
  warning_count++;
  outln("UNKNOWN OPCODE 0x%02X (%c)\n", op,
        (op >= 32 && op <= 126) ? op : '.');
  print_raw(start, p);
  if (opt_raw) outln("\n");
  return p;
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
  int opt;
  const struct option options[] =
    { {"help", no_argument, 0, 'h'},
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
  opcodeptr = bytecode;
  uint8_t *end = bytecode + filesize;
  if (!require_bytes(opcodeptr, end, 2, "locals/params header")) {
    FREE_ARRAY(unsigned char, bytecode, filesize);
    exit(EXIT_FAILURE);
  }
  uint8_t locals = *opcodeptr++;
  uint8_t params = *opcodeptr++;
  if (!opt_no_header && locals > 0) logmsg("Local variables: %d\n", locals);
  else if (!opt_no_header) logmsg("No local variables.\n");
  if (!opt_no_header
      && params > 0) logmsg("(Of which, %d are parameters.)\n", locals);
  else if (!opt_no_header) logmsg("(No parameters.)\n");
  while (opcodeptr < end && *opcodeptr != 'h') {
    outln("Byte %05u: ", opcodeptr - bytecode - 1);
    opcodeptr = decode_opcode(opcodeptr, end, DECODE_STMT);
    if (decode_failed) break;
  }
  if (decode_failed)
      logerr("Disassembly aborted due to malformed bytecode.\n");
  else if (opcodeptr >=
           end)
      logerr
      ("Decode error: unterminated stream (missing HALT 'h' before EOF).\n");
  else outln("Byte %05u: HALT\n", opcodeptr - bytecode - 1);
  outln("Summary: instructions=%d unknown=%d warnings=%d\n",
        instruction_count, unknown_opcode_count, warning_count);
  logmsg("Shutting down.\n");
  FREE_ARRAY(unsigned char, bytecode, filesize);
  return EXIT_SUCCESS;
}
