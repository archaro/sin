#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "bytecode_verify.h"
#include "compiler_pipeline.h"
#include "error.h"
#include "test_assert.h"
#include "test_helpers.h"
#include "shared/test_pipeline_cases.h"

static void assert_verify_status(const uint8_t *bytes, uint32_t len,
                                 BC_VerifyStatus expected,
                                 const char *label,
                                 const char *expected_message) {
  BC_VerifyResult result = bc_verify_bytecode(bytes, len, label, NULL);
  ASSERT_EQ_INT(expected, result.status);
  if (expected_message != NULL) {
    ASSERT_TRUE(strstr(result.diagnostic.message, expected_message) != NULL);
  }
}

void test_bytecode_verify_minimal_and_header_errors(void) {
  const uint8_t minimal[] = {0, 0, 'h'};
  assert_verify_status(minimal, sizeof(minimal), BC_VERIFY_OK, "minimal", NULL);

  const uint8_t header_too_short[] = {0};
  assert_verify_status(header_too_short, sizeof(header_too_short),
                       BC_VERIFY_ERROR, "header_too_short",
                       "missing two-byte locals/params header");

  const uint8_t params_exceed_locals[] = {0, 1, 'h'};
  assert_verify_status(params_exceed_locals, sizeof(params_exceed_locals),
                       BC_VERIFY_ERROR, "params_exceed_locals",
                       "parameter count exceeds local count");
}

void test_bytecode_verify_opcode_halt_and_trailing_bytes(void) {
  const uint8_t unknown_opcode[] = {0, 0, 0x7F, 'h'};
  assert_verify_status(unknown_opcode, sizeof(unknown_opcode), BC_VERIFY_ERROR,
                       "unknown_opcode", "unknown opcode");

  const uint8_t missing_halt[] = {0, 0, 'b', 1};
  assert_verify_status(missing_halt, sizeof(missing_halt), BC_VERIFY_ERROR,
                       "missing_halt", "missing terminating HALT opcode");

  const uint8_t trailing_after_halt[] = {0, 0, 'h', 'h'};
  assert_verify_status(trailing_after_halt, sizeof(trailing_after_halt),
                       BC_VERIFY_ERROR, "trailing_after_halt",
                       "trailing bytes after HALT");
}

void test_bytecode_verify_truncated_operand_widths(void) {
  const uint8_t truncated_u8[] = {0, 0, 'b'};
  assert_verify_status(truncated_u8, sizeof(truncated_u8), BC_VERIFY_ERROR,
                       "truncated_u8", "truncated PUSH_BOOL");

  const uint8_t truncated_i16[] = {0, 0, 'j', 0};
  assert_verify_status(truncated_i16, sizeof(truncated_i16), BC_VERIFY_ERROR,
                       "truncated_i16", "truncated JUMP");

  const uint8_t truncated_i64[] = {0, 0, 'p', 1, 2, 3, 4, 5, 6, 7};
  assert_verify_status(truncated_i64, sizeof(truncated_i64), BC_VERIFY_ERROR,
                       "truncated_i64", "truncated PUSH_INT");

  const uint8_t truncated_f64[] = {0, 0, 'P', 1, 2, 3, 4, 5, 6, 7};
  assert_verify_status(truncated_f64, sizeof(truncated_f64), BC_VERIFY_ERROR,
                       "truncated_f64", "truncated PUSH_FLOAT");

  const uint8_t truncated_string_blob[] = {0, 0, 'l', 3, 0, 'a', 'b'};
  assert_verify_status(truncated_string_blob, sizeof(truncated_string_blob),
                       BC_VERIFY_ERROR, "truncated_string_blob",
                       "truncated PUSH_STRING");

  const uint8_t truncated_embedded_code_blob[] = {0, 0, 'B', 3, 0, '1', ';'};
  assert_verify_status(truncated_embedded_code_blob,
                       sizeof(truncated_embedded_code_blob), BC_VERIFY_ERROR,
                       "truncated_embedded_code_blob",
                       "truncated ITEM_SAVE_CODE");
}

