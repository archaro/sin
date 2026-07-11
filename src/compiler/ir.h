// Intermediate representation (IR) for compiler pipeline.
// IR is intentionally independent of concrete VM byte encoding.

// Licensed under the MIT License - see LICENSE file for details.

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "compiler/absyn.h"
#include "compiler/compdiag.h"

typedef enum {
  IR_OP_HALT = 0,

  IR_OP_PUSH_INT,
  IR_OP_PUSH_FLOAT,
  IR_OP_PUSH_BOOL,
  IR_OP_PUSH_STRING,

  IR_OP_ADD,
  IR_OP_SUB,
  IR_OP_MUL,
  IR_OP_DIV,
  IR_OP_NEG,

  IR_OP_EQ,
  IR_OP_NEQ,
  IR_OP_LT,
  IR_OP_GT,
  IR_OP_LE,
  IR_OP_GE,

  IR_OP_NOT,
  IR_OP_AND,
  IR_OP_OR,

  IR_OP_LOAD_LOCAL,
  IR_OP_STORE_LOCAL,
  IR_OP_INC_LOCAL,
  IR_OP_DEC_LOCAL,

  IR_OP_JUMP,
  IR_OP_JUMP_IF_FALSE,

  IR_OP_LABEL,

  IR_OP_ITEM_BEGIN,
  IR_OP_ITEM_BEGIN_REL,
  IR_OP_ITEM_PUSH_LAYER,
  IR_OP_ITEM_PUSH_DEREF,
  IR_OP_ITEM_PUSH_DEREF_LOCAL,
  IR_OP_ITEM_END,
  IR_OP_ITEM_DEREF,
  IR_OP_ITEM_SAVE,

  IR_OP_CALL,
  IR_OP_LIBCALL_TOKEN,
  IR_OP_EXISTS,
  IR_OP_DELETE,
  IR_OP_NTHNAME,
  IR_OP_ROOTNAME,
  IR_OP_ITEM_SAVE_CODE
} IR_Op;

typedef struct {
  IR_Op op;
  int32_t a;
  int32_t b;
  int64_t imm;
} IR_Inst;

typedef struct {
  size_t count;
  size_t capacity;
  IR_Inst* code;
} IR_Function;

typedef struct {
  int32_t id;
  size_t position;
  bool bound;
} IR_Label;

typedef struct {
  size_t count;
  size_t capacity;
  IR_Label* entries;
} IR_LabelTable;

typedef struct {
  char* name;
  uint8_t index;
  bool param;
} IR_EmbeddedLocal;

typedef struct {
  const char* source;
  size_t param_count;
  const char** params;
  size_t local_count;
  IR_EmbeddedLocal* locals;
} IR_EmbeddedCodePayload;

typedef struct {
  size_t count;
  size_t capacity;
  IR_EmbeddedCodePayload* entries;
} IR_EmbeddedCodeTable;

typedef struct {
  IR_Function function;
  IR_LabelTable labels;
  IR_EmbeddedCodeTable embedded_code;
} IR_Unit;

IR_Unit* ir_create_unit(void);
void ir_destroy_unit(IR_Unit* unit);

size_t ir_emit(IR_Unit* unit, IR_Inst inst);

int32_t ir_new_label(IR_Unit* unit);
bool ir_bind_label(IR_Unit* unit, int32_t label_id);
int32_t ir_add_embedded_code_payload(IR_Unit* unit, IR_EmbeddedCodePayload payload);
bool ir_embedded_locals_from_params(AS_NODE* params, IR_EmbeddedCodePayload* payload);

void ir_dump(FILE* out, IR_Unit* unit);

const char* ir_op_name(IR_Op op);
int8_t ir_validate_diag(IR_Unit* unit, uint32_t local_count, char **errdetail, CompilerDiagnostic *diag);
int8_t ir_validate(IR_Unit* unit, uint32_t local_count, char **errdetail);
