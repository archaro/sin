// Intermediate representation for bytecode/codegen emit stage.
//
// Licensed under the MIT License - see LICENSE file for details.

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
  IR_OP_PUSH_INT,
  IR_OP_PUSH_STR,

  IR_OP_LOAD_LOCAL,
  IR_OP_STORE_LOCAL,
  IR_OP_INC_LOCAL,
  IR_OP_DEC_LOCAL,

  IR_OP_ADD,
  IR_OP_SUB,
  IR_OP_MUL,
  IR_OP_DIV,

  IR_OP_EQ,
  IR_OP_NE,
  IR_OP_GT,
  IR_OP_GE,
  IR_OP_LT,
  IR_OP_LE,

  IR_OP_AND,
  IR_OP_OR,
  IR_OP_NOT,

  IR_OP_BUILD_ITEM,
  IR_OP_DEREF,

  IR_OP_CALL,
  IR_OP_LIBCALL,

  IR_OP_EXISTS,
  IR_OP_DELETE,
  IR_OP_NTHNAME,
  IR_OP_ROOTNAME,

  IR_OP_JUMP,
  IR_OP_JUMPFALSE,

  IR_OP_RETURN,
  IR_OP_HALT,
} IR_OP;

typedef struct {
  IR_OP op;
  union {
    int64_t i64;
    const char *sid;
    uint8_t local;
    uint16_t label;
    uint8_t argc;
  } operand;
} IR_INST;

typedef struct {
  uint32_t inst_index;
  uint16_t label;
} IR_FIXUP;

typedef struct {
  bool bound;
  uint32_t inst_index;
} IR_LABEL;

typedef struct {
  IR_INST *code;
  uint32_t count;
  uint32_t capacity;

  IR_LABEL *labels;
  uint16_t label_count;
  uint16_t label_capacity;

  IR_FIXUP *fixups;
  uint32_t fixup_count;
  uint32_t fixup_capacity;

  uint32_t max_stack;
} IR_CTX;

IR_CTX *ir_new(void);
void ir_free(IR_CTX *fn);

uint16_t ir_new_label(IR_CTX *fn);
void ir_bind_label(IR_CTX *fn, uint16_t label);

void ir_emit_op(IR_CTX *fn, IR_OP op);
void ir_emit_i64(IR_CTX *fn, IR_OP op, int64_t value);
void ir_emit_sid(IR_CTX *fn, IR_OP op, const char *sid);
void ir_emit_local(IR_CTX *fn, IR_OP op, uint8_t local);
void ir_emit_label(IR_CTX *fn, IR_OP op, uint16_t label);
void ir_emit_argc(IR_CTX *fn, IR_OP op, uint8_t argc);

bool ir_patch_labels(IR_CTX *fn);

