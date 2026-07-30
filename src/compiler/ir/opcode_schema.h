#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "compiler/ir.h"

typedef enum {
  OPERAND_NONE = 0,
  OPERAND_A_U8,
  OPERAND_A_U16,
  OPERAND_A_U32,
  OPERAND_A_B_U8,
  OPERAND_LABEL_ID,
  OPERAND_IMM_I64,
  OPERAND_IMM_F64_BITS,
  OPERAND_IMM_CSTR,
  OPERAND_EMBEDDED_ID
} IR_OperandKind;

typedef enum {
  SIZE_FIXED_0 = 0,
  SIZE_FIXED_1,
  SIZE_FIXED_2,
  SIZE_FIXED_3,
  SIZE_FIXED_5,
  SIZE_PUSH_INT,
  SIZE_PUSH_FLOAT,
  SIZE_PUSH_STRING,
  SIZE_ITEM_PUSH_LAYER,
  SIZE_ITEM_SAVE_CODE
} IR_SizePolicy;

typedef enum {
  VALIDATE_NONE = 0,
  VALIDATE_A_U8,
  VALIDATE_A_U16,
  VALIDATE_A_U32,
  VALIDATE_A_B_U8,
  VALIDATE_LABEL_ID,
  VALIDATE_NON_NULL_IMM,
  VALIDATE_EMBEDDED_INDEX
} IR_Validator;

typedef struct {
  IR_Op op;
  const char *name;
  uint8_t encoded_symbol;
  bool requires_runtime_handler;
  IR_OperandKind operand_kind;
  IR_SizePolicy size_policy;
  IR_Validator validator;
  const char *runtime_handler_name;
} IR_OpSchema;

extern const IR_OpSchema g_ir_opcode_schema[];
extern const size_t g_ir_opcode_schema_count;

const IR_OpSchema *ir_opcode_schema(IR_Op op);
typedef bool (*IR_RuntimeOpcodeVisitor)(uint8_t opcode_byte, IR_Op op, const IR_OpSchema *schema, void *ctx);
void ir_opcode_schema_for_each_runtime_opcode(IR_RuntimeOpcodeVisitor visitor, void *ctx);
int8_t ir_opcode_schema_validate_unique(char **errdetail);
