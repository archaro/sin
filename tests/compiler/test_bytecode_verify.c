#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "bytecode_verify.h"
#include "memory.h"
#include "string_limits.h"
#include "compiler/compiler_pipeline.h"
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

void test_bytecode_verify_policy_profiles(void) {
  BC_VerifyOptions strict = bc_verify_strict_options();
  ASSERT_TRUE(strict.validate_local_indices);
  ASSERT_TRUE(strict.validate_control_flow);
  ASSERT_TRUE(strict.validate_stack_effects);
  ASSERT_EQ_INT(BC_TRAILING_BYTES_ERROR, strict.trailing_bytes);

  BC_VerifyOptions runtime = bc_verify_runtime_options();
  ASSERT_TRUE(!runtime.validate_local_indices);
  ASSERT_TRUE(!runtime.validate_control_flow);
  ASSERT_TRUE(!runtime.validate_stack_effects);
  ASSERT_EQ_INT(BC_TRAILING_BYTES_WARNING, runtime.trailing_bytes);

  BC_VerifyOptions disassembly = bc_verify_disassembly_options();
  ASSERT_TRUE(disassembly.validate_local_indices);
  ASSERT_TRUE(!disassembly.validate_control_flow);
  ASSERT_TRUE(!disassembly.validate_stack_effects);
  ASSERT_EQ_INT(BC_TRAILING_BYTES_WARNING, disassembly.trailing_bytes);

  const uint8_t trailing[] = {0, 0, 'h', 'h'};
  BC_VerifyResult result = bc_verify_bytecode(
      trailing, sizeof(trailing), "strict trailing", &strict);
  ASSERT_EQ_INT(BC_VERIFY_ERROR, result.status);
  result = bc_verify_bytecode(trailing, sizeof(trailing),
                              "runtime trailing", &runtime);
  ASSERT_EQ_INT(BC_VERIFY_WARNING, result.status);
  result = bc_verify_bytecode(trailing, sizeof(trailing),
                              "disassembly trailing", &disassembly);
  ASSERT_EQ_INT(BC_VERIFY_WARNING, result.status);

  const uint8_t invalid_jump[] = {0, 0, 'j', 4, 0, 'h'};
  result = bc_verify_bytecode(invalid_jump, sizeof(invalid_jump),
                              "strict jump", &strict);
  ASSERT_EQ_INT(BC_VERIFY_ERROR, result.status);
  result = bc_verify_bytecode(invalid_jump, sizeof(invalid_jump),
                              "runtime jump", &runtime);
  ASSERT_EQ_INT(BC_VERIFY_OK, result.status);
  result = bc_verify_bytecode(invalid_jump, sizeof(invalid_jump),
                              "disassembly jump", &disassembly);
  ASSERT_EQ_INT(BC_VERIFY_OK, result.status);

  const uint8_t underflow[] = {0, 0, 'a', 'h'};
  result = bc_verify_bytecode(underflow, sizeof(underflow),
                              "strict stack", &strict);
  ASSERT_EQ_INT(BC_VERIFY_ERROR, result.status);
  result = bc_verify_bytecode(underflow, sizeof(underflow),
                              "runtime stack", &runtime);
  ASSERT_EQ_INT(BC_VERIFY_OK, result.status);
  result = bc_verify_bytecode(underflow, sizeof(underflow),
                              "disassembly stack", &disassembly);
  ASSERT_EQ_INT(BC_VERIFY_OK, result.status);

  const uint8_t invalid_local[] = {0, 0, 'e', 1, 'h'};
  result = bc_verify_bytecode(invalid_local, sizeof(invalid_local),
                              "strict local", &strict);
  ASSERT_EQ_INT(BC_VERIFY_ERROR, result.status);
  result = bc_verify_bytecode(invalid_local, sizeof(invalid_local),
                              "runtime local", &runtime);
  ASSERT_EQ_INT(BC_VERIFY_OK, result.status);
  result = bc_verify_bytecode(invalid_local, sizeof(invalid_local),
                              "disassembly local", &disassembly);
  ASSERT_EQ_INT(BC_VERIFY_ERROR, result.status);
}