void test_bytecode_verify_local_indexes_and_items(void) {
  const uint8_t bad_load_local[] = {0, 0, 'e', 0, 'h'};
  assert_verify_status(bad_load_local, sizeof(bad_load_local), BC_VERIFY_ERROR,
                       "bad_load_local", "local index 0 out of range");

  const uint8_t valid_nested_item[] = {
      1, 0, 'I', 'L', 3, 'f', 'o', 'o', 'D', 'I', 'L', 3, 'b', 'a', 'r',
      'D', 'V', 0, 'E', 'E', 'h'};
  assert_verify_status(valid_nested_item, sizeof(valid_nested_item),
                       BC_VERIFY_OK, "valid_nested_item", NULL);

  const uint8_t invalid_nested_item[] = {
      1, 0, 'I', 'L', 3, 'f', 'o', 'o', 'D', 'I', 'Q', 'E', 'E', 'h'};
  assert_verify_status(invalid_nested_item, sizeof(invalid_nested_item),
                       BC_VERIFY_ERROR, "invalid_nested_item",
                       "unknown item-layer opcode");
}

void test_bytecode_verify_jumps_and_stack_flow(void) {
  const uint8_t jump_out_of_range[] = {0, 0, 'j', 4, 0, 'h'};
  assert_verify_status(jump_out_of_range, sizeof(jump_out_of_range),
                       BC_VERIFY_ERROR, "jump_out_of_range",
                       "jump target past bytecode body");

  const uint8_t jump_into_operand_payload[] = {
      0, 0, 'l', 3, 0, 'a', 'b', 'c', 'j', 0xFD, 0xFF, 'h'};
  assert_verify_status(jump_into_operand_payload,
                       sizeof(jump_into_operand_payload), BC_VERIFY_ERROR,
                       "jump_into_operand_payload",
                       "not a top-level instruction boundary");

  const uint8_t stack_underflow[] = {0, 0, 'a', 'h'};
  assert_verify_status(stack_underflow, sizeof(stack_underflow),
                       BC_VERIFY_ERROR, "stack_underflow", "stack underflow");

  const uint8_t branch_stack_mismatch[] = {
      0, 0, 'b', 1, 'k', 0x0B, 0x00,
      'p', 7, 0, 0, 0, 0, 0, 0, 0,
      'p', 8, 0, 0, 0, 0, 0, 0, 0,
      'h'};
  assert_verify_status(branch_stack_mismatch, sizeof(branch_stack_mismatch),
                       BC_VERIFY_ERROR, "branch_stack_mismatch",
                       "conflicting stack depths");
}

void test_bytecode_verify_pipeline_fixture_bytecode(void) {
  size_t count = 0;
  const PipelineGoldenCase *cases = pipeline_golden_cases(&count);
  ASSERT_TRUE(count > 0);
  for (size_t i = 0; i < count; i++) {
    size_t len = 0;
    uint8_t *bytes = load_hex_fixture(cases[i].fixture_path, &len);
    ASSERT_NOT_NULL(bytes);
    BC_VerifyResult result = bc_verify_bytecode(bytes, (uint32_t)len,
                                                cases[i].name, NULL);
    ASSERT_EQ_INT(BC_VERIFY_OK, result.status);
    free(bytes);
  }
}

void test_bytecode_verify_compiler_emitted_bytecode(void) {
  const char *sources[] = {
      "42;",
      "@x = 7; @x;",
      "if 1 < 2 then 9; else 7; endif;",
      "foo.12;",
  };
  for (size_t i = 0; i < sizeof(sources) / sizeof(sources[0]); i++) {
    OUTPUT_t *out = NULL;
    char *errdetail = NULL;
    int8_t rc = compile_source_to_bytecode(sources[i], strlen(sources[i]), &out,
                                           &errdetail);
    ASSERT_EQ_INT(ERR_NOERROR, rc);
    ASSERT_NOT_NULL(out);
    BC_VerifyResult result = bc_verify_bytecode(
        out->bytecode, (uint32_t)(out->nextbyte - out->bytecode), sources[i], NULL);
    ASSERT_EQ_INT(BC_VERIFY_OK, result.status);
    free(errdetail);
    free(out->bytecode);
    free(out);
  }
}
