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
  (void)tc->name;
  compile_source_and_assert_hex(tc->source, tc->fixture_path);
}

static void test_source_pipeline_negative_cases(void) {
  AS_NODE *absyn = NULL;
  char *errdetail = NULL;

  const char *bad_char = "^;";
  ParseInput input = {bad_char, strlen(bad_char), "<test>"};
  int8_t rc = parse_source(&input, &absyn, &errdetail);
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

static void test_source_pipeline_bool_and_truthiness_nonregression(void) {
  const char *ok_cases[] = {
      "@l = true;",
      "foo.bar = false;",
      "@l = false; @l == false;",
      "@l = false; if @l == false then sys.log{\"False\"}; endif;",
      "if 1 then sys.log{\"int truthy\"}; endif;",
      "if \"\" then sys.log{\"empty string truthy\"}; endif;",
  };

  for (size_t i = 0; i < sizeof(ok_cases) / sizeof(ok_cases[0]); i++) {
    OUTPUT_t *out = NULL;
    char *errdetail = NULL;
    int8_t rc = compile_source_to_bytecode(ok_cases[i], strlen(ok_cases[i]), &out, &errdetail);
    ASSERT_EQ_INT(ERR_NOERROR, rc);
    ASSERT_TRUE(errdetail == NULL);
    ASSERT_NOT_NULL(out);
    free(out->bytecode);
    free(out);
  }
}

void test_pipeline_source_golden(void) {
  const SourceGoldenCase cases[] = {
      {"int_literal", "42;", "tests/fixtures/int_literal.hex"},
      {"locals_store_load", "@x = 7; @x;", "tests/fixtures/locals_store_load.hex"},
      {"arithmetic_add", "2 + 3;", "tests/fixtures/arithmetic_add.hex"},
      {"if_elsif_else", "if 1 < 2 then 9; elsif 0 < 1 then 8; else 7; endif;", "tests/fixtures/if_elsif_else.hex"},
      {"locals_inc", "@x = 1; @x++; @x;", "tests/fixtures/locals_inc.hex"},
      {"locals_dec", "@x = 2; @x--; @x;", "tests/fixtures/locals_dec.hex"},
      {"libcall_exprstmt", "sys.log{\"hello\"};", "tests/fixtures/libcall_exprstmt.hex"},
      {"item_numeric_layer", "foo.12;", "tests/fixtures/item_numeric_layer.hex"},
  };

  for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
    run_source_case(&cases[i]);
  }

  test_source_pipeline_negative_cases();
  test_source_pipeline_bool_and_truthiness_nonregression();
}
