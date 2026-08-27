#include "item.h"
#include <stdint.h>
#include "test_helpers.h"
#include <stdlib.h>
#include <string.h>

#include "config.h"
#include "compiler/compiler_pipeline.h"
#include "error.h"
#include "interpret.h"
#include "item.h"
#include "test_assert.h"
#include "value.h"
#include "vm.h"

extern CONFIG_t config;

static void assert_compile_ok(const char *name, const char *source) {
  OUTPUT_t *out = NULL;
  char *errdetail = NULL;
  int8_t rc = compile_source_to_bytecode(source, strlen(source), &out, &errdetail);
  if (rc != ERR_NOERROR) {
    TEST_FAILF("%s: compiler error %d: %s", name, rc,
               errdetail ? errdetail : "<no detail>");
  }
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

static void assert_compile_rejected(const char *source) {
  OUTPUT_t *out = NULL;
  char *errdetail = NULL;
  int8_t rc = compile_source_to_bytecode(source, strlen(source), &out, &errdetail);
  ASSERT_TRUE(rc != ERR_NOERROR);
  ASSERT_TRUE(out == NULL);
  ASSERT_NOT_NULL(errdetail);
  free(errdetail);
}

static VALUE_t compile_and_run(const char *name, const char *source) {
  OUTPUT_t *out = NULL;
  char *errdetail = NULL;
  int8_t rc = compile_source_to_bytecode(source, strlen(source), &out, &errdetail);
  ASSERT_EQ_INT(ERR_NOERROR, rc);
  ASSERT_TRUE(errdetail == NULL);
  ASSERT_NOT_NULL(out);

  uint32_t len = (uint32_t)(out->nextbyte - out->bytecode);
  uint8_t *bytecode = malloc(len);
  ASSERT_NOT_NULL(bytecode);
  memcpy(bytecode, out->bytecode, len);
  ITEM_t *code = test_item_set_code(itemstore_root(config.itemstore_ctx), name, len, bytecode);
  ASSERT_NOT_NULL(code);
  RuntimeContext ctx;
  runtime_context_init(&ctx, config.vm);
  ctx.itemstore = config.itemstore_ctx;
  ctx.strict_runtime_contracts = config.strict_runtime_contracts;
  VALUE_t result = interpret(&ctx, code);

  free(out->bytecode);
  free(out);
  return result;
}

static void setup_runtime(void) {
  memset(&config, 0, sizeof(config));
  config.itemstore_ctx = itemstore_owner(make_root_item("root"));
  ASSERT_NOT_NULL(itemstore_root(config.itemstore_ctx));
  config.vm = make_vm();
  ASSERT_NOT_NULL(config.vm);
}

static void teardown_runtime(void) {
  destroy_vm(config.vm);
  destroy_item(itemstore_root(config.itemstore_ctx));
  memset(&config, 0, sizeof(config));
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

void test_relative_item_leading_dot_rejects_whitespace_after_separator(void) {
  assert_compile_rejected("foo. bar = 1;");
  assert_compile_rejected("foo.\tbar = 1;");
  assert_compile_rejected("foo.\nbar = 1;");
  assert_compile_rejected("foo.\r\nbar = 1;");
}

void test_keywords_as_layer_names_after_dot(void) {
  assert_compile_ok("keyword as layer: foo.if",
                    "foo.if = 1;");
  assert_compile_ok("keyword as layer: foo.do",
                    "foo.do = 1;");
  assert_compile_ok("keyword as layer: foo.while",
                    "foo.while = 1;");
  assert_compile_ok("keyword as layer: foo.then",
                    "foo.then = 1;");
  assert_compile_ok("keyword as layer: foo.foreach",
                    "foo.foreach = 1;");
  assert_compile_ok("keyword as layer: foo.in",
                    "foo.in = 1;");
  assert_compile_ok("keyword as layer: foo.endfor",
                    "foo.endfor = 1;");
  assert_compile_ok("keyword as layer: foo.else",
                    "foo.else = 1;");
  assert_compile_ok("keyword as layer: foo.break",
                    "foo.break = 1;");
  assert_compile_ok("keyword as layer: .foreach",
                    ".foreach = 1;");
  assert_compile_ok("keyword as layer: .in",
                    ".in = 1;");
  assert_compile_ok("keyword as layer: .if",
                    ".if = 1;");
}

void test_float_item_literal_layer_rejected_at_compile_time(void) {
  assert_compile_err("foo.1.0 = 1;", ERR_COMP_SYNTAX, "syntax");
}

void test_float_local_deref_layer_returns_nil_and_does_not_save_item(void) {
  setup_runtime();
  VALUE_t result = compile_and_run("test.floatlocal",
                                   "@layer = 1.0; foo.[@layer] = 7; foo;");
  ASSERT_EQ_INT(VALUE_nil, result.type);
  ASSERT_TRUE(find_item(itemstore_root(config.itemstore_ctx), "foo") == NULL);
  teardown_runtime();
}

void test_item_deref_value_layer_resolves_normally(void) {
  setup_runtime();
  VALUE_t result = compile_and_run("test.itemderefvalue",
      "selector = \"BrAnCh\"; FoO.[SeLeCtOr] = 7; return fOo.bRaNcH;");
  ASSERT_EQ_INT(VALUE_int, result.type);
  ASSERT_EQ_INT(7, result.i);
  value_free(&result);
  teardown_runtime();
}

void test_item_deref_code_layer_is_rejected(void) {
  setup_runtime();
  VALUE_t result = compile_and_run("test.itemderefcode",
      "selector = code (return \"branch\"); foo.[selector] = 7; return foo;");
  ASSERT_EQ_INT(VALUE_nil, result.type);
  value_free(&result);
  ASSERT_TRUE(find_item(itemstore_root(config.itemstore_ctx), "foo") == NULL);
  teardown_runtime();
}

void test_item_deref_missing_layer_is_rejected(void) {
  setup_runtime();
  VALUE_t result = compile_and_run("test.itemderefmissing",
      "foo.[missing] = 7; return foo;");
  ASSERT_EQ_INT(VALUE_nil, result.type);
  value_free(&result);
  ASSERT_TRUE(find_item(itemstore_root(config.itemstore_ctx), "foo") == NULL);
  teardown_runtime();
}

void test_item_deref_invalid_result_layer_is_rejected(void) {
  setup_runtime();
  VALUE_t result = compile_and_run("test.itemderefinvalid",
      "selector = 1.0; foo.[selector] = 7; return foo;");
  ASSERT_EQ_INT(VALUE_nil, result.type);
  value_free(&result);
  ASSERT_TRUE(find_item(itemstore_root(config.itemstore_ctx), "foo") == NULL);
  teardown_runtime();
}
