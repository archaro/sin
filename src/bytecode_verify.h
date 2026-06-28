#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "compiler/ir/opcode_schema.h"


typedef enum {
  BC_VERIFY_MODE_RUNTIME = 0,
  BC_VERIFY_MODE_ITEMSTORE = 1,
  BC_VERIFY_MODE_DISASSEMBLY = 2
} BC_VerifyMode;

typedef enum {
  BC_VERIFY_OK = 0,
  BC_VERIFY_WARNING = 1,
  BC_VERIFY_ERROR = 2
} BC_VerifyStatus;

typedef enum {
  BC_OPERAND_NONE = 0,
  BC_OPERAND_U8,
  BC_OPERAND_U16,
  BC_OPERAND_I16,
  BC_OPERAND_I64,
  BC_OPERAND_F64_BITS,
  BC_OPERAND_CSTR_U16,
  BC_OPERAND_CSTR_U8,
  BC_OPERAND_EMBEDDED_SOURCE
} BC_OperandKind;

typedef struct {
  BC_OperandKind kind;
  uint32_t offset;
  uint32_t width;
} BC_Operand;

typedef struct {
  uint32_t offset;
  uint8_t opcode;
  const IR_OpSchema *schema;
  BC_Operand operand;
} BC_Instruction;

typedef struct {
  uint32_t offset;
  uint8_t opcode;
  char message[192];
} BC_VerifyError;

typedef struct {
  BC_VerifyMode mode;
  bool strict_trailing_bytes;
} BC_VerifyOptions;

typedef struct {
  BC_VerifyStatus status;
  uint32_t instruction_count;
  uint32_t warning_count;
  uint32_t halt_offset;
  BC_VerifyError diagnostic;
} BC_VerifyResult;

typedef enum {
  BC_ITEM_EXPR_ABSOLUTE = 0,
  BC_ITEM_EXPR_RELATIVE = 1
} BC_ItemExprKind;

BC_VerifyOptions bc_verify_default_options(void);
BC_VerifyOptions bc_verify_disassembly_options(void);
BC_VerifyResult bc_verify_bytecode(const uint8_t *bytecode,
                                   uint32_t bytecode_len,
                                   const char *source_label,
                                   const BC_VerifyOptions *options);
bool bc_decode_item_expression(const uint8_t *item_payload,
                               const uint8_t *bytecode_end,
                               BC_ItemExprKind kind,
                               const uint8_t **after_item,
                               BC_VerifyError *diagnostic);
const char *bc_verify_status_name(BC_VerifyStatus status);