void test_bytecode_verify_analysis_storage_is_profile_scoped(void) {
  enum { PUSH_COUNT = 4096 };
  const size_t bytecode_len = 2 + (size_t)PUSH_COUNT * 2 + 1;
  uint8_t *bytecode = malloc(bytecode_len);
  ASSERT_NOT_NULL(bytecode);
  size_t pos = 0;
  bytecode[pos++] = 0;
  bytecode[pos++] = 0;
  for (size_t i = 0; i < PUSH_COUNT; i++) {
    bytecode[pos++] = 'b';
    bytecode[pos++] = 1;
  }
  bytecode[pos++] = 'h';
  ASSERT_EQ_INT(bytecode_len, pos);

  BC_VerifyOptions disassembly = bc_verify_disassembly_options();
  alloc_test_fail_after(0);
  BC_VerifyResult result = bc_verify_bytecode(
      bytecode, (uint32_t)bytecode_len, "allocation-free disassembly", &disassembly);
  ASSERT_EQ_INT(BC_VERIFY_OK, result.status);

  BC_VerifyOptions strict = bc_verify_strict_options();
  result = bc_verify_bytecode(bytecode, (uint32_t)bytecode_len,
                              "analysis allocation failure", &strict);
  ASSERT_EQ_INT(BC_VERIFY_ERROR, result.status);
  ASSERT_TRUE(strstr(result.diagnostic.message,
                     "out of memory recording instruction starts") != NULL);

  alloc_test_fail_after(-1);
  free(bytecode);
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
  const uint8_t invalid_opcode[] = {0, 0, 0x7F, 'h'};
  BC_VerifyResult invalid_result = bc_verify_bytecode(
      invalid_opcode, sizeof(invalid_opcode), "invalid_opcode", NULL);
  ASSERT_EQ_INT(BC_VERIFY_ERROR, invalid_result.status);
  ASSERT_EQ_INT(2, invalid_result.diagnostic.offset);
  ASSERT_EQ_INT(0x7F, invalid_result.diagnostic.opcode);
  ASSERT_TRUE(strstr(invalid_result.diagnostic.message,
                     "invalid opcode; recompile from Sinistra source") != NULL);

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
                       "truncated embedded source");

  const uint8_t truncated_parameter_block[] = {0, 0, 'B', 'P', 1};
  assert_verify_status(truncated_parameter_block,
                       sizeof(truncated_parameter_block), BC_VERIFY_ERROR,
                       "truncated_parameter_block",
                       "truncated embedded parameter length");

  const size_t too_many_param_count = 1025;
  const size_t too_many_len = 2 + 2 + too_many_param_count * 3 + 2 + 2;
  uint8_t *too_many_params = malloc(too_many_len);
  ASSERT_NOT_NULL(too_many_params);
  size_t pos = 0;
  too_many_params[pos++] = 0;
  too_many_params[pos++] = 0;
  too_many_params[pos++] = 'B';
  too_many_params[pos++] = 'P';
  for (size_t i = 0; i < too_many_param_count; i++) {
    too_many_params[pos++] = 1;
    too_many_params[pos++] = 0;
    too_many_params[pos++] = 'a';
  }
  too_many_params[pos++] = 0;
  too_many_params[pos++] = 0;
  too_many_params[pos++] = 0;
  too_many_params[pos++] = 0;
  ASSERT_EQ_INT(too_many_len, pos);
  assert_verify_status(too_many_params, (uint32_t)too_many_len,
                       BC_VERIFY_ERROR, "too_many_embedded_parameters",
                       "embedded parameter count exceeds maximum 1024");
  free(too_many_params);

  const size_t excessive_param_bytes_len = 2 + 2 + 2 +
      SIN_MAX_STRING_BYTES + 2 + 1 + 2 + 2;
  uint8_t *excessive_param_bytes = malloc(excessive_param_bytes_len);
  ASSERT_NOT_NULL(excessive_param_bytes);
  pos = 0;
  excessive_param_bytes[pos++] = 0;
  excessive_param_bytes[pos++] = 0;
  excessive_param_bytes[pos++] = 'B';
  excessive_param_bytes[pos++] = 'P';
  excessive_param_bytes[pos++] = 0xFF;
  excessive_param_bytes[pos++] = 0xFF;
  memset(excessive_param_bytes + pos, 'a', SIN_MAX_STRING_BYTES);
  pos += SIN_MAX_STRING_BYTES;
  excessive_param_bytes[pos++] = 1;
  excessive_param_bytes[pos++] = 0;
  excessive_param_bytes[pos++] = 'b';
  excessive_param_bytes[pos++] = 0;
  excessive_param_bytes[pos++] = 0;
  excessive_param_bytes[pos++] = 0;
  excessive_param_bytes[pos++] = 0;
  ASSERT_EQ_INT(excessive_param_bytes_len, pos);
  assert_verify_status(excessive_param_bytes,
                       (uint32_t)excessive_param_bytes_len,
                       BC_VERIFY_ERROR, "excessive_embedded_parameter_bytes",
                       "embedded parameter bytes exceed maximum string size");
  free(excessive_param_bytes);
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

  const uint8_t libcall_underflow[] = {0, 0, 'M', 1, 'h'};
  assert_verify_status(libcall_underflow, sizeof(libcall_underflow),
                       BC_VERIFY_ERROR, "libcall_underflow",
                       "stack underflow");

  const uint8_t valid_libcall[] = {0, 0, 'l', 1, 0, 'x', 'M', 1, 'h'};
  assert_verify_status(valid_libcall, sizeof(valid_libcall), BC_VERIFY_OK,
                       "valid_libcall", NULL);
}

