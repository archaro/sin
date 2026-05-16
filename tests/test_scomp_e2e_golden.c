#include <stdlib.h>
#include <string.h>

#include "compiler_pipeline.h"
#include "error.h"
#include "test_assert.h"
#include "test_helpers.h"

void test_scomp_e2e_golden(void) {
  const char *source = "42;";
  const char *hex_fixture = "tests/fixtures/int_literal.hex";

  char *errdetail = NULL;
  OUTPUT_t *out = NULL;
  int8_t rc = compile_source_to_bytecode(source, strlen(source), &out, &errdetail);
  ASSERT_EQ_INT(ERR_NOERROR, rc);
  ASSERT_TRUE(errdetail == NULL);
  ASSERT_NOT_NULL(out);

  size_t expected_len = 0;
  uint8_t *expected = load_hex_fixture(hex_fixture, &expected_len);
  size_t actual_len = (size_t)(out->nextbyte - out->bytecode);
  ASSERT_EQ_INT((int)expected_len, (int)actual_len);
  ASSERT_EQ_INT(0, memcmp(expected, out->bytecode, expected_len));

  free(expected);
  free(out->bytecode);
  free(out);
}
