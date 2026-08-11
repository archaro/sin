#include "bytecode_convert.h"
#include "bytecode_verify.h"
#include "memory.h"
#include "test_assert.h"
#include "test_helpers.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

static bool contains_bytes(const uint8_t *haystack, size_t hay_len,
                           const uint8_t *needle, size_t needle_len) {
  for (size_t i = 0; i + needle_len <= hay_len; i++)
    if (memcmp(haystack + i, needle, needle_len) == 0)
      return true;
  return false;
}

void test_bytecode_convert_legacy_and_v1(void) {
  size_t legacy_len = 0, expected_len = 0;
  uint8_t *legacy = load_hex_fixture(
      "tests/fixtures/bytecode-migration/legacy-0.7.1.hex", &legacy_len);
  uint8_t *expected = load_hex_fixture(
      "tests/fixtures/bytecode-migration/v1.hex", &expected_len);
  BC_ConvertResult converted = bc_convert_latest(legacy, (uint32_t)legacy_len);
  ASSERT_EQ_INT(BC_CONVERT_SUCCESS, converted.status);
  assert_bytes_equal_with_diag(expected, expected_len, converted.data,
                               converted.length, "rich migration fixture");
  ASSERT_EQ_INT(BC_VERIFY_OK,
                bc_verify_executable_bytecode(converted.data,
                                              converted.length, "test")
                    .status);
  /* M token zero is the historical (library 1, call 0) permanent pair. */
  const uint8_t expected_m[] = {'M', 1, 0};
  ASSERT_TRUE(contains_bytes(converted.data, converted.length, expected_m,
                             sizeof expected_m));
  /* Operand-relative jumps relocate from +0x35/-49 to +0x36/-50. */
  ASSERT_EQ_INT(0x36, converted.data[9]);
  ASSERT_EQ_INT(0x00, converted.data[10]);
  ASSERT_EQ_INT(0xce, converted.data[converted.length - 3]);
  ASSERT_EQ_INT(0xff, converted.data[converted.length - 2]);
  bc_convert_result_free(&converted);
  free(legacy);
  free(expected);
}

void test_bytecode_convert_malformed_matrix(void) {
  const struct {
    const uint8_t *bytes;
    size_t length;
    BC_ConvertStatus status;
  } cases[] = {
      {(const uint8_t[]){0}, 1, BC_CONVERT_TRUNCATED},
      {(const uint8_t[]){1, 2}, 2, BC_CONVERT_INVALID},
      {(const uint8_t[]){0, 0xff, 'S'}, 3, BC_CONVERT_TRUNCATED},
      {(const uint8_t[]){0, 0xff, 'S', 'B', 2, 0, 0, 0, 'h'}, 9,
       BC_CONVERT_UNSUPPORTED_VERSION},
      {(const uint8_t[]){0, 0, '!', 'h'}, 4, BC_CONVERT_INVALID},
      {(const uint8_t[]){0, 0, 'M', 99, 'h'}, 5, BC_CONVERT_INVALID},
      {(const uint8_t[]){0, 0, 'p', 1, 2, 'h'}, 6, BC_CONVERT_INVALID},
      {(const uint8_t[]){0, 0, 'j', 1, 0, 'h'}, 6, BC_CONVERT_INVALID},
  };
  for (size_t i = 0; i < sizeof cases / sizeof cases[0]; i++) {
    BC_ConvertResult r =
        bc_convert_latest(cases[i].bytes, (uint32_t)cases[i].length);
    ASSERT_EQ_INT(cases[i].status, r.status);
    ASSERT_TRUE(r.data == NULL);
    bc_convert_result_free(&r);
  }
}

