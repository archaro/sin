#include "bytecode_verify.h"
#include "compiler/ir/opcode_schema.h"
#include "libcall.h"
#include "test_assert.h"

typedef struct {
  IR_Op op;
  uint8_t byte;
  BC_OperandKind operand;
  int pops;
  int pushes;
  IR_StackPolicy policy;
  IR_ControlClass flow;
  bool stmt;
  bool item;
  bool deref;
} AbiEntry;

/* Independent v1 compatibility oracle.  Do not derive this table from the
 * production schema: changing an encoding requires a new bytecode version. */
void test_bytecode_v1_abi_manifest(void) {
  static const AbiEntry abi[] = {
      {IR_OP_HALT,'h',BC_OPERAND_NONE,0,0,IR_STACK_FIXED,IR_CONTROL_TERMINATING,1,0,0},
      {IR_OP_RETURN,'Q',BC_OPERAND_NONE,1,0,IR_STACK_FIXED,IR_CONTROL_TERMINATING,1,0,0},
      {IR_OP_PUSH_INT,'p',BC_OPERAND_I64,0,1,IR_STACK_FIXED,IR_CONTROL_STRAIGHT,1,0,0},
      {IR_OP_PUSH_FLOAT,'P',BC_OPERAND_F64_BITS,0,1,IR_STACK_FIXED,IR_CONTROL_STRAIGHT,1,0,0},
      {IR_OP_PUSH_BOOL,'b',BC_OPERAND_U8,0,1,IR_STACK_FIXED,IR_CONTROL_STRAIGHT,1,0,0},
      {IR_OP_PUSH_STRING,'l',BC_OPERAND_CSTR_U16,0,1,IR_STACK_FIXED,IR_CONTROL_STRAIGHT,1,0,0},
      {IR_OP_PUSH_NIL,'N',BC_OPERAND_NONE,0,1,IR_STACK_FIXED,IR_CONTROL_STRAIGHT,1,0,0},
      {IR_OP_ADD,'a',BC_OPERAND_NONE,2,1,IR_STACK_FIXED,IR_CONTROL_STRAIGHT,1,0,0},
      {IR_OP_SUB,'s',BC_OPERAND_NONE,2,1,IR_STACK_FIXED,IR_CONTROL_STRAIGHT,1,0,0},
      {IR_OP_MUL,'m',BC_OPERAND_NONE,2,1,IR_STACK_FIXED,IR_CONTROL_STRAIGHT,1,0,0},
      {IR_OP_DIV,'d',BC_OPERAND_NONE,2,1,IR_STACK_FIXED,IR_CONTROL_STRAIGHT,1,0,0},
      {IR_OP_MOD,'%',BC_OPERAND_NONE,2,1,IR_STACK_FIXED,IR_CONTROL_STRAIGHT,1,0,0},
      {IR_OP_NEG,'n',BC_OPERAND_NONE,1,1,IR_STACK_FIXED,IR_CONTROL_STRAIGHT,1,0,0},
      {IR_OP_EQ,'o',BC_OPERAND_NONE,2,1,IR_STACK_FIXED,IR_CONTROL_STRAIGHT,1,0,0},
      {IR_OP_NEQ,'q',BC_OPERAND_NONE,2,1,IR_STACK_FIXED,IR_CONTROL_STRAIGHT,1,0,0},
      {IR_OP_LT,'r',BC_OPERAND_NONE,2,1,IR_STACK_FIXED,IR_CONTROL_STRAIGHT,1,0,0},
      {IR_OP_GT,'t',BC_OPERAND_NONE,2,1,IR_STACK_FIXED,IR_CONTROL_STRAIGHT,1,0,0},
      {IR_OP_LE,'u',BC_OPERAND_NONE,2,1,IR_STACK_FIXED,IR_CONTROL_STRAIGHT,1,0,0},
      {IR_OP_GE,'v',BC_OPERAND_NONE,2,1,IR_STACK_FIXED,IR_CONTROL_STRAIGHT,1,0,0},
      {IR_OP_NOT,'x',BC_OPERAND_NONE,1,1,IR_STACK_FIXED,IR_CONTROL_STRAIGHT,1,0,0},
      {IR_OP_AND,'y',BC_OPERAND_NONE,2,1,IR_STACK_FIXED,IR_CONTROL_STRAIGHT,1,0,0},
      {IR_OP_OR,'z',BC_OPERAND_NONE,2,1,IR_STACK_FIXED,IR_CONTROL_STRAIGHT,1,0,0},
      {IR_OP_DISCARD,'w',BC_OPERAND_NONE,1,0,IR_STACK_FIXED,IR_CONTROL_STRAIGHT,1,0,0},
      {IR_OP_LOAD_LOCAL,'e',BC_OPERAND_U8,0,1,IR_STACK_FIXED,IR_CONTROL_STRAIGHT,1,0,0},
      {IR_OP_STORE_LOCAL,'c',BC_OPERAND_U8,1,0,IR_STACK_FIXED,IR_CONTROL_STRAIGHT,1,0,0},
      {IR_OP_INC_LOCAL,'f',BC_OPERAND_U8,0,0,IR_STACK_FIXED,IR_CONTROL_STRAIGHT,1,0,0},
      {IR_OP_DEC_LOCAL,'g',BC_OPERAND_U8,0,0,IR_STACK_FIXED,IR_CONTROL_STRAIGHT,1,0,0},
      {IR_OP_JUMP,'j',BC_OPERAND_I16,0,0,IR_STACK_FIXED,IR_CONTROL_JUMP,1,0,0},
      {IR_OP_JUMP_IF_FALSE,'k',BC_OPERAND_I16,1,0,IR_STACK_FIXED,IR_CONTROL_CONDITIONAL,1,0,0},
      {IR_OP_ITEM_BEGIN,'I',BC_OPERAND_NONE,0,1,IR_STACK_FIXED,IR_CONTROL_STRAIGHT,1,0,0},
      {IR_OP_ITEM_BEGIN_REL,'R',BC_OPERAND_NONE,0,1,IR_STACK_FIXED,IR_CONTROL_STRAIGHT,1,0,0},
      {IR_OP_ITEM_PUSH_LAYER,'L',BC_OPERAND_CSTR_U8,0,0,IR_STACK_FIXED,IR_CONTROL_STRAIGHT,0,1,1},
      {IR_OP_ITEM_PUSH_DEREF,'D',BC_OPERAND_NONE,0,0,IR_STACK_FIXED,IR_CONTROL_STRAIGHT,0,1,1},
      {IR_OP_ITEM_PUSH_DEREF_LOCAL,'V',BC_OPERAND_U8,0,0,IR_STACK_FIXED,IR_CONTROL_STRAIGHT,0,1,1},
      {IR_OP_ITEM_END,'E',BC_OPERAND_NONE,0,0,IR_STACK_FIXED,IR_CONTROL_STRAIGHT,0,1,1},
      {IR_OP_ITEM_DEREF,'F',BC_OPERAND_U16,1,1,IR_STACK_FIXED,IR_CONTROL_STRAIGHT,0,0,1},
      {IR_OP_ITEM_SAVE,'C',BC_OPERAND_NONE,2,0,IR_STACK_FIXED,IR_CONTROL_STRAIGHT,1,0,0},
      {IR_OP_CALL,'F',BC_OPERAND_U16,1,1,IR_STACK_CALL,IR_CONTROL_STRAIGHT,1,0,0},
      {IR_OP_LIBCALL,'M',BC_OPERAND_U16,0,1,IR_STACK_LIBCALL,IR_CONTROL_STRAIGHT,1,0,0},
      {IR_OP_ITEM_SAVE_CODE,'B',BC_OPERAND_EMBEDDED_SOURCE,1,0,IR_STACK_FIXED,IR_CONTROL_STRAIGHT,1,0,0},
      {IR_OP_BUILD_LIST,'[',BC_OPERAND_U32,0,1,IR_STACK_BUILD_LIST,IR_CONTROL_STRAIGHT,1,0,0},
      {IR_OP_MAKE_ITEMREF,'&',BC_OPERAND_NONE,1,1,IR_STACK_FIXED,IR_CONTROL_STRAIGHT,1,0,0},
  };
  for (size_t i = 0; i < sizeof abi / sizeof abi[0]; i++) {
    const BC_OpcodeSchema *s = bc_opcode_for_ir(abi[i].op);
    ASSERT_NOT_NULL(s);
    ASSERT_EQ_INT(abi[i].byte, s->opcode);
    ASSERT_EQ_INT(abi[i].operand, s->operand_encoding);
    ASSERT_EQ_INT(abi[i].pops, s->stack_effect.pops);
    ASSERT_EQ_INT(abi[i].pushes, s->stack_effect.pushes);
    ASSERT_EQ_INT(abi[i].policy, s->ir->stack_policy);
    ASSERT_EQ_INT(abi[i].policy != IR_STACK_FIXED,
                  s->stack_effect.operand_dependent);
    ASSERT_EQ_INT(abi[i].flow, s->control_flow);
    ASSERT_EQ_INT(abi[i].stmt, s->valid_in_statement);
    ASSERT_EQ_INT(abi[i].item, s->valid_in_item_expression);
    ASSERT_EQ_INT(abi[i].deref, s->valid_in_dereference);
  }
  for (size_t i = 0; i < g_ir_opcode_schema_count; i++) {
    const IR_OpSchema *ir = &g_ir_opcode_schema[i];
    if (ir->encoded_symbol == 0) continue;
    bool found = false;
    for (size_t j = 0; j < sizeof abi / sizeof abi[0]; j++) {
      if (abi[j].op == ir->op) { found = true; break; }
    }
    ASSERT_TRUE(found);
  }
  const BC_OpcodeSchema *call = bc_opcode_for_ir(IR_OP_CALL);
  ASSERT_EQ_INT(3, bc_opcode_stack_effect(call, 2).pops);
  const BC_OpcodeSchema *list = bc_opcode_for_ir(IR_OP_BUILD_LIST);
  ASSERT_EQ_INT(4, bc_opcode_stack_effect(list, 4).pops);
  ASSERT_EQ_INT(IR_STACK_CALL, call->ir->stack_policy);
  ASSERT_EQ_INT(IR_STACK_BUILD_LIST, list->ir->stack_policy);
  const BC_OpcodeSchema *lib = bc_opcode_for_ir(IR_OP_LIBCALL);
  ASSERT_EQ_INT(IR_STACK_LIBCALL, lib->ir->stack_policy);
  uint8_t args = 0;
  ASSERT_TRUE(libcall_pair_arg_count(4, 7, &args));
  ASSERT_EQ_INT(args, bc_opcode_stack_effect(lib, (4u << 8) | 7u).pops);
  const BC_OpcodeSchema *deref = bc_opcode_lookup('F', BC_CONTEXT_DEREFERENCE);
  ASSERT_EQ_INT(IR_OP_ITEM_DEREF, deref->ir->op);
  ASSERT_EQ_INT(IR_OP_CALL, bc_opcode_lookup('F', BC_CONTEXT_STATEMENT)->ir->op);
  bool assigned[256] = {false};
  for (size_t i = 0; i < sizeof abi / sizeof abi[0]; i++) assigned[abi[i].byte] = true;
  for (unsigned byte = 0; byte < 256; byte++) {
    if (assigned[byte]) continue;
    ASSERT_TRUE(bc_opcode_lookup((uint8_t)byte, BC_CONTEXT_STATEMENT) == NULL);
    ASSERT_TRUE(bc_opcode_lookup((uint8_t)byte, BC_CONTEXT_ITEM_EXPRESSION) == NULL);
    ASSERT_TRUE(bc_opcode_lookup((uint8_t)byte, BC_CONTEXT_DEREFERENCE) == NULL);
  }
}
