#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "compiler/ir/opcode_schema.h"
#include "bytecode_verify.h"
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
    if (meta->requires_runtime_handler) ASSERT_NOT_NULL(meta->runtime_handler_name);
    else ASSERT_TRUE(meta->runtime_handler_name == NULL);
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


void test_bytecode_verify_local_index_bounds(void) {
  const uint8_t top_level_opcodes[] = {'e', 'c', 'f', 'g'};
  for (size_t i = 0; i < sizeof(top_level_opcodes); i++) {
    const uint8_t local_zero_count_zero[] = {0, 0, top_level_opcodes[i], 0, 'h'};
    BC_VerifyResult result = bc_verify_bytecode(local_zero_count_zero,
                                                sizeof(local_zero_count_zero),
                                                "top_local_zero_count_zero", NULL);
    ASSERT_EQ_INT(BC_VERIFY_ERROR, result.status);
    ASSERT_TRUE(strstr(result.diagnostic.message, "local index 0 out of range for local count 0") != NULL);

    const uint8_t local_one_count_one[] = {1, 0, top_level_opcodes[i], 1, 'h'};
    result = bc_verify_bytecode(local_one_count_one, sizeof(local_one_count_one),
                                "top_local_one_count_one", NULL);
    ASSERT_EQ_INT(BC_VERIFY_ERROR, result.status);
    ASSERT_TRUE(strstr(result.diagnostic.message, "local index 1 out of range for local count 1") != NULL);
  }

  const uint8_t deref_zero_count_zero[] = {0, 0, 'I', 'D', 'V', 0, 'E', 'h'};
  BC_VerifyResult result = bc_verify_bytecode(deref_zero_count_zero,
                                              sizeof(deref_zero_count_zero),
                                              "deref_zero_count_zero", NULL);
  ASSERT_EQ_INT(BC_VERIFY_ERROR, result.status);
  ASSERT_TRUE(strstr(result.diagnostic.message, "local index 0 out of range for local count 0") != NULL);

  const uint8_t deref_one_count_one[] = {1, 0, 'I', 'D', 'V', 1, 'E', 'h'};
  result = bc_verify_bytecode(deref_one_count_one, sizeof(deref_one_count_one),
                              "deref_one_count_one", NULL);
  ASSERT_EQ_INT(BC_VERIFY_ERROR, result.status);
  ASSERT_TRUE(strstr(result.diagnostic.message, "local index 1 out of range for local count 1") != NULL);
}

void test_bytecode_verify_item_expression_streams(void) {
  const uint8_t valid[] = {
      1, 0,
      'I', 'L', 3, 'f', 'o', 'o',
           'D', 'V', 0,
           'D', 'I', 'L', 3, 'b', 'a', 'r', 'E',
      'E',
      'h'};
  BC_VerifyResult result = bc_verify_bytecode(valid, sizeof(valid), "valid", NULL);
  ASSERT_EQ_INT(BC_VERIFY_OK, result.status);

  const uint8_t missing_end[] = {0, 0, 'I', 'L', 1, 'x'};
  result = bc_verify_bytecode(missing_end, sizeof(missing_end), "missing_end", NULL);
  ASSERT_EQ_INT(BC_VERIFY_ERROR, result.status);
  ASSERT_TRUE(strstr(result.diagnostic.message, "unterminated item stream") != NULL);

  const uint8_t truncated_layer[] = {0, 0, 'I', 'L', 4, 'x', 'E', 'h'};
  result = bc_verify_bytecode(truncated_layer, sizeof(truncated_layer), "truncated_layer", NULL);
  ASSERT_EQ_INT(BC_VERIFY_ERROR, result.status);
  ASSERT_TRUE(strstr(result.diagnostic.message, "truncated layer string") != NULL);

  const uint8_t bad_deref_type[] = {0, 0, 'I', 'D', 'Q', 'E', 'h'};
  result = bc_verify_bytecode(bad_deref_type, sizeof(bad_deref_type), "bad_deref_type", NULL);
  ASSERT_EQ_INT(BC_VERIFY_ERROR, result.status);
  ASSERT_TRUE(strstr(result.diagnostic.message, "unknown dereference type") != NULL);

  const uint8_t missing_local_index[] = {0, 0, 'I', 'D', 'V'};
  result = bc_verify_bytecode(missing_local_index, sizeof(missing_local_index), "missing_local_index", NULL);
  ASSERT_EQ_INT(BC_VERIFY_ERROR, result.status);
  ASSERT_TRUE(strstr(result.diagnostic.message, "truncated dereference local index") != NULL);

  const uint8_t unknown_layer[] = {0, 0, 'I', 'Q', 'E', 'h'};
  result = bc_verify_bytecode(unknown_layer, sizeof(unknown_layer), "unknown_layer", NULL);
  ASSERT_EQ_INT(BC_VERIFY_ERROR, result.status);
  ASSERT_TRUE(strstr(result.diagnostic.message, "unknown item-layer opcode") != NULL);
}