void test_bytecode_convert_v1_idempotent(void) {
  size_t fixture_len = 0;
  uint8_t *fixture = load_hex_fixture("tests/fixtures/bytecode-migration/v1.hex",
                                      &fixture_len);
  const uint8_t v1[] = {0, 0xff, 'S', 'B', 1, 0, 1, 0, 'b', 1, 'Q', 'h'};
  BC_ConvertResult a = bc_convert_latest(v1, sizeof v1);
  ASSERT_EQ_INT(BC_CONVERT_SUCCESS, a.status);
  ASSERT_EQ_INT((int)sizeof v1, (int)a.length);
  ASSERT_TRUE(memcmp(v1, a.data, sizeof v1) == 0);
  BC_ConvertResult b = bc_convert_latest(a.data, a.length);
  ASSERT_EQ_INT(BC_CONVERT_SUCCESS, b.status);
  ASSERT_EQ_INT((int)a.length, (int)b.length);
  ASSERT_TRUE(memcmp(a.data, b.data, a.length) == 0);
  BC_ConvertResult fixture_result = bc_convert_latest(fixture, (uint32_t)fixture_len);
  ASSERT_EQ_INT(BC_CONVERT_SUCCESS, fixture_result.status);
  assert_bytes_equal_with_diag(fixture, fixture_len, fixture_result.data,
                               fixture_result.length, "v1 fixture idempotence");
  bc_convert_result_free(&fixture_result);
  free(fixture);
  bc_convert_result_free(&b);
  bc_convert_result_free(&a);
}

void test_bytecode_convert_legacy_token_boundaries(void) {
  const struct {
    uint8_t token, lib, call;
  } cases[] = {
      {0, 1, 0},  {24, 1, 24}, {25, 5, 0},  {30, 5, 5}, {31, 3, 0},
      {37, 3, 6}, {38, 4, 0},  {55, 4, 17}, {56, 2, 0}, {60, 2, 4},
  };
  for (size_t i = 0; i < sizeof cases / sizeof cases[0]; i++) {
    const uint8_t legacy[] = {0, 0, 'j', 4, 0, 'M', cases[i].token, 'h'};
    BC_ConvertResult result = bc_convert_latest(legacy, sizeof legacy);
    ASSERT_EQ_INT(BC_CONVERT_SUCCESS, result.status);
    const uint8_t pair[] = {'M', cases[i].lib, cases[i].call};
    ASSERT_TRUE(contains_bytes(result.data, result.length, pair, sizeof pair));
    bc_convert_result_free(&result);
  }
  const uint8_t invalid[] = {0, 0, 'M', 61, 'h'};
  BC_ConvertResult rejected = bc_convert_latest(invalid, sizeof invalid);
  ASSERT_EQ_INT(BC_CONVERT_INVALID, rejected.status);
  ASSERT_TRUE(rejected.data == NULL);
  bc_convert_result_free(&rejected);
}

void test_bytecode_convert_allocation_failures(void) {
  const uint8_t legacy[] = {0, 0, 'h'};
  alloc_test_fail_after(1);
  BC_ConvertResult output = bc_convert_latest(legacy, sizeof legacy);
  alloc_test_fail_after(-1);
  ASSERT_EQ_INT(BC_CONVERT_ALLOCATION_FAILURE, output.status);
  ASSERT_TRUE(output.data == NULL && output.length == 0);
  bc_convert_result_free(&output);

  alloc_test_fail_after(0);
  BC_ConvertResult pair = bc_convert_latest(legacy, sizeof legacy);
  alloc_test_fail_after(-1);
  ASSERT_EQ_INT(BC_CONVERT_ALLOCATION_FAILURE, pair.status);
  ASSERT_TRUE(pair.data == NULL && pair.length == 0);
  bc_convert_result_free(&pair);

  const uint8_t jump_legacy[] = {0, 0, 'j', 3, 0, 'h'};
  alloc_test_fail_after(1);
  BC_ConvertResult jump = bc_convert_latest(jump_legacy, sizeof jump_legacy);
  alloc_test_fail_after(-1);
  ASSERT_EQ_INT(BC_CONVERT_ALLOCATION_FAILURE, jump.status);
  ASSERT_TRUE(jump.data == NULL && jump.length == 0);
  bc_convert_result_free(&jump);

  const uint8_t v1[] = {0, 0xff, 'S', 'B', 1, 0, 0, 0, 'h'};
  size_t work_used = 0;
  bool budget_exhausted = false;
  BC_ConvertResult budget = bc_convert_latest_with_limits(
      v1, sizeof v1, UINT32_MAX, &work_used, sizeof v1 - 1u,
      &budget_exhausted);
  ASSERT_EQ_INT(BC_CONVERT_ALLOCATION_FAILURE, budget.status);
  ASSERT_TRUE(budget_exhausted);
  ASSERT_EQ_INT(sizeof v1, budget.budget_request);
  ASSERT_EQ_INT(0, work_used);
  bc_convert_result_free(&budget);
}

