#include <stddef.h>

#include "compiler/ir/opcode_schema.h"
#include "test_assert.h"

typedef struct {
  IR_Op op;
  uint8_t expected_symbol;
} ExpectedCoverage;

void test_emitbc_all_ir_ops_accounted_for(void) {
  /*
   * Keep this manifest in sync with src/compiler/ir/opcode_schema.def.
   * If a new IR opcode is added without updating this list, this test must fail.
   */
  static const ExpectedCoverage expected[] = {
      {IR_OP_HALT, 'h'},
      {IR_OP_PUSH_INT, 'p'},
      {IR_OP_PUSH_BOOL, 'b'},
      {IR_OP_PUSH_STRING, 'l'},
      {IR_OP_ADD, 'a'},
      {IR_OP_SUB, 's'},
      {IR_OP_MUL, 'm'},
      {IR_OP_DIV, 'd'},
      {IR_OP_NEG, 'n'},
      {IR_OP_EQ, 'o'},
      {IR_OP_NEQ, 'q'},
      {IR_OP_LT, 'r'},
      {IR_OP_GT, 't'},
      {IR_OP_LE, 'u'},
      {IR_OP_GE, 'v'},
      {IR_OP_NOT, 'x'},
      {IR_OP_AND, 'y'},
      {IR_OP_OR, 'z'},
      {IR_OP_LOAD_LOCAL, 'e'},
      {IR_OP_STORE_LOCAL, 'c'},
      {IR_OP_INC_LOCAL, 'f'},
      {IR_OP_DEC_LOCAL, 'g'},
      {IR_OP_JUMP, 'j'},
      {IR_OP_JUMP_IF_FALSE, 'k'},
      {IR_OP_LABEL, 0},
      {IR_OP_ITEM_BEGIN, 'I'},
      {IR_OP_ITEM_BEGIN_REL, 'R'},
      {IR_OP_ITEM_PUSH_LAYER, 'L'},
      {IR_OP_ITEM_PUSH_DEREF, 'D'},
      {IR_OP_ITEM_END, 'E'},
      {IR_OP_ITEM_DEREF, 'F'},
      {IR_OP_ITEM_SAVE, 'C'},
      {IR_OP_CALL, 'F'},
      {IR_OP_LIBCALL_TOKEN, 'M'},
      {IR_OP_EXISTS, 'X'},
      {IR_OP_DELETE, 'W'},
      {IR_OP_NTHNAME, 'Y'},
      {IR_OP_ROOTNAME, 'Z'},
      {IR_OP_ITEM_SAVE_CODE, 'B'},
  };

  ASSERT_EQ_INT((int)g_ir_opcode_schema_count, (int)(sizeof(expected) / sizeof(expected[0])));

  for (size_t i = 0; i < g_ir_opcode_schema_count; i++) {
    const IR_OpSchema *meta = &g_ir_opcode_schema[i];
    int found = 0;

    for (size_t j = 0; j < sizeof(expected) / sizeof(expected[0]); j++) {
      if (expected[j].op == meta->op) {
        found = 1;
        ASSERT_EQ_INT((int)expected[j].expected_symbol, (int)meta->encoded_symbol);
        break;
      }
    }

    ASSERT_TRUE(found);
  }
}
