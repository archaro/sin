#include <stdlib.h>
#include <string.h>

#include "compiler_pipeline.h"
#include "error.h"
#include "test_assert.h"
#include "test_helpers.h"

void test_scomp_e2e_golden(void) {
  const char *source = "42;";
  const char *hex_fixture = "tests/fixtures/int_literal.hex";

  compile_source_and_assert_hex(source, hex_fixture);
}
