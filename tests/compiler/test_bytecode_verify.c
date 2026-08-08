#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>

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

  BC_VerifyOptions runtime = bc_verify_runtime_options();
  ASSERT_TRUE(runtime.validate_local_indices);
  ASSERT_TRUE(runtime.validate_control_flow);
  ASSERT_TRUE(runtime.validate_stack_effects);

  BC_VerifyOptions disassembly = bc_verify_disassembly_options();
  ASSERT_TRUE(disassembly.validate_local_indices);
  ASSERT_TRUE(!disassembly.validate_control_flow);
  ASSERT_TRUE(!disassembly.validate_stack_effects);

  const uint8_t trailing[] = {0, 0, 'h', 'h'};
  BC_VerifyResult result = bc_verify_bytecode(
      trailing, sizeof(trailing), "multiple terminators", &strict);
  ASSERT_EQ_INT(BC_VERIFY_OK, result.status);

  const uint8_t invalid_jump[] = {0, 0, 'j', 4, 0, 'h'};
  result = bc_verify_bytecode(invalid_jump, sizeof(invalid_jump),
                              "strict jump", &strict);
  ASSERT_EQ_INT(BC_VERIFY_ERROR, result.status);
  result = bc_verify_bytecode(invalid_jump, sizeof(invalid_jump),
                              "runtime jump", &runtime);
  ASSERT_EQ_INT(BC_VERIFY_ERROR, result.status);
  result = bc_verify_bytecode(invalid_jump, sizeof(invalid_jump),
                              "disassembly jump", &disassembly);
  ASSERT_EQ_INT(BC_VERIFY_OK, result.status);

  const uint8_t underflow[] = {0, 0, 'a', 'h'};
  result = bc_verify_bytecode(underflow, sizeof(underflow),
                              "strict stack", &strict);
  ASSERT_EQ_INT(BC_VERIFY_ERROR, result.status);
  result = bc_verify_bytecode(underflow, sizeof(underflow),
                              "runtime stack", &runtime);
  ASSERT_EQ_INT(BC_VERIFY_ERROR, result.status);
  result = bc_verify_bytecode(underflow, sizeof(underflow),
                              "disassembly stack", &disassembly);
  ASSERT_EQ_INT(BC_VERIFY_OK, result.status);

  const uint8_t invalid_local[] = {0, 0, 'e', 1, 'h'};
  result = bc_verify_bytecode(invalid_local, sizeof(invalid_local),
                              "strict local", &strict);
  ASSERT_EQ_INT(BC_VERIFY_ERROR, result.status);
  result = bc_verify_bytecode(invalid_local, sizeof(invalid_local),
                              "runtime local", &runtime);
  ASSERT_EQ_INT(BC_VERIFY_ERROR, result.status);
  result = bc_verify_bytecode(invalid_local, sizeof(invalid_local),
                              "disassembly local", &disassembly);
  ASSERT_EQ_INT(BC_VERIFY_ERROR, result.status);
}

