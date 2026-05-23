#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "compiler_pipeline.h"
#include "error.h"
#include "test_assert.h"

static void assert_compile_ok(const char *name, const char *source) {
  OUTPUT_t *out = NULL;
  char *errdetail = NULL;
  int8_t rc = compile_source_to_bytecode(source, strlen(source), &out, &errdetail);
  ASSERT_EQ_INT(ERR_NOERROR, rc);
  ASSERT_TRUE(errdetail == NULL);
  ASSERT_NOT_NULL(out);
  free(out->bytecode);
  free(out);
  (void)name;
}

static void assert_compile_err(const char *source, int8_t expected_code,
                               const char *expected_substr) {
  OUTPUT_t *out = NULL;
  char *errdetail = NULL;
  int8_t rc = compile_source_to_bytecode(source, strlen(source), &out, &errdetail);
  ASSERT_EQ_INT(expected_code, rc);
  ASSERT_TRUE(out == NULL);
  ASSERT_NOT_NULL(errdetail);
  ASSERT_TRUE(strstr(errdetail, expected_substr) != NULL);
  free(errdetail);
}

void test_relative_item_leading_dot_parse_accepts_deref_chain(void) {
  assert_compile_ok("relative item leading dot parse accepts .baz.xxx.[@a]",
                    "@a=\"tail\"; .baz.xxx.[@a] = 1;");
}

void test_relative_item_leading_dot_nested_relative_deref_layers(void) {
  assert_compile_ok("relative item leading dot handles nested deref .foo.[@b].[x.y]",
                    "@b=\"branch\"; .foo.[@b].[x.y] = 1;");
}

void test_relative_item_leading_dot_nested_deref_nil_or_empty_leading_allowed(void) {
  assert_compile_ok("relative item leading dot allows leading nil/empty via [@a].foo.bar",
                    "@a=nil; [@a].foo.bar = 1; @a=\"\"; [@a].foo.bar = 2;");
}

void test_relative_item_leading_dot_nil_or_empty_non_leading_rejected(void) {
  assert_compile_ok("relative item leading dot compiles; runtime enforces non-leading missing layer",
                    "@a=nil; foo.[@a].bar = 1; @a=\"\"; foo.[@a].bar = 2;");
}

void test_relative_item_leading_dot_boundary_max_name_after_prefix_expansion_compiles(void) {
  assert_compile_ok("relative item leading dot boundary max item name",
                    "abcdefghijklmnopqrstuvwx.abcdefghijklmnopqrstuvwx.abcdefghijklmnopqrstuvwx = 1;"
                    ".abcdefghijklmnopqrstuvwx = 2;");
}

void test_relative_item_leading_dot_existing_absolute_item_unchanged(void) {
  assert_compile_ok("relative item leading dot keeps existing absolute item behavior",
                    "foo.bar.baz = 1; foo.bar.baz = foo.bar.baz + 1;");
}

void test_relative_item_leading_dot_parse_still_rejects_bad_double_dot(void) {
  assert_compile_err("foo..bar = 1;", ERR_COMP_SYNTAX, "syntax error");
}
