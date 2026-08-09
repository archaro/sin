#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Limits that are part of the bytecode/runtime ABI. */
#define SIN_BYTECODE_VALUE_STACK_CAPACITY 1024
#define SIN_BYTECODE_CALL_STACK_CAPACITY 1024
#define SIN_BYTECODE_LIST_ELEMENT_LIMIT ((size_t)1048576u)
#define SIN_BYTECODE_LIST_DEPTH_LIMIT ((size_t)64u)

/* IR operation identifiers are retained for compatibility with compiler APIs,
 * but their numeric values and schema are owned by the bytecode ABI. */
typedef enum {
  IR_OP_HALT = 0,
  IR_OP_PUSH_INT,
  IR_OP_PUSH_FLOAT,
  IR_OP_PUSH_BOOL,
  IR_OP_PUSH_STRING,
  IR_OP_PUSH_NIL,
  IR_OP_ADD,
  IR_OP_SUB,
  IR_OP_MUL,
  IR_OP_DIV,
  IR_OP_MOD,
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
  IR_OP_DISCARD,
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
  IR_OP_LIBCALL,
  IR_OP_ITEM_SAVE_CODE,
  /* Keep new operations appended after the formerly last opcode. */
  IR_OP_RETURN,
  IR_OP_BUILD_LIST,
  IR_OP_MAKE_ITEMREF
} IR_Op;

typedef enum {
  OPERAND_NONE = 0,
  OPERAND_A_U8,
  OPERAND_A_U16,
  OPERAND_A_U32,
  OPERAND_A_B_U8,
  OPERAND_LIBCALL_PAIR,
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
  VALIDATE_LIBCALL_PAIR,
  VALIDATE_LABEL_ID,
  VALIDATE_NON_NULL_IMM,
  VALIDATE_EMBEDDED_INDEX
} IR_Validator;

typedef enum {
  IR_STACK_FIXED = 0,
  IR_STACK_CALL,
  IR_STACK_LIBCALL,
  IR_STACK_BUILD_LIST
} IR_StackPolicy;

typedef enum {
  IR_CONTROL_STRAIGHT = 0,
  IR_CONTROL_JUMP,
  IR_CONTROL_CONDITIONAL,
  IR_CONTROL_TERMINATING,
  IR_CONTROL_IR_ONLY
} IR_ControlClass;

enum {
  IR_OPCODE_CONTEXT_STATEMENT = 1u << 0,
  IR_OPCODE_CONTEXT_ITEM_EXPRESSION = 1u << 1,
  IR_OPCODE_CONTEXT_DEREFERENCE = 1u << 2
};

typedef struct {
  IR_Op op;
  const char *name;
  uint8_t encoded_symbol;
  uint8_t context_mask;
  bool requires_runtime_handler;
  IR_OperandKind operand_kind;
  IR_SizePolicy size_policy;
  IR_Validator validator;
  const char *runtime_handler_name;
  int8_t stack_pops;
  int8_t stack_pushes;
  IR_StackPolicy stack_policy;
  IR_ControlClass control_class;
} IR_OpSchema;

extern const IR_OpSchema g_ir_opcode_schema[];
extern const size_t g_ir_opcode_schema_count;

const IR_OpSchema *ir_opcode_schema(IR_Op op);
typedef bool (*IR_RuntimeOpcodeVisitor)(uint8_t opcode_byte, IR_Op op,
                                        const IR_OpSchema *schema, void *ctx);
void ir_opcode_schema_for_each_runtime_opcode(IR_RuntimeOpcodeVisitor visitor,
                                               void *ctx);
bool ir_opcode_schema_validate_unique(const IR_OpSchema *schema, size_t count,
                                      char **errdetail);