void test_bytecode_verify_jump_targets(void) {
  const uint8_t before_start[] = {0, 0, 'j', 0xFE, 0xFF, 'h'};
  BC_VerifyResult result = bc_verify_bytecode(before_start, sizeof(before_start),
                                              "before_start", NULL);
  ASSERT_EQ_INT(BC_VERIFY_ERROR, result.status);
  ASSERT_TRUE(strstr(result.diagnostic.message, "before bytecode body") != NULL);

  const uint8_t past_end[] = {0, 0, 'j', 0x04, 0x00, 'h'};
  result = bc_verify_bytecode(past_end, sizeof(past_end), "past_end", NULL);
  ASSERT_EQ_INT(BC_VERIFY_ERROR, result.status);
  ASSERT_TRUE(strstr(result.diagnostic.message, "past bytecode body") != NULL);

  const uint8_t into_string_payload[] = {
      0, 0, 'l', 3, 0, 'a', 'b', 'c', 'j', 0xFD, 0xFF, 'h'};
  result = bc_verify_bytecode(into_string_payload, sizeof(into_string_payload),
                              "into_string_payload", NULL);
  ASSERT_EQ_INT(BC_VERIFY_ERROR, result.status);
  ASSERT_TRUE(strstr(result.diagnostic.message, "not a top-level instruction boundary") != NULL);

  const uint8_t into_item_payload[] = {
      0, 0, 'I', 'L', 3, 'a', 'b', 'c', 'E', 'j', 0xFC, 0xFF, 'h'};
  result = bc_verify_bytecode(into_item_payload, sizeof(into_item_payload),
                              "into_item_payload", NULL);
  ASSERT_EQ_INT(BC_VERIFY_ERROR, result.status);
  ASSERT_TRUE(strstr(result.diagnostic.message, "not a top-level instruction boundary") != NULL);

  const uint8_t directly_to_halt[] = {0, 0, 'j', 0x02, 0x00, 'h'};
  result = bc_verify_bytecode(directly_to_halt, sizeof(directly_to_halt),
                              "directly_to_halt", NULL);
  ASSERT_EQ_INT(BC_VERIFY_OK, result.status);

  const uint8_t forward_jump[] = {0, 0, 'j', 0x02, 0x00, 'b', 1, 'h'};
  result = bc_verify_bytecode(forward_jump, sizeof(forward_jump),
                              "forward_jump", NULL);
  ASSERT_EQ_INT(BC_VERIFY_OK, result.status);

  const uint8_t backward_conditional_jump[] = {
      0, 0, 'b', 1, 'k', 0xFD, 0xFF, 'h'};
  result = bc_verify_bytecode(backward_conditional_jump,
                              sizeof(backward_conditional_jump),
                              "backward_conditional_jump", NULL);
  ASSERT_EQ_INT(BC_VERIFY_OK, result.status);
}

void test_bytecode_verify_stack_flow(void) {
  const uint8_t underflow[] = {0, 0, 'a', 'h'};
  BC_VerifyResult result = bc_verify_bytecode(underflow, sizeof(underflow),
                                              "stack_underflow", NULL);
  ASSERT_EQ_INT(BC_VERIFY_ERROR, result.status);
  ASSERT_TRUE(strstr(result.diagnostic.message, "stack underflow") != NULL);

  const uint8_t valid_linear[] = {0, 0, 'p', 1, 0, 0, 0, 0, 0, 0, 0,
                                        'p', 2, 0, 0, 0, 0, 0, 0, 0,
                                        'a', 'h'};
  result = bc_verify_bytecode(valid_linear, sizeof(valid_linear),
                              "valid_linear", NULL);
  ASSERT_EQ_INT(BC_VERIFY_OK, result.status);

  const uint8_t valid_branch[] = {0, 0, 'b', 1, 'k', 0x0E, 0x00,
                                        'p', 7, 0, 0, 0, 0, 0, 0, 0,
                                        'j', 0x0B, 0x00,
                                        'p', 8, 0, 0, 0, 0, 0, 0, 0,
                                        'h'};
  result = bc_verify_bytecode(valid_branch, sizeof(valid_branch),
                              "valid_branch", NULL);
  ASSERT_EQ_INT(BC_VERIFY_OK, result.status);

  const uint8_t invalid_branch[] = {0, 0, 'b', 1, 'k', 0x0B, 0x00,
                                          'p', 7, 0, 0, 0, 0, 0, 0, 0,
                                          'p', 8, 0, 0, 0, 0, 0, 0, 0,
                                          'h'};
  result = bc_verify_bytecode(invalid_branch, sizeof(invalid_branch),
                              "invalid_branch", NULL);
  ASSERT_EQ_INT(BC_VERIFY_ERROR, result.status);
  ASSERT_TRUE(strstr(result.diagnostic.message, "conflicting stack depths") != NULL);
}
