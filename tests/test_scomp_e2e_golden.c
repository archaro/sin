#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "test_assert.h"
#include "test_helpers.h"

static size_t file_size(FILE *f) {
  ASSERT_EQ_INT(0, fseek(f, 0, SEEK_END));
  long n = ftell(f);
  ASSERT_TRUE(n >= 0);
  ASSERT_EQ_INT(0, fseek(f, 0, SEEK_SET));
  return (size_t)n;
}

void test_scomp_e2e_golden(void) {
  const char *out = "tests/fixtures/int_literal.e2e.bin";
  const char *hex_fixture = "tests/fixtures/int_literal.hex";

  int rc = system("./scomp tests/fixtures/int_literal.src tests/fixtures/int_literal.e2e.bin");
  ASSERT_EQ_INT(0, rc);

  size_t expected_len = 0;
  uint8_t *expected = load_hex_fixture(hex_fixture, &expected_len);

  FILE *f = fopen(out, "rb");
  ASSERT_NOT_NULL(f);
  size_t actual_len = file_size(f);
  ASSERT_EQ_INT((int)expected_len, (int)actual_len);

  uint8_t *actual = malloc(actual_len);
  ASSERT_NOT_NULL(actual);
  size_t read = fread(actual, 1, actual_len, f);
  ASSERT_EQ_INT((int)actual_len, (int)read);
  fclose(f);

  ASSERT_EQ_INT(0, memcmp(expected, actual, expected_len));

  free(actual);
  free(expected);
  remove(out);
}