void test_bytecode_verify_nesting_and_vm_stack_limits(void) {
  uint8_t valid_nesting[2 + 1 + (BC_MAX_ITEM_EXPRESSION_DEPTH - 1) * 2 +
                        BC_MAX_ITEM_EXPRESSION_DEPTH + 1];
  size_t pos = 0;
  valid_nesting[pos++] = 0;
  valid_nesting[pos++] = 0;
  valid_nesting[pos++] = 'I';
  for (uint32_t i = 1; i < BC_MAX_ITEM_EXPRESSION_DEPTH; i++) {
    valid_nesting[pos++] = 'D';
    valid_nesting[pos++] = 'I';
  }
  for (uint32_t i = 0; i < BC_MAX_ITEM_EXPRESSION_DEPTH; i++) {
    valid_nesting[pos++] = 'E';
  }
  valid_nesting[pos++] = 'h';
  ASSERT_EQ_INT(sizeof(valid_nesting), pos);
  assert_verify_status(valid_nesting, sizeof(valid_nesting), BC_VERIFY_OK,
                       "valid maximum item nesting", NULL);

  uint8_t excessive_nesting[2 + 1 + BC_MAX_ITEM_EXPRESSION_DEPTH * 2 +
                            BC_MAX_ITEM_EXPRESSION_DEPTH + 1 + 1];
  pos = 0;
  excessive_nesting[pos++] = 0;
  excessive_nesting[pos++] = 0;
  excessive_nesting[pos++] = 'I';
  for (uint32_t i = 0; i < BC_MAX_ITEM_EXPRESSION_DEPTH; i++) {
    excessive_nesting[pos++] = 'D';
    excessive_nesting[pos++] = 'I';
  }
  for (uint32_t i = 0; i <= BC_MAX_ITEM_EXPRESSION_DEPTH; i++) {
    excessive_nesting[pos++] = 'E';
  }
  excessive_nesting[pos++] = 'h';
  ASSERT_EQ_INT(sizeof(excessive_nesting), pos);
  assert_verify_status(excessive_nesting, sizeof(excessive_nesting),
                       BC_VERIFY_ERROR, "excessive item nesting",
                       "item-expression nesting exceeds maximum depth");

  const size_t push_count = 770;
  const size_t bytecode_len = 2 + push_count * 2 + 1;
  uint8_t *excessive_stack = malloc(bytecode_len);
  ASSERT_NOT_NULL(excessive_stack);
  pos = 0;
  excessive_stack[pos++] = 255;
  excessive_stack[pos++] = 0;
  for (size_t i = 0; i < push_count; i++) {
    excessive_stack[pos++] = 'b';
    excessive_stack[pos++] = 1;
  }
  excessive_stack[pos++] = 'h';
  ASSERT_EQ_INT(bytecode_len, pos);
  assert_verify_status(excessive_stack, (uint32_t)bytecode_len,
                       BC_VERIFY_ERROR, "locals plus operand stack",
                       "reserved local slots exceeds VM capacity");
  free(excessive_stack);
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
      "if 1 < 2 then 9; elsif 0 < 1 then 8; 7; else 6; endif; 5;",
      "@x = 0; while @x < 2 do 9; @x++; endwhile; @x;",
      "foo.12;",
      "add = code {@a, @b} ( @a + @b; );",
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
