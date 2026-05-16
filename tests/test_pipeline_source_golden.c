#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "error.h"
#include "compiler_pipeline.h"
#include "parser.h"
#include "test_assert.h"
#include "test_helpers.h"

typedef struct {
  const char *name;
  const char *source;
  const char *fixture_path;
} SourceGoldenCase;

static void run_source_case(const SourceGoldenCase *tc) {
  char *errdetail = NULL;
  OUTPUT_t *out = NULL;
  int8_t rc = compile_source_to_bytecode(tc->source, strlen(tc->source), &out, &errdetail);
  ASSERT_EQ_INT(ERR_NOERROR, rc);
  ASSERT_TRUE(errdetail == NULL);
  ASSERT_NOT_NULL(out);

  size_t expected_len = 0;
  uint8_t *expected = load_hex_fixture(tc->fixture_path, &expected_len);
  size_t actual_len = (size_t)(out->nextbyte - out->bytecode);
  ASSERT_EQ_INT((int)expected_len, (int)actual_len);
  ASSERT_EQ_INT(0, memcmp(expected, out->bytecode, expected_len));

  free(expected);
  free(out->bytecode);
  free(out);
}

static void test_source_pipeline_negative_cases(void) {
  AS_NODE *absyn = NULL;
  char *errdetail = NULL;

  const char *bad_char = "^;";
  int8_t rc = parse_source((char *)bad_char, (int)strlen(bad_char), &absyn, &errdetail);
  ASSERT_EQ_INT(ERR_COMP_UNKNOWNCHAR, rc);
  ASSERT_NOT_NULL(errdetail);
  ASSERT_TRUE(strcmp(errdetail, "^") == 0);
  free(errdetail);

  OUTPUT_t *out = NULL;
  const char *bad_semantic = "@x;";
  rc = compile_source_to_bytecode(bad_semantic, strlen(bad_semantic), &out, &errdetail);
  ASSERT_EQ_INT(ERR_COMP_LOCALBEFOREDEF, rc);
  ASSERT_NOT_NULL(errdetail);
  ASSERT_TRUE(strstr(errdetail, "x") != NULL);
  free(errdetail);

  errdetail = NULL;
  const char *bad_inc_semantic = "@y++;";
  rc = compile_source_to_bytecode(bad_inc_semantic, strlen(bad_inc_semantic), &out, &errdetail);
  ASSERT_EQ_INT(ERR_COMP_LOCALBEFOREDEF, rc);
  ASSERT_NOT_NULL(errdetail);
  ASSERT_TRUE(strstr(errdetail, "y") != NULL);
  free(errdetail);
}


static void test_source_exprstmt_libcall_no_pop(void) {
  char *errdetail = NULL;
  const char *source = "sys.log{\"hello\"};";
  OUTPUT_t *out = NULL;

  int8_t rc = compile_source_to_bytecode(source, strlen(source), &out, &errdetail);
  ASSERT_EQ_INT(ERR_NOERROR, rc);
  ASSERT_TRUE(errdetail == NULL);
  ASSERT_NOT_NULL(out);

  static const uint8_t expected[] = {
      0x00, 0x00,
      0x6c, 0x05, 0x00, 'h', 'e', 'l', 'l', 'o',
      0x41, 0x01, 0x01,
      0x68,
  };
  size_t n = (size_t)(out->nextbyte - out->bytecode);
  ASSERT_EQ_INT((int)sizeof(expected), (int)n);
  ASSERT_EQ_INT(0, memcmp(expected, out->bytecode, n));

  free(out->bytecode);
  free(out);
}

static void test_source_item_with_numeric_layer(void) {
  char *errdetail = NULL;
  const char *source = "foo.12;";
  OUTPUT_t *out = NULL;

  int8_t rc = compile_source_to_bytecode(source, strlen(source), &out, &errdetail);
  ASSERT_EQ_INT(ERR_NOERROR, rc);
  ASSERT_TRUE(errdetail == NULL);
  ASSERT_NOT_NULL(out);

  static const uint8_t expected[] = {
      0x00, 0x00,
      'I',
      'L', 0x03, 'f', 'o', 'o',
      'L', 0x02, '1', '2',
      'E',
      'F', 0x00, 0x00,
      'h',
  };
  size_t n = (size_t)(out->nextbyte - out->bytecode);
  ASSERT_EQ_INT((int)sizeof(expected), (int)n);
  ASSERT_EQ_INT(0, memcmp(expected, out->bytecode, n));

  free(out->bytecode);
  free(out);
}

void test_pipeline_source_golden(void) {
  const SourceGoldenCase cases[] = {
      {"int_literal", "42;", "tests/fixtures/int_literal.hex"},
      {"locals_store_load", "@x = 7; @x;", "tests/fixtures/locals_store_load.hex"},
      {"arithmetic_add", "2 + 3;", "tests/fixtures/arithmetic_add.hex"},
      {"if_elsif_else", "if 1 < 2 then 9; elsif 0 < 1 then 8; else 7; endif;", "tests/fixtures/if_elsif_else.hex"},
      {"locals_inc", "@x = 1; @x++; @x;", "tests/fixtures/locals_inc.hex"},
      {"locals_dec", "@x = 2; @x--; @x;", "tests/fixtures/locals_dec.hex"},
  };

  for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
    run_source_case(&cases[i]);
  }

  test_source_pipeline_negative_cases();
  test_source_exprstmt_libcall_no_pop();
  test_source_item_with_numeric_layer();
}