static void test_bytecode_verify_push_nil_return_flow(void) {
  const uint8_t bytes[] = {0, 0, 'N', 'Q', 'h'};
  assert_verify_status(bytes, sizeof(bytes), BC_VERIFY_OK, "push_nil", NULL);
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

void test_bytecode_verify_dense_budget_and_growth_failures(void) {
  const size_t n = 17u * 1024u * 1024u;
  uint8_t *bytes = malloc(n + 3u);
  ASSERT_NOT_NULL(bytes);
  bytes[0] = bytes[1] = 0;
  memset(bytes + 2, 'N', n);
  bytes[n + 2] = 'h';
  BC_VerifyResult r = bc_verify_bytecode(bytes, (uint32_t)(n + 3u), "budget", NULL);
  ASSERT_EQ_INT(BC_VERIFY_ERROR, r.status);
  ASSERT_TRUE(strstr(r.diagnostic.message, "verification analysis memory budget exceeded") != NULL);
  free(bytes);

  const uint8_t tiny[] = {0, 0, 'N', 'h'};
  for (long fail = 0; fail < 5; fail++) {
    alloc_test_fail_after(fail);
    r = bc_verify_bytecode(tiny, sizeof(tiny), "growth failure", NULL);
    ASSERT_EQ_INT(BC_VERIFY_ERROR, r.status);
    ASSERT_TRUE(strstr(r.diagnostic.message, "out of memory") != NULL);
  }
  alloc_test_fail_after(5);
  r = bc_verify_bytecode(tiny, sizeof(tiny), "growth success", NULL);
  ASSERT_EQ_INT(BC_VERIFY_OK, r.status);
  alloc_test_fail_after(-1);
}

void test_bytecode_verify_constrained_address_space(void) {
#if defined(RLIMIT_AS) && !defined(__SANITIZE_ADDRESS__)
  const size_t n = 60u * 1024u * 1024u;
  uint8_t *bytes = malloc(n + 3u);
  ASSERT_NOT_NULL(bytes);
  bytes[0] = bytes[1] = 0;
  size_t pos = 2;
  while (pos + 65539u < n + 2u) {
    bytes[pos++] = 'l'; bytes[pos++] = 0xff; bytes[pos++] = 0xff;
    memset(bytes + pos, 'x', 65535u); pos += 65535u;
    bytes[pos++] = 'w';
  }
  bytes[pos++] = 'h';
  pid_t pid = fork();
  ASSERT_TRUE(pid >= 0);
  if (pid == 0) {
    struct rlimit lim = {256u * 1024u * 1024u, 256u * 1024u * 1024u};
    if (setrlimit(RLIMIT_AS, &lim) != 0) _exit(2);
    BC_VerifyResult r = bc_verify_bytecode(bytes, (uint32_t)pos, "rlimit", NULL);
    if (r.status != BC_VERIFY_OK) {
      dprintf(STDERR_FILENO, "%s\n", r.diagnostic.message);
    }
    _exit(r.status == BC_VERIFY_OK ? 0 : 1);
  }
  int status = 0;
  ASSERT_EQ_INT((int)pid, (int)waitpid(pid, &status, 0));
  ASSERT_TRUE(WIFEXITED(status));
  ASSERT_EQ_INT(0, WEXITSTATUS(status));
  free(bytes);
#endif
}

void test_bytecode_verify_minimal_and_header_errors(void) {
  test_bytecode_verify_push_nil_return_flow();
  const uint8_t minimal[] = {0, 0, 'h'};
  assert_verify_status(minimal, sizeof(minimal), BC_VERIFY_OK, "minimal", NULL);

  const uint8_t header_too_short[] = {0};
  assert_verify_status(header_too_short, sizeof(header_too_short),
                       BC_VERIFY_ERROR, "header_too_short",
                       "truncated bytecode header");

  const uint8_t params_exceed_locals[] = {0, 1, 'h'};
  assert_verify_status(params_exceed_locals, sizeof(params_exceed_locals),
                       BC_VERIFY_ERROR, "params_exceed_locals",
                       "invalid bytecode header");
}

void test_bytecode_format_header_variants(void) {
  const uint8_t v1[] = {0, 0xff, 'S', 'B', 1, 0, 3, 2, 'h'};
  BC_FormatHeader h;
  ASSERT_EQ_INT(BC_FORMAT_OK, bc_decode_header(v1, sizeof(v1), &h));
  ASSERT_TRUE(!h.legacy); ASSERT_EQ_INT(1, h.version);
  ASSERT_EQ_INT(3, h.locals); ASSERT_EQ_INT(2, h.params);
  ASSERT_EQ_INT(8, h.instruction_offset); ASSERT_EQ_INT('h', *h.instructions);
  const uint8_t legacy[] = {3, 2, 'h'};
  ASSERT_EQ_INT(BC_FORMAT_OK, bc_decode_header(legacy, sizeof(legacy), &h));
  ASSERT_TRUE(h.legacy); ASSERT_EQ_INT(2, h.instruction_offset);
  const uint8_t trunc[] = {0, 0xff, 'S'};
  ASSERT_EQ_INT(BC_FORMAT_TRUNCATED, bc_decode_header(trunc, sizeof(trunc), &h));
  const uint8_t magic[] = {0, 0xff, 'X', 'B', 1, 0, 0, 0};
  ASSERT_EQ_INT(BC_FORMAT_INVALID, bc_decode_header(magic, sizeof(magic), &h));
  const uint8_t version[] = {0, 0xff, 'S', 'B', 2, 0, 0, 0};
  ASSERT_EQ_INT(BC_FORMAT_UNSUPPORTED_VERSION, bc_decode_header(version, sizeof(version), &h));
  const uint8_t bad_v1[] = {0, 0xff, 'S', 'B', 1, 0, 0, 1};
  ASSERT_EQ_INT(BC_FORMAT_INVALID, bc_decode_header(bad_v1, sizeof(bad_v1), &h));
  const uint8_t bad_legacy[] = {0, 1, 'h'};
  ASSERT_EQ_INT(BC_FORMAT_INVALID, bc_decode_header(bad_legacy, sizeof(bad_legacy), &h));
}

void test_bytecode_verify_opcode_terminators_and_complete_buffer(void) {
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
                       "missing_halt", "final physical instruction must be HALT");

  const uint8_t intermediate_terminators[] = {0, 0, 'h', 'Q', 'h'};
  assert_verify_status(intermediate_terminators, sizeof(intermediate_terminators),
                       BC_VERIFY_OK, "intermediate_terminators", NULL);

  const uint8_t malformed_after_halt[] = {0, 0, 'h', 0x7F, 'h'};
  assert_verify_status(malformed_after_halt, sizeof(malformed_after_halt),
                       BC_VERIFY_ERROR, "malformed_after_halt", "invalid opcode");

  const uint8_t return_underflow[] = {0, 0, 'Q', 'h'};
  assert_verify_status(return_underflow, sizeof(return_underflow),
                       BC_VERIFY_ERROR, "return_underflow", "stack underflow");

  const uint8_t return_parameter[] = {1, 1, 'Q', 'h'};
  assert_verify_status(return_parameter, sizeof(return_parameter),
                       BC_VERIFY_ERROR, "return_parameter",
                       "exactly one value above parameter baseline");
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

  const uint8_t markerless_v1[] = {
      0, 0xff, 'S', 'B', 1, 0, 0, 0,
      'l', 1, 0, 'x', 'B', 1, 0, 'x', 'h'};
  assert_verify_status(markerless_v1, sizeof(markerless_v1), BC_VERIFY_ERROR,
                       "markerless_v1",
                       "embedded code is missing canonical parameter marker");

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

  const uint8_t truncated_build_list[] = {0, 0, '[', 1, 0, 0};
  assert_verify_status(truncated_build_list, sizeof(truncated_build_list),
                       BC_VERIFY_ERROR, "truncated_build_list",
                       "truncated BUILD_LIST");
}