void test_embedded_code_conversion_boundaries(void) {
  const uint8_t markerless[] = {
      0, 0, 'l', 1, 0, 'x', 'B', 9, 0,
      'r', 'e', 't', 'u', 'r', 'n', ' ', '7', ';', 'h'};
  BC_ConvertResult upgraded = bc_convert_latest(markerless,
                                                 sizeof(markerless));
  ASSERT_EQ_INT(BC_CONVERT_SUCCESS, upgraded.status);
  const uint8_t canonical[] = {'B', 'P', 0, 0, 9, 0};
  ASSERT_TRUE(contains_bytes(upgraded.data, upgraded.length, canonical,
                             sizeof(canonical)));
  ASSERT_EQ_INT(BC_VERIFY_OK,
                bc_verify_executable_bytecode(
                    upgraded.data, upgraded.length,
                    "upgraded markerless embedded code").status);
  bc_convert_result_free(&upgraded);

  const uint32_t lengths[] = {0x50u, 0x150u, 0xff50u};
  for (size_t i = 0; i < sizeof lengths / sizeof lengths[0]; i++) {
    uint32_t src_len = lengths[i];
    size_t legacy_len = 2u + 1u + 2u + src_len + 1u;
    uint8_t *legacy = malloc(legacy_len);
    ASSERT_NOT_NULL(legacy);
    legacy[0] = 0; legacy[1] = 0; legacy[2] = 'B';
    legacy[3] = (uint8_t)src_len;
    legacy[4] = (uint8_t)(src_len >> 8);
    memset(legacy + 5, 'x', src_len);
    legacy[5 + src_len] = 'h';
    BC_ConvertResult converted = bc_convert_latest(legacy, (uint32_t)legacy_len);
    /* Historical markerless lengths with low byte 0x50 are ambiguous; the
     * converter rejects them deterministically instead of guessing. */
    ASSERT_EQ_INT(BC_CONVERT_INVALID, converted.status);
    ASSERT_TRUE(converted.data == NULL && converted.length == 0);
    bc_convert_result_free(&converted);
    free(legacy);
  }
}

static uint8_t *make_jump_chain(size_t jumps, size_t *length) {
  size_t n = 2u + 2u + jumps * 3u + 1u;
  uint8_t *bytes = malloc(n);
  if (!bytes) return NULL;
  bytes[0] = 0;
  bytes[1] = 0;
  bytes[2] = 'b';
  bytes[3] = 1u;
  size_t p = 4u;
  for (size_t i = 0; i < jumps; i++) {
    bytes[p++] = 'j';
    bytes[p++] = 2u;
    bytes[p++] = 0u;
  }
  bytes[p++] = 'h';
  *length = p;
  return bytes;
}

void test_bytecode_convert_jump_lookup_scaling(void) {
  const size_t counts[] = {10000u, 20000u, 40000u};
  size_t probes[sizeof counts / sizeof counts[0]];
  for (size_t i = 0; i < sizeof counts / sizeof counts[0]; i++) {
    size_t length = 0;
    uint8_t *legacy = make_jump_chain(counts[i], &length);
    ASSERT_NOT_NULL(legacy);
    bc_convert_test_reset_lookup_probes();
    BC_ConvertResult result = bc_convert_latest(legacy, (uint32_t)length);
    ASSERT_EQ_INT(BC_CONVERT_SUCCESS, result.status);
    probes[i] = bc_convert_test_lookup_probes();
    ASSERT_TRUE(probes[i] < counts[i] * 20u);
    bc_convert_result_free(&result);
    free(legacy);
  }
  ASSERT_TRUE(probes[1] < probes[0] * 3u);
  ASSERT_TRUE(probes[2] < probes[1] * 3u);
}
