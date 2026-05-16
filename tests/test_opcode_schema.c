#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "compiler/ir/opcode_schema.h"
#include "error.h"
#include "test_assert.h"

void test_opcode_schema_consistency(void) {
  for (size_t i = 0; i < g_ir_opcode_schema_count; i++) {
    const IR_OpSchema *meta = &g_ir_opcode_schema[i];
    ASSERT_EQ_INT((int)i, (int)meta->op);
    ASSERT_NOT_NULL(meta->name);
    ASSERT_TRUE(meta->size_policy >= SIZE_FIXED_0 && meta->size_policy <= SIZE_ITEM_SAVE_CODE);
    ASSERT_TRUE(meta->validator >= VALIDATE_NONE && meta->validator <= VALIDATE_EMBEDDED_INDEX);
    ASSERT_TRUE(meta->operand_kind >= OPERAND_NONE && meta->operand_kind <= OPERAND_EMBEDDED_ID);
    if (meta->op == IR_OP_LABEL) ASSERT_EQ_INT(0, meta->encoded_symbol);
    else ASSERT_TRUE(meta->encoded_symbol != 0);
  }

  char *err_a = NULL;
  char *err_b = NULL;
  int8_t rc_a = ir_opcode_schema_validate_unique(&err_a);
  int8_t rc_b = ir_opcode_schema_validate_unique(&err_b);
  ASSERT_TRUE(rc_a != ERR_NOERROR);
  ASSERT_TRUE(rc_b != ERR_NOERROR);
  ASSERT_NOT_NULL(err_a);
  ASSERT_NOT_NULL(err_b);
  ASSERT_TRUE(strstr(err_a, "ITEM_DEREF") != NULL || strstr(err_a, "CALL") != NULL);
  ASSERT_EQ_INT(0, strcmp(err_a, err_b));

  free(err_a);
  free(err_b);
}