void test_bytecode_verify_list_operations(void) {
  const uint8_t empty[] = {0, 0, '[', 0, 0, 0, 0, 'Q', 'h'};
  assert_verify_status(empty, sizeof(empty), BC_VERIFY_OK, "empty list", NULL);
  const uint8_t nonempty[] = {0, 0, 'b', 1, '[', 1, 0, 0, 0, 'Q', 'h'};
  assert_verify_status(nonempty, sizeof(nonempty), BC_VERIFY_OK, "nonempty list", NULL);
  const uint8_t over_limit[] = {0, 0, '[', 0x01, 0x00, 0x10, 0x00, 'h'};
  assert_verify_status(over_limit, sizeof(over_limit), BC_VERIFY_ERROR,
                       "list over limit", "list count exceeds maximum");
  const uint8_t underflow[] = {0, 0, '[', 1, 0, 0, 0, 'h'};
  assert_verify_status(underflow, sizeof(underflow), BC_VERIFY_ERROR,
                       "list underflow", "stack underflow");
  const uint8_t itemref_underflow[] = {0, 0, '&', 'h'};
  assert_verify_status(itemref_underflow, sizeof(itemref_underflow), BC_VERIFY_ERROR,
                       "itemref underflow", "stack underflow");
  const uint8_t merge[] = {0, 0, 'b', 1, 'k', 0x0c, 0x00,
                           'b', 1, '[', 1, 0, 0, 0, 'j', 0x02, 0x00,
                           'Q', 'h'};
  assert_verify_status(merge, sizeof(merge), BC_VERIFY_ERROR,
                       "list count flow", "conflicting stack depths");
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

  const uint8_t libcall_underflow[] = {0, 0, 'M', 1, 1, 'h'};
  assert_verify_status(libcall_underflow, sizeof(libcall_underflow),
                       BC_VERIFY_ERROR, "libcall_underflow",
                       "stack underflow");

  const uint8_t valid_libcall[] = {0, 0, 'l', 1, 0, 'x', 'M', 1, 1, 'h'};
  assert_verify_status(valid_libcall, sizeof(valid_libcall), BC_VERIFY_OK,
                       "valid_libcall", NULL);
  const uint8_t libcall_missing_both[] = {0, 0, 'M'};
  assert_verify_status(libcall_missing_both, sizeof(libcall_missing_both), BC_VERIFY_ERROR,
                       "libcall_missing_both", "truncated");
  const uint8_t libcall_missing_call[] = {0, 0, 'M', 1};
  assert_verify_status(libcall_missing_call, sizeof(libcall_missing_call), BC_VERIFY_ERROR,
                       "libcall_missing_call", "truncated");
  const uint8_t libcall_unknown[] = {0, 0, 'M', 5, 255, 'h'};
  assert_verify_status(libcall_unknown, sizeof(libcall_unknown), BC_VERIFY_ERROR,
                       "libcall_unknown", "unknown libcall pair");

  /* The conditional jump targets a reachable RETURN after an earlier HALT. */
  const uint8_t jump_after_terminator[] = {
      0, 0, 'b', 1, 'k', 0x03, 0x00, 'h',
      'p', 7, 0, 0, 0, 0, 0, 0, 0, 'Q', 'h'};
  assert_verify_status(jump_after_terminator, sizeof(jump_after_terminator),
                       BC_VERIFY_OK, "jump_after_terminator", NULL);

  const uint8_t return_depth_mismatch[] = {
      0, 0, 'b', 1, 'k', 0x0E, 0x00,
      'p', 7, 0, 0, 0, 0, 0, 0, 0,
      'j', 0x02, 0x00, 'Q', 'h'};
  assert_verify_status(return_depth_mismatch, sizeof(return_depth_mismatch),
                       BC_VERIFY_ERROR, "return_depth_mismatch",
                       "conflicting stack depths");
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
    if (result.status != BC_VERIFY_OK) {
      TEST_FAILF("fixture %s failed verification: %s", cases[i].fixture_path,
                 result.diagnostic.message);
    }
    ASSERT_EQ_INT(BC_VERIFY_OK, result.status);
    free(bytes);
  }
}

void test_bytecode_verify_compiler_emitted_bytecode(void) {
  const char *sources[] = {
      "return 42;",
      "@x = 7; return @x;",
      "if 1 < 2 then return 9; else return 7; endif;",
      "if 1 < 2 then return 9; elsif 0 < 1 then return 8; else return 6; endif;",
      "@x = 0; while @x < 2 do @x++; endwhile; return @x;",
      "@x = 0; do @x++; while @x < 2; return @x;",
      "foo.12;",
      "add = code {@a, @b} ( return @a + @b; );",
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
