#include <stddef.h>

#include "compiler/ir/opcode_schema.h"
#include "test_assert.h"

typedef struct {
  IR_Op op;
  uint8_t expected_symbol;
  int expects_runtime_handler;
} ExpectedCoverage;

void test_emitbc_all_ir_ops_accounted_for(void) {
  /*
   * Keep this manifest in sync with src/compiler/ir/opcode_schema.def.
   * If a new IR opcode is added without updating this list, this test must fail.
   */
  static const ExpectedCoverage expected[] = {
      {IR_OP_HALT, 'h', 0},
      {IR_OP_PUSH_INT, 'p', 1},
      {IR_OP_PUSH_FLOAT, 'P', 1},
      {IR_OP_PUSH_BOOL, 'b', 1},
      {IR_OP_PUSH_STRING, 'l', 1},
      {IR_OP_ADD, 'a', 1},
      {IR_OP_SUB, 's', 1},
      {IR_OP_MUL, 'm', 1},
      {IR_OP_DIV, 'd', 1},
      {IR_OP_NEG, 'n', 1},
      {IR_OP_EQ, 'o', 1},
      {IR_OP_NEQ, 'q', 1},
      {IR_OP_LT, 'r', 1},
      {IR_OP_GT, 't', 1},
      {IR_OP_LE, 'u', 1},
      {IR_OP_GE, 'v', 1},
      {IR_OP_NOT, 'x', 1},
      {IR_OP_AND, 'y', 1},
      {IR_OP_OR, 'z', 1},
      {IR_OP_LOAD_LOCAL, 'e', 1},
      {IR_OP_STORE_LOCAL, 'c', 1},
      {IR_OP_INC_LOCAL, 'f', 1},
      {IR_OP_DEC_LOCAL, 'g', 1},
      {IR_OP_JUMP, 'j', 1},
      {IR_OP_JUMP_IF_FALSE, 'k', 1},
      {IR_OP_LABEL, 0, 0},
      {IR_OP_ITEM_BEGIN, 'I', 1},
      {IR_OP_ITEM_BEGIN_REL, 'R', 1},
      {IR_OP_ITEM_PUSH_LAYER, 'L', 0},
      {IR_OP_ITEM_PUSH_DEREF, 'D', 0},
      {IR_OP_ITEM_PUSH_DEREF_LOCAL, 'V', 0},
      {IR_OP_ITEM_END, 'E', 0},
      {IR_OP_ITEM_DEREF, 'F', 1},
      {IR_OP_ITEM_SAVE, 'C', 1},
      {IR_OP_CALL, 'F', 0},
      {IR_OP_LIBCALL_TOKEN, 'M', 1},
      {IR_OP_EXISTS, 'X', 1},
      {IR_OP_DELETE, 'W', 1},
      {IR_OP_NTHNAME, 'Y', 1},
      {IR_OP_ROOTNAME, 'Z', 1},
      {IR_OP_ITEM_SAVE_CODE, 'B', 1},
  };

  ASSERT_EQ_INT((int)g_ir_opcode_schema_count, (int)(sizeof(expected) / sizeof(expected[0])));

  for (size_t i = 0; i < g_ir_opcode_schema_count; i++) {
    const IR_OpSchema *meta = &g_ir_opcode_schema[i];
    int found = 0;

    for (size_t j = 0; j < sizeof(expected) / sizeof(expected[0]); j++) {
      if (expected[j].op == meta->op) {
        found = 1;
        ASSERT_EQ_INT((int)expected[j].expected_symbol, (int)meta->encoded_symbol);
        ASSERT_EQ_INT(expected[j].expects_runtime_handler, meta->requires_runtime_handler ? 1 : 0);
        break;
      }
    }

    ASSERT_TRUE(found);
  }
}
