// Intermediate representation (IR) for compiler pipeline.
// IR is intentionally independent of concrete VM byte encoding.

// Licensed under the MIT License - see LICENSE file for details.

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "bytecode/bytecode_abi.h"
#include "compiler/absyn.h"
#include "compiler/compdiag.h"

typedef struct {
  IR_Op op;
  int32_t a;
  int32_t b;
  int64_t imm;
  CompilerSourceSpan span;
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
  CompilerSourceSpan span;
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

/*
 * Pointer contract: pointer-valued instruction operands and embedded payload
 * strings must reference accessible NUL-terminated storage for the IR unit's
 * lifetime. When param_count is nonzero, params must reference an accessible
 * table of that many string pointers. Preflight can validate nulls, table
 * consistency, semantic shape, lengths, and wire-format representability; C
 * cannot portably validate an arbitrary forged or dangling non-null address.
 */
typedef struct {
  const char* source;
  size_t param_count;
  const char** params;
  size_t local_count;
  IR_EmbeddedLocal* locals;
  CompilerSourceSpan span;
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
int8_t ir_validate_diag(IR_Unit *unit, uint32_t local_count,
                        CompilerDiagnostic *diag);
