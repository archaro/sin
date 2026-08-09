#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "compiler/ir/opcode_schema.h"
#include "bytecode_format.h"

#define BC_MAX_ITEM_EXPRESSION_DEPTH 8u


typedef enum {
  BC_VERIFY_OK = 0,
  BC_VERIFY_WARNING = 1,
  BC_VERIFY_ERROR = 2
} BC_VerifyStatus;

typedef enum {
  BC_OPERAND_NONE = 0,
  BC_OPERAND_U8,
  BC_OPERAND_U16,
  BC_OPERAND_U32,
  BC_OPERAND_I16,
  BC_OPERAND_I64,
  BC_OPERAND_F64_BITS,
  BC_OPERAND_CSTR_U16,
  BC_OPERAND_CSTR_U8,
  BC_OPERAND_EMBEDDED_SOURCE
} BC_OperandKind;

typedef enum {
  BC_CONTEXT_STATEMENT = 0,
  BC_CONTEXT_ITEM_EXPRESSION = 1,
  BC_CONTEXT_DEREFERENCE = 2
} BC_Context;

typedef struct {
  int pops;
  int pushes;
  bool operand_dependent;
} BC_StackEffect;

typedef struct {
  uint8_t opcode;
  const char *mnemonic;
  BC_OperandKind operand_encoding;
  bool valid_in_statement;
  bool valid_in_item_expression;
  bool valid_in_dereference;
  BC_StackEffect stack_effect;
  IR_ControlClass control_flow;
  bool terminates;
  bool valid_top_level;
  bool item_assembly_only;
  const IR_OpSchema *ir;
} BC_OpcodeSchema;

typedef struct {
  BC_OperandKind kind;
  uint32_t offset;
  uint32_t width;
  union {
    uint8_t u8;
    uint16_t u16;
    uint32_t u32;
    int16_t i16;
    int64_t i64;
    uint64_t u64;
    struct {
      const uint8_t *data;
      uint16_t len;
    } bytes;
  } value;
} BC_Operand;

typedef enum {
  BC_EVENT_CONTEXT_STMT = 0,
  BC_EVENT_CONTEXT_ITEM = 1,
  BC_EVENT_CONTEXT_DEREF = 2
} BC_EventContext;

typedef struct {
  uint32_t offset;
  uint8_t opcode;
  const char *mnemonic;
  const IR_OpSchema *schema;
  BC_Operand operand;
  const uint8_t *raw;
  uint32_t raw_len;
  BC_EventContext context;
  uint32_t depth;
} BC_Instruction;

typedef struct {
  uint8_t locals;
  uint8_t params;
  uint16_t version;
  uint32_t instruction_offset;
  const uint8_t *instructions;
  bool legacy;
} BC_BytecodeMetadata;

typedef bool (*BC_DecodeInstructionCallback)(const BC_Instruction *instruction, void *ctx);

typedef struct {
  uint32_t offset;
  uint8_t opcode;
  char message[192];
} BC_VerifyError;

/* Verification behavior for configurable, non-executable inspection paths.
 * Use bc_verify_executable_bytecode() for bytecode that will execute. */
typedef struct {
  bool validate_local_indices;
  bool validate_control_flow;
  bool validate_stack_effects;
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

BC_VerifyOptions bc_verify_disassembly_options(void);
BC_VerifyResult bc_verify_executable_bytecode(const uint8_t *bytecode,
                                              uint32_t bytecode_len,
                                              const char *source_label);
BC_VerifyResult bc_verify_bytecode(const uint8_t *bytecode,
                                   uint32_t bytecode_len,
                                   const char *source_label,
                                   const BC_VerifyOptions *options);
BC_VerifyResult bc_decode_bytecode_events(const uint8_t *bytecode,
                                          uint32_t bytecode_len,
                                          const char *source_label,
                                          const BC_VerifyOptions *options,
                                          BC_BytecodeMetadata *metadata,
                                          BC_DecodeInstructionCallback callback,
                                          void *callback_ctx);
bool bc_decode_item_expression(const uint8_t *item_payload,
                               const uint8_t *bytecode_end,
                               BC_ItemExprKind kind,
                               const uint8_t **after_item,
                               BC_VerifyError *diagnostic);
const char *bc_verify_status_name(BC_VerifyStatus status);
const BC_OpcodeSchema *bc_opcode_lookup(uint8_t opcode, BC_Context context);
const BC_OpcodeSchema *bc_opcode_for_ir(IR_Op op);
const char *bc_opcode_mnemonic(const BC_OpcodeSchema *schema);
BC_StackEffect bc_opcode_stack_effect(const BC_OpcodeSchema *schema,
                                      uint32_t operand_u32);
uint8_t bc_opcode_byte(IR_Op op);
BC_OperandKind bc_opcode_operand_encoding(IR_Op op);
