#include "item.h"
#include "item_internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

#include "config.h"
#include "compiler/compiler_pipeline.h"
#include "bytecode_convert.h"
#include "error.h"
#include "interpret.h"
#include "item.h"
#include "test_assert.h"
#include "test_helpers.h"
#include "value.h"
#include "vm.h"
#include "list.h"
#include "itemref.h"
#include "memory.h"

#undef destroy_item
static void destroy_raw_runtime_item(ITEM_t *item) { destroy_item(item); }
#define destroy_item test_destroy_item

uint8_t *op_jump(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item);

extern CONFIG_t config;
extern uint8_t *op_build_list(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item);
extern uint8_t *op_pushstr(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item);

typedef struct {
  const char *name;
  const char *src_path;
  const char *fixture_path;
  const char *expected_code_items[8];
  unsigned runs;
} InterpretGoldenCase;

static void normalize_runtime_path(char *text, const char *path,
                                   const char *replacement) {
  if (!text || !path || !replacement) return;
  size_t path_len = strlen(path);
  size_t replacement_len = strlen(replacement);
  char *match = NULL;
  while ((match = strstr(text, path)) != NULL) {
    memcpy(match, replacement, replacement_len);
    memmove(match + replacement_len, match + path_len,
            strlen(match + path_len) + 1u);
  }
}

static void assert_run_matches(const char *case_name, const char *variant,
                               const TestProcessResult *actual,
                               const TestProcessResult *expected) {
  if (expected->exit_code >= 0 && actual->exit_code != expected->exit_code) {
    fprintf(stderr,
            "[%s/%s] mismatch exit code: expected=%d actual=%d\n",
            case_name, variant, expected->exit_code, actual->exit_code);
    ASSERT_EQ_INT(expected->exit_code, actual->exit_code);
  }

  int missing_line = -1;
  if (!test_contains_all_lines(expected->stdout_text, actual->stdout_text,
                               &missing_line)) {
    fprintf(stderr,
            "[%s/%s] mismatch stdout: expected marker line %d not found\n",
            case_name, variant, missing_line);
    ASSERT_TRUE(0);
  }

  missing_line = -1;
  bool stderr_matches = expected->stderr_text[0] == '\0'
      ? actual->stderr_text[0] == '\0'
      : test_contains_all_lines(expected->stderr_text, actual->stderr_text,
                                &missing_line);
  if (!stderr_matches) {
    fprintf(stderr,
            "[%s/%s] mismatch stderr: expected marker line %d not found\n",
            case_name, variant, missing_line);
    ASSERT_TRUE(0);
  }
}

static void assert_persisted_code_items(const InterpretGoldenCase *tc,
                                        const char *itemstore_path) {
  if (!tc->expected_code_items[0]) return;

  ITEM_t *root = load_itemstore_with_options(itemstore_path, true);
  ASSERT_NOT_NULL(root);
  for (size_t i = 0; i < sizeof(tc->expected_code_items) /
                         sizeof(tc->expected_code_items[0]); i++) {
    const char *name = tc->expected_code_items[i];
    if (!name) continue;
    ITEM_t *item = find_item(root, name);
    ASSERT_NOT_NULL(item);
    ASSERT_EQ_INT(ITEM_code, item_kind(item));
  }
  destroy_item(root);
}

static void remove_persisted_source_files(const InterpretGoldenCase *tc,
                                          const char *srcroot_path) {
  for (size_t i = 0; i < sizeof(tc->expected_code_items) /
                         sizeof(tc->expected_code_items[0]); i++) {
    const char *name = tc->expected_code_items[i];
    if (!name) continue;
    char item_dir[512];
    char source_path[sizeof(item_dir) + sizeof("/source.sin")];
    int written = snprintf(item_dir, sizeof(item_dir), "%s/%s",
                           srcroot_path, name);
    ASSERT_TRUE(written > 0 && (size_t)written < sizeof(item_dir));
    written = snprintf(source_path, sizeof(source_path), "%s/source.sin",
                       item_dir);
    ASSERT_TRUE(written > 0 && (size_t)written < sizeof(source_path));
    ASSERT_EQ_INT(0, remove(source_path));
    ASSERT_EQ_INT(0, rmdir(item_dir));
  }
}

static void run_case(const InterpretGoldenCase *tc) {
  char generated_obj_path[128];
  ASSERT_EQ_INT(0, test_make_temp_path("sin-interp-golden", generated_obj_path,
                                       sizeof(generated_obj_path)));
  char *const compile_argv[] = {
    "./scomp", (char *)tc->src_path, generated_obj_path, NULL
  };
  TestProcessResult compile_result = {0};
  int capture_rc = test_run_argv_capture(compile_argv, 0, &compile_result);
  int compile_exit = compile_result.exit_code;
  test_process_result_free(&compile_result);
  if (capture_rc != 0) remove(generated_obj_path);
  ASSERT_EQ_INT(0, capture_rc);
  if (compile_exit != 0) remove(generated_obj_path);
  ASSERT_EQ_INT(0, compile_exit);

  char run_dir[] = "/tmp/sin-interp-golden-run-XXXXXX";
  ASSERT_NOT_NULL(mkdtemp(run_dir));
  char itemstore_path[sizeof(run_dir) + sizeof("/items.dat")];
  char srcroot_path[sizeof(run_dir) + sizeof("/srcroot")];
  ASSERT_TRUE(snprintf(itemstore_path, sizeof(itemstore_path), "%s/items.dat",
                       run_dir) > 0);
  ASSERT_TRUE(snprintf(srcroot_path, sizeof(srcroot_path), "%s/srcroot",
                       run_dir) > 0);
  ASSERT_EQ_INT(0, mkdir(srcroot_path, 0700));
  char *const run_argv[] = {
    "./sin", "--loadonly", "-i", itemstore_path, "-s", srcroot_path,
    "-o", generated_obj_path, NULL
  };
  char *fixture = test_read_text_file(tc->fixture_path);
  ASSERT_NOT_NULL(fixture);
  char *expected_stdout = test_extract_fixture_block(fixture, "===stdout===\n");
  char *expected_stdout_second = test_extract_fixture_block(fixture, "===stdout_run2===\n");
  char *expected_stderr = test_extract_fixture_block(fixture, "===stderr===\n");
  char *expected_exit = test_extract_fixture_block(fixture, "===exit===\n");
  ASSERT_NOT_NULL(expected_stdout);
  ASSERT_NOT_NULL(expected_stderr);
  ASSERT_NOT_NULL(expected_exit);
  test_normalize_text(expected_stdout);
  if (expected_stdout_second) test_normalize_text(expected_stdout_second);
  test_normalize_text(expected_stderr);

  TestProcessResult expected = {.stdout_text = expected_stdout,
                                .stderr_text = expected_stderr,
                                .exit_code = atoi(expected_exit)};
  unsigned runs = tc->runs == 0 ? 1u : tc->runs;
  for (unsigned run = 0; run < runs; run++) {
    TestProcessResult generated = {0};
    int run_capture_rc = test_run_argv_capture(run_argv, 2000, &generated);
    normalize_runtime_path(generated.stdout_text, srcroot_path, "srcroot");
    normalize_runtime_path(generated.stdout_text, itemstore_path, "items.dat");
    test_normalize_text(generated.stderr_text);
    ASSERT_EQ_INT(0, run_capture_rc);
    char variant[32];
    ASSERT_TRUE(snprintf(variant, sizeof(variant), "generated_obj_run%u", run + 1u) > 0);
    TestProcessResult expected_for_run = expected;
    if (run > 0 && expected_stdout_second) {
      expected_for_run.stdout_text = expected_stdout_second;
    }
    assert_run_matches(tc->name, variant, &generated, &expected_for_run);
    test_process_result_free(&generated);
  }
  ASSERT_EQ_INT(0, remove(generated_obj_path));
  assert_persisted_code_items(tc, itemstore_path);
  ASSERT_EQ_INT(0, remove(itemstore_path));
  remove_persisted_source_files(tc, srcroot_path);
  ASSERT_EQ_INT(0, rmdir(srcroot_path));
  ASSERT_EQ_INT(0, rmdir(run_dir));

  test_process_result_free(&expected);
  free(expected_exit);
  free(expected_stdout_second);
  free(fixture);
}

void test_interpret_semantics_golden(void) {
  const InterpretGoldenCase cases[] = {
      {"chat_boot", "examples/chat-boot.src",
       "tests/fixtures/interpret/chat-boot.expected.txt", {NULL, NULL}, 0},
      {"chat_load", "examples/chat-load.src",
       "tests/fixtures/interpret/chat-load.expected.txt", {"input", "docommand"}, 0},
      {"echo_boot", "examples/echo-boot.src",
       "tests/fixtures/interpret/echo-boot.expected.txt", {NULL, NULL}, 0},
      {"echo_load", "examples/echo-load.src",
       "tests/fixtures/interpret/echo-load.expected.txt", {"input", NULL}, 0},
      {"break_log", "tests/fixtures/interpret/break-log.src",
       "tests/fixtures/interpret/break-log.expected.txt", {NULL, NULL}, 0},
      {"continue_log", "tests/fixtures/interpret/continue-log.src",
       "tests/fixtures/interpret/continue-log.expected.txt", {NULL, NULL}, 0},
      {"list_itemref_persist", "tests/fixtures/interpret/list-itemref-persist.src",
       "tests/fixtures/interpret/list-itemref-persist.expected.txt", {"target", "once"}, 2},
      {"positive_core", "tests/fixtures/conformance/positive-core.src",
       "tests/fixtures/conformance/positive-core.expected.txt",
       {"tick", "first", "second", "pair", "returner", "fallthrough", "bare", "dup"}, 0},
  };

  for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
    run_case(&cases[i]);
  }

  char *const failing_argv[] = {
    "/bin/sh", "-c", "printf capture-failure; exit 23", NULL
  };
  TestProcessResult failing = {0};
  ASSERT_EQ_INT(0, test_run_argv_capture(failing_argv, 1000, &failing));
  int failing_exit = failing.exit_code;
  int captured_failure = failing.stdout_text &&
                         strcmp(failing.stdout_text, "capture-failure") == 0;
  test_process_result_free(&failing);
  ASSERT_EQ_INT(23, failing_exit);
  ASSERT_TRUE(captured_failure);

  char *const timeout_argv[] = {
    "/bin/sh", "-c", "printf timeout-output; while :; do :; done", NULL
  };
  TestProcessResult timeout = {0};
  ASSERT_EQ_INT(0, test_run_argv_capture(timeout_argv, 50, &timeout));
  int timeout_exit = timeout.exit_code;
  int timeout_seen = timeout.timed_out && timeout.stdout_text &&
                     strcmp(timeout.stdout_text, "timeout-output") == 0;
  test_process_result_free(&timeout);
  ASSERT_EQ_INT(137, timeout_exit);
  ASSERT_TRUE(timeout_seen);
}

static void setup_result_semantics_runtime(void) {
  memset(&config, 0, sizeof(config));
  init_errmsg();
  config.itemstore_ctx = itemstore_owner(make_root_item("root"));
  ASSERT_NOT_NULL(itemstore_root(config.itemstore_ctx));
  config.vm = make_vm();
  ASSERT_NOT_NULL(config.vm);
}

static void teardown_result_semantics_runtime(void) {
  destroy_vm(config.vm);
  destroy_item(itemstore_root(config.itemstore_ctx));
  memset(&config, 0, sizeof(config));
}

static ITEM_t *compile_result_semantics_item_named(const char *label,
                                                   const char *source,
                                                   const char *requested_name) {
  OUTPUT_t *out = NULL;
  char *errdetail = NULL;
  int8_t rc = compile_source_to_bytecode(source, strlen(source), &out, &errdetail);
  if (rc != ERR_NOERROR) {
    fprintf(stderr, "[%s] compile failed: %s\n", label, errdetail ? errdetail : "<no detail>");
  }
  ASSERT_EQ_INT(ERR_NOERROR, rc);
  ASSERT_NOT_NULL(out);
  ASSERT_NOT_NULL(out->bytecode);

  size_t bytecode_len = (size_t)(out->nextbyte - out->bytecode);
  ASSERT_TRUE(bytecode_len <= UINT32_MAX);
  uint8_t *bytecode = out->bytecode;
  out->bytecode = NULL;
  static unsigned next_item_id = 0;
  char generated_name[32];
  const char *item_name = requested_name;
  if (!item_name) {
    int n = snprintf(generated_name, sizeof(generated_name), "result.t%u",
                     next_item_id++);
    ASSERT_TRUE(n > 0 && (size_t)n < sizeof(generated_name));
    item_name = generated_name;
  }
  ITEM_t *item = test_item_set_code(itemstore_root(config.itemstore_ctx), item_name, (uint32_t)bytecode_len, bytecode);
  ASSERT_NOT_NULL(item);

  free(out);
  free(errdetail);
  return item;
}

static ITEM_t *compile_result_semantics_item(const char *label,
                                             const char *source) {
  return compile_result_semantics_item_named(label, source, NULL);
}

static VALUE_t run_result_semantics_source(const char *label, const char *source) {
  ITEM_t *item = compile_result_semantics_item(label, source);
  RuntimeContext ctx;
  runtime_context_init(&ctx, config.vm);
  ctx.itemstore = config.itemstore_ctx;
  ctx.itemstore_filename = config.itemstore;
  ctx.itemstore_durability = config.itemstore_durability;
  ctx.strict_runtime_contracts = config.strict_runtime_contracts;
  return interpret(&ctx, item);
}

static VALUE_t run_result_semantics_source_named(const char *label,
                                                 const char *source,
                                                 const char *item_name) {
  ITEM_t *item = compile_result_semantics_item_named(label, source, item_name);
  RuntimeContext ctx;
  runtime_context_init(&ctx, config.vm);
  ctx.itemstore = config.itemstore_ctx;
  return interpret(&ctx, item);
}

static void assert_result_int(const char *name, const char *source, int64_t expected) {
  VALUE_t result = run_result_semantics_source(name, source);
  ASSERT_EQ_INT(VALUE_int, result.type);
  ASSERT_EQ_INT((int)expected, (int)result.i);
  value_free(&result);
}

static void assert_result_bool(const char *name, const char *source, bool expected) {
  VALUE_t result = run_result_semantics_source(name, source);
  ASSERT_EQ_INT(VALUE_bool, result.type);
  ASSERT_EQ_INT(expected ? 1 : 0, result.i ? 1 : 0);
  value_free(&result);
}

static void assert_result_nil(const char *name, const char *source) {
  VALUE_t result = run_result_semantics_source(name, source);
  ASSERT_EQ_INT(VALUE_nil, result.type);
  value_free(&result);
}

static void assert_result_string(const char *name, const char *source,
                                 const char *expected) {
  VALUE_t result = run_result_semantics_source(name, source);
  ASSERT_EQ_INT(VALUE_str, result.type);
  ASSERT_TRUE(strcmp(result.s, expected) == 0);
  value_free(&result);
}

static void assert_result_list_ints(const char *name, const char *source,
                                    const int64_t *expected, size_t count) {
  VALUE_t result = run_result_semantics_source(name, source);
  ASSERT_EQ_INT(VALUE_list, result.type);
  ASSERT_EQ_INT((int)count, (int)sin_list_count(result.list));
  for (size_t i = 0; i < count; i++) {
    ASSERT_EQ_INT(VALUE_int, sin_list_get(result.list, i)->type);
    ASSERT_EQ_INT((int)expected[i], (int)sin_list_get(result.list, i)->i);
  }
  value_free(&result);
}

void test_interpret_result_semantics(void) {
  setup_result_semantics_runtime();

  char save_path[128];
  ASSERT_EQ_INT(0, test_make_temp_path("sin-interp-sys-save", save_path,
                                      sizeof(save_path)));
  config.itemstore = save_path;
  config.itemstore_durability = ITEMSTORE_DURABLE_FAST;

  assert_result_nil("result.final_expression_is_discarded", "42;");
  assert_result_nil("result.expression_statements_discard", "1; 2;");
  assert_result_nil("result.middle_expression_discard", "@x = 7; @x; 8;");
  assert_result_int("result.explicit_return", "return 42;", 42);
  assert_result_int("result.modulo_precedence", "return 20 % 6 * 2 + 1;", 5);
  assert_result_nil("result.bare_return", "return;");
  assert_result_nil("result.explicit_nil_return", "return nil;");
  assert_result_bool("result.nil_semantics", "return nil == nil and !nil and nil != false and nil != 0 and nil != \"\";", true);
  assert_result_int("result.and_skips_falsy_rhs",
                    "result.marker = 0; result.rhs = code ( result.marker = 1; return 7; ); false and result.rhs; return result.marker;", 0);
  assert_result_int("result.or_skips_truthy_rhs",
                    "result.marker = 0; result.rhs = code ( result.marker = 1; return 7; ); true or result.rhs; return result.marker;", 0);
  assert_result_int("result.and_evaluates_rhs_once",
                    "result.marker = 0; result.rhs = code ( result.marker = result.marker + 1; return 7; ); true and result.rhs; return result.marker;", 1);
  assert_result_int("result.or_evaluates_rhs_once",
                    "result.marker = 0; result.rhs = code ( result.marker = result.marker + 1; return 7; ); false or result.rhs; return result.marker;", 1);
  assert_result_int("result.logical_lhs_evaluates_once",
                    "result.marker = 0; result.lhs = code ( result.marker = result.marker + 1; return false; ); result.rhs = code ( result.marker = result.marker + 10; return true; ); result.lhs and result.rhs; return result.marker;", 1);
  assert_result_bool("result.and_normalizes_truthiness", "return 3 and 7;", true);
  assert_result_bool("result.or_normalizes_truthiness", "return 0 or 7;", true);
  assert_result_bool("result.falsy_short_circuit_normalizes", "return nil and 7;", false);
  assert_result_int("result.binary_operands_left_to_right",
                    "result.order = 0; result.left = code ( result.order = result.order * 10 + 1; return 2; ); result.right = code ( result.order = result.order * 10 + 2; return 3; ); result.left + result.right; return result.order;", 12);
  assert_result_int("result.call_arguments_left_to_right",
                    "result.order = 0; result.callee = code {@a, @b} ( return @a * 10 + @b; ); result.one = code ( result.order = result.order * 10 + 1; return 1; ); result.two = code ( result.order = result.order * 10 + 2; return 2; ); result.callee{result.one, result.two}; return result.order;", 12);
  assert_result_bool("result.nil_local_and_item", "@n = nil; result.explicit_nil = nil; return @n == nil;", true);
  ITEM_t *explicit_nil = find_item(itemstore_root(config.itemstore_ctx), "result.explicit_nil");
  ASSERT_NOT_NULL(explicit_nil);
  ASSERT_EQ_INT(VALUE_nil, item_value(explicit_nil)->type);
  assert_result_int("result.early_return", "return 7; @x = 9;", 7);
  assert_result_int("result.if_return", "if true then return 8; endif; return 9;", 8);
  assert_result_int("result.while_return", "@i = 0; while true do @i++; return @i; endwhile;", 1);
  assert_result_int("result.do_while_return", "do return 6; while true;", 6);
  assert_result_int("result.foreach_executes_list",
                    "@sum = 0; foreach @x in #[1, 2, 3] do @sum = @sum + @x; endfor; return @sum;", 6);
  {
    VALUE_t result = run_result_semantics_source(
        "result.foreach_non_list_leaves_iterator_nil",
        "foreach @x in 7 do @x = 1; endfor; return @x == nil;");
    ASSERT_EQ_INT(VALUE_bool, result.type);
    ASSERT_TRUE(result.i != 0);
    value_free(&result);
    /* FOREACH on a non-list must not set an error. */
    ITEM_t *error = find_item(itemstore_root(config.itemstore_ctx), "error");
    ASSERT_NOT_NULL(error);
    ASSERT_EQ_INT(VALUE_nil, item_value(error)->type);
  }
  assert_result_bool("result.foreach_empty_list_leaves_iterator_nil",
                     "foreach @x in #[] do @x = 1; endfor; return @x == nil;", true);
  assert_result_int("result.foreach_iterator_keeps_last_element",
                    "foreach @x in #[4, 5] do endfor; return @x;", 5);
  assert_result_int("result.foreach_break_exits_loop",
                    "@sum = 0; foreach @x in #[1, 2, 3] do @sum = @sum + @x; break; endfor; return @sum;", 1);
  assert_result_int("result.foreach_continue_advances_iterator",
                    "@sum = 0; foreach @x in #[1, 2, 3] do if @x == 2 then continue; endif; @sum = @sum + @x; endfor; return @sum;", 4);
  assert_result_int("result.foreach_sequence_evaluated_once",
                    "result.calls = 0; result.make = code ( result.calls = result.calls + 1; return #[1, 2]; ); foreach @x in result.make do endfor; return result.calls;", 1);
  assert_result_bool("result.foreach_visits_nil_elements",
                     "result.nil_seen = false; foreach @x in #[nil, 2] do if @x == nil then result.nil_seen = true; endif; endfor; return result.nil_seen;", true);
  assert_result_int("result.foreach_nested_loops",
                    "result.total = 0; foreach @outer in #[1, 2] do foreach @inner in #[10, 20] do result.total = result.total + @outer + @inner; endfor; endfor; return result.total;", 66);
  assert_result_int("result.foreach_snapshot_under_rebinding",
                    "@source = #[1, 2, 3]; result.total = 0; foreach @x in @source do result.total = result.total + @x; @source = #[99]; endfor; return result.total;", 6);
  assert_result_bool("result.foreach_zero_iterations_iterator_nil",
                     "@source = #[]; foreach @x in @source do result.unexpected = true; endfor; return @x == nil;", true);
  assert_result_string("result.owned_string_return", "return \"owned result\";", "owned result");
  assert_result_int("result.embedded_code_return",
                    "result.callee = code ( return 11; ); return result.callee;", 11);
  ASSERT_NOT_NULL(test_item_set_value(itemstore_root(config.itemstore_ctx),
                                      "result.value_item",
                                      (VALUE_t){VALUE_int, {.i = 13}}));
  assert_result_int("result.value_item_unchanged", "return result.value_item;", 13);
  assert_result_nil("result.assignment_has_no_result", "@x = 7;");
  {
    assert_result_list_ints("result.empty_list", "return #[];", NULL, 0);
    const int64_t ordered[] = {1, 2, 12};
    assert_result_list_ints("result.list_order_once",
      "result.order = 0; result.one = code ( result.order = result.order * 10 + 1; return 1; ); result.two = code ( result.order = result.order * 10 + 2; return 2; ); return #[result.one, result.two, result.order];",
      ordered, 3);
  }
  assert_result_int("result.list_code_items_once",
                    "result.marker = 0; result.one = code ( result.marker = result.marker + 1; return 1; ); result.two = code ( result.marker = result.marker + 1; return 2; ); #[result.one, result.two]; return result.marker;", 2);
  {
    VALUE_t result = run_result_semantics_source("result.itemref", "result.marker = 0; @index = 3; fred = code ( result.marker = result.marker + 1; return 7; ); return #[fred, &fred, &players.[@index], result.marker];");
    ASSERT_EQ_INT(VALUE_list, result.type);
    ASSERT_EQ_INT(VALUE_int, sin_list_get(result.list, 0)->type);
    ASSERT_EQ_INT(7, (int)sin_list_get(result.list, 0)->i);
    ASSERT_EQ_INT(VALUE_itemref, sin_list_get(result.list, 1)->type);
    ASSERT_TRUE(strcmp(sin_itemref_path(sin_list_get(result.list, 1)->itemref), "fred") == 0);
    ASSERT_EQ_INT(VALUE_itemref, sin_list_get(result.list, 2)->type);
    ASSERT_TRUE(strcmp(sin_itemref_path(sin_list_get(result.list, 2)->itemref), "players.3") == 0);
    ASSERT_EQ_INT(VALUE_int, sin_list_get(result.list, 3)->type);
    ASSERT_EQ_INT(1, (int)sin_list_get(result.list, 3)->i);
    value_free(&result);
  }
  {
    VALUE_t result = run_result_semantics_source_named(
        "result.relative_itemref", "return &.sibling;", "result.relative");
    ASSERT_EQ_INT(VALUE_itemref, result.type);
    ASSERT_TRUE(strcmp(sin_itemref_path(result.itemref),
                       "result.relative.sibling") == 0);
    value_free(&result);
  }

  assert_result_nil("result.if_statement_discards_branch_value",
                    "if true then 7; else 8; endif;");
  assert_result_nil("result.if_statement_before_final_expression",
                    "if false then 7; else 8; endif; 9;");

  assert_result_nil("result.while_statement_discards_body_value",
                    "@i = 0; while @i < 1 do @i++; 44; endwhile;");
  assert_result_nil("result.while_statement_before_final_expression",
                    "@i = 0; while @i < 3 do @i++; @i; endwhile; @i;");
  assert_result_int("result.do_while_once_then_return",
                    "@i = 0; do @i++; while false; return @i;", 1);
  assert_result_nil("result.do_while_statement_discards_body_value",
                    "@i = 0; do @i++; 44; while false;");
  assert_result_int("result.break_continue_while",
                    "@i = 0; @sum = 0; while @i < 5 do @i++; if @i == 2 then continue; endif; if @i == 4 then break; endif; @sum++; endwhile; return @sum;", 2);
  assert_result_int("result.break_continue_do_while",
                    "@a = 0; do @a++; if @a == 1 then continue; endif; if @a == 2 then break; endif; while @a < 1; @b = 0; do @b++; break; while true; return @a * 10 + @b;", 11);
  assert_result_int("result.break_continue_nearest_nested",
                    "@outer = 0; @hits = 0; while @outer < 2 do @outer++; @inner = 0; do @inner++; if @inner == 1 then continue; endif; @hits++; if @inner == 2 then break; endif; while @inner < 4; endwhile; return @hits;", 2);

  assert_result_bool("result.return_libcall", "return sys.exists{\"result.missing\"};", false);
  assert_result_int("result.nonfinal_libcall_discard",
                    "sys.exists{\"result.missing\"}; return 5;", 5);

  assert_result_bool("result.return_sys_compile",
                     "return sys.compile{\"result.compiled.value = 17;\"};", true);
  ITEM_t *compiled_value = find_item(itemstore_root(config.itemstore_ctx), "result.compiled.value");
  ASSERT_NOT_NULL(compiled_value);
  ASSERT_EQ_INT(VALUE_int, item_value(compiled_value)->type);
  ASSERT_EQ_INT(17, (int)item_value(compiled_value)->i);

  assert_result_int("result.nonfinal_sys_compile_discard",
                    "sys.compile{\"result.compiled.discard = 23;\"}; return 99;", 99);
  ITEM_t *compiled_discard = find_item(itemstore_root(config.itemstore_ctx), "result.compiled.discard");
  ASSERT_NOT_NULL(compiled_discard);
  ASSERT_EQ_INT(VALUE_int, item_value(compiled_discard)->type);
  ASSERT_EQ_INT(23, (int)item_value(compiled_discard)->i);

  ASSERT_NOT_NULL(test_item_set_value(itemstore_root(config.itemstore_ctx), "result.save.marker",
                              (VALUE_t){VALUE_int, {.i = 44}}));
  assert_result_bool("result.return_sys_save", "return sys.save;", true);
  ITEM_t *saved = load_itemstore(save_path);
  ASSERT_NOT_NULL(saved);
  ITEM_t *saved_marker = find_item(saved, "result.save.marker");
  ASSERT_NOT_NULL(saved_marker);
  ASSERT_EQ_INT(VALUE_int, item_value(saved_marker)->type);
  ASSERT_EQ_INT(44, (int)item_value(saved_marker)->i);
  destroy_item(saved);
  ASSERT_EQ_INT(0, unlink(save_path));

  teardown_result_semantics_runtime();
}

void test_runtime_build_list_allocation_failure_consumes_inputs(void) {
  VM_t *vm = make_vm();
  ASSERT_NOT_NULL(vm);
  RuntimeContext ctx;
  runtime_context_init(&ctx, vm);
  uint8_t frame[] = {2, 0, 0, 0};
  runtime_decoder_init(&ctx.decoder, frame, frame + sizeof(frame));
  char *first = strdup("first");
  char *second = strdup("second");
  ASSERT_NOT_NULL(first);
  ASSERT_NOT_NULL(second);
  push_stack(vm->stack, (VALUE_t){VALUE_str, {.s = first}});
  push_stack(vm->stack, (VALUE_t){VALUE_str, {.s = second}});
  alloc_test_fail_after(0);
  uint8_t *next = op_build_list(&ctx, frame, NULL);
  alloc_test_fail_after(-1);
  ASSERT_TRUE(next == frame + sizeof(frame));
  ASSERT_EQ_INT(1, size_stack(vm->stack));
  ASSERT_EQ_INT(VALUE_nil, peek_stack(vm->stack)->type);
  destroy_vm(vm);
}

void test_interpreter_string_literal_allocation_failure_aborts_frame(void) {
  VM_t *vm = make_vm();
  ASSERT_NOT_NULL(vm);
  RuntimeContext ctx;
  runtime_context_init(&ctx, vm);
  uint8_t operand[] = {3, 0, 'a', 'b', 'c'};
  runtime_decoder_init(&ctx.decoder, operand, operand + sizeof(operand));
  alloc_test_fail_after(0);
  uint8_t *next = op_pushstr(&ctx, operand, NULL);
  alloc_test_fail_after(-1);

  ASSERT_TRUE(next == NULL);
  ASSERT_EQ_INT(-1, vm->stack->current);
  destroy_vm(vm);
}


void test_interpret_rejects_malformed_bytecode_before_execution(void) {
  memset(&config, 0, sizeof(config));
  init_errmsg();
  config.strict_validation = true;
  config.itemstore_ctx = itemstore_owner(make_root_item("root"));
  ASSERT_NOT_NULL(itemstore_root(config.itemstore_ctx));
  config.vm = make_vm();
  ASSERT_NOT_NULL(config.vm);

  uint8_t bytecode[] = {0, 0, 'l', 3, 0, 'a'};
  uint8_t *owned = malloc(sizeof(bytecode));
  ASSERT_NOT_NULL(owned);
  memcpy(owned, bytecode, sizeof(bytecode));
  ITEM_t *code = test_item_set_code(itemstore_root(config.itemstore_ctx), "malformed", sizeof(bytecode), owned);
  ASSERT_NOT_NULL(code);

  RuntimeContext ctx;
  runtime_context_init(&ctx, config.vm);
  ctx.itemstore = config.itemstore_ctx;
  ctx.strict_runtime_contracts = config.strict_runtime_contracts;
  ctx.invocation_callstack_floor = 73;
  ctx.invocation_caller_item = itemstore_root(config.itemstore_ctx);
  VALUE_t result = interpret(&ctx, code);
  ASSERT_EQ_INT(VALUE_nil, result.type);
  ASSERT_EQ_INT(73, ctx.invocation_callstack_floor);
  ASSERT_TRUE(ctx.invocation_caller_item == itemstore_root(config.itemstore_ctx));
  ITEM_t *err = find_item(itemstore_root(config.itemstore_ctx), "error");
  ASSERT_NOT_NULL(err);
  ASSERT_EQ_INT(ERR_RUNTIME_BYTECODE, item_value(err)->i);
  ITEM_t *msg = find_item(itemstore_root(config.itemstore_ctx), "error.msg");
  ASSERT_NOT_NULL(msg);
  ASSERT_EQ_INT(VALUE_str, item_value(msg)->type);
  ASSERT_TRUE(strstr(item_value(msg)->s, "Runtime bytecode validation failed") != NULL);
  ASSERT_TRUE(strstr(item_value(msg)->s, "truncated") != NULL);
  ITEM_t *error_item = find_item(itemstore_root(config.itemstore_ctx), "error.item");
  ASSERT_NOT_NULL(error_item);
  ASSERT_EQ_INT(VALUE_str, item_value(error_item)->type);
  ASSERT_TRUE(strcmp(item_value(error_item)->s, "malformed") == 0);

  destroy_vm(config.vm);
  destroy_item(itemstore_root(config.itemstore_ctx));
  memset(&config, 0, sizeof(config));
}

static ITEM_t *install_runtime_verify_test_code(const char *name,
                                                const uint8_t *bytes,
                                                size_t length) {
  uint8_t *owned = malloc(length);
  ASSERT_NOT_NULL(owned);
  memcpy(owned, bytes, length);
  ASSERT_TRUE(length <= UINT32_MAX);
  ITEM_t *item = test_item_set_code(itemstore_root(config.itemstore_ctx), name,
                                    (uint32_t)length, owned);
  ASSERT_NOT_NULL(item);
  return item;
}

static ITEM_t *install_runtime_verify_test_code_in_store(
    ITEMSTORE_t *store, const char *name, const uint8_t *bytes, size_t length) {
  uint8_t *owned = malloc(length);
  ASSERT_NOT_NULL(owned);
  memcpy(owned, bytes, length);
  ASSERT_TRUE(length <= UINT32_MAX);
  ITEM_t *item = item_set_code(itemstore_root(store), name,
                               (uint32_t)length, owned).item;
  ASSERT_NOT_NULL(item);
  return item;
}

static VALUE_t run_runtime_verify_test(RuntimeContext *ctx, ITEM_t *item) {
  ctx->itemstore = config.itemstore_ctx;
  return interpret(ctx, item);
}

void test_runtime_verification_cache_reuses_fetch_transfer(void) {
  static const uint8_t callee_bytes[] = {0, 0, 'h'};
  static const uint8_t caller_bytes[] = {
      0, 0, 'l', 6, 0, 'c', 'a', 'c', 'h', 'e', 'e', 'F', 0, 0, 'h'
  };
  setup_result_semantics_runtime();
  ITEM_t *callee = install_runtime_verify_test_code("cachee", callee_bytes,
                                                    sizeof(callee_bytes));
  ITEM_t *caller = install_runtime_verify_test_code("caller", caller_bytes,
                                                    sizeof(caller_bytes));
  RuntimeContext ctx;
  runtime_context_init(&ctx, config.vm);

  VALUE_t first = run_runtime_verify_test(&ctx, caller);
  ASSERT_EQ_INT(VALUE_nil, first.type);
  value_free(&first);
  ASSERT_EQ_INT(2, runtime_verify_invocations_for_tests(&ctx));
  VALUE_t second = run_runtime_verify_test(&ctx, caller);
  ASSERT_EQ_INT(VALUE_nil, second.type);
  value_free(&second);
  ASSERT_EQ_INT(2, runtime_verify_invocations_for_tests(&ctx));
  ASSERT_TRUE(!item_is_in_use(caller));
  ASSERT_TRUE(!item_is_in_use(callee));
  runtime_destroy(&ctx);
  teardown_result_semantics_runtime();
}

void test_runtime_verification_cache_revision_and_failure_contract(void) {
  static const uint8_t valid_callee[] = {0, 0, 'h'};
  static const uint8_t replacement_callee[] = {0, 0, 'p', 1, 0, 0, 0, 0, 0,
                                               0, 0, 'h'};
  static const uint8_t malformed_callee[] = {0, 0, 'l', 3, 0, 'a'};
  static const uint8_t caller_bytes[] = {
      0, 0, 'l', 6, 0, 'c', 'a', 'c', 'h', 'e', 'e', 'F', 0, 0, 'h'
  };

  setup_result_semantics_runtime();
  ITEM_t *callee = install_runtime_verify_test_code("cachee", valid_callee,
                                                    sizeof(valid_callee));
  ITEM_t *caller = install_runtime_verify_test_code("caller", caller_bytes,
                                                    sizeof(caller_bytes));
  RuntimeContext ctx;
  runtime_context_init(&ctx, config.vm);
  VALUE_t first = run_runtime_verify_test(&ctx, caller);
  ASSERT_EQ_INT(VALUE_nil, first.type);
  value_free(&first);
  ASSERT_EQ_INT(2, runtime_verify_invocations_for_tests(&ctx));

  uint8_t *replacement_owned = malloc(sizeof(replacement_callee));
  ASSERT_NOT_NULL(replacement_owned);
  memcpy(replacement_owned, replacement_callee, sizeof(replacement_callee));
  callee = test_item_set_code(itemstore_root(config.itemstore_ctx), "cachee",
                              sizeof(replacement_callee), replacement_owned);
  ASSERT_NOT_NULL(callee);
  VALUE_t replaced = run_runtime_verify_test(&ctx, caller);
  ASSERT_EQ_INT(VALUE_nil, replaced.type);
  value_free(&replaced);
  ASSERT_EQ_INT(4, runtime_verify_invocations_for_tests(&ctx));

  uint8_t *malformed_owned = malloc(sizeof(malformed_callee));
  ASSERT_NOT_NULL(malformed_owned);
  memcpy(malformed_owned, malformed_callee, sizeof(malformed_callee));
  ASSERT_NOT_NULL(item_set_code(itemstore_root(config.itemstore_ctx),
                                "cachee", sizeof(malformed_callee),
                                malformed_owned).item);
  VALUE_t failed = run_runtime_verify_test(&ctx, caller);
  ASSERT_EQ_INT(VALUE_nil, failed.type);
  value_free(&failed);
  ITEM_t *msg = find_item(itemstore_root(config.itemstore_ctx), "error.msg");
  ASSERT_NOT_NULL(msg);
  ASSERT_EQ_INT(VALUE_str, item_value(msg)->type);
  char *first_message = strdup(item_value(msg)->s);
  ASSERT_NOT_NULL(first_message);
  ASSERT_EQ_INT(6, runtime_verify_invocations_for_tests(&ctx));
  ASSERT_TRUE(!item_is_in_use(caller));
  ASSERT_TRUE(!item_is_in_use(callee));

  VALUE_t failed_again = run_runtime_verify_test(&ctx, caller);
  ASSERT_EQ_INT(VALUE_nil, failed_again.type);
  value_free(&failed_again);
  msg = find_item(itemstore_root(config.itemstore_ctx), "error.msg");
  ASSERT_NOT_NULL(msg);
  ASSERT_TRUE(strcmp(first_message, item_value(msg)->s) == 0);
  /* set_error_item mutates the store and bumps payload revision after the
   * first failure, so retrying conservatively re-verifies both caller and
   * failed callee. */
  ASSERT_EQ_INT(8, runtime_verify_invocations_for_tests(&ctx));
  ASSERT_TRUE(!item_is_in_use(caller));
  ASSERT_TRUE(!item_is_in_use(callee));
  free(first_message);
  runtime_destroy(&ctx);
  teardown_result_semantics_runtime();
}

void test_runtime_verification_cache_revision_wrap_invalidates(void) {
  static const uint8_t code_bytes[] = {0, 0, 'h'};
  setup_result_semantics_runtime();
  ITEM_t *code = install_runtime_verify_test_code("wrap", code_bytes,
                                                 sizeof(code_bytes));
  RuntimeContext ctx;
  runtime_context_init(&ctx, config.vm);
  VALUE_t first = run_runtime_verify_test(&ctx, code);
  ASSERT_EQ_INT(VALUE_nil, first.type);
  value_free(&first);
  ASSERT_EQ_INT(1, runtime_verify_invocations_for_tests(&ctx));

  ITEMSTORE_t *store = config.itemstore_ctx;
  store->context.payload_revision = UINT64_MAX;
  store->context.payload_revision_epoch = 41u;
  VALUE_t at_max = run_runtime_verify_test(&ctx, code);
  ASSERT_EQ_INT(VALUE_nil, at_max.type);
  value_free(&at_max);
  ASSERT_EQ_INT(2, runtime_verify_invocations_for_tests(&ctx));

  uint8_t *replacement = malloc(sizeof(code_bytes));
  ASSERT_NOT_NULL(replacement);
  memcpy(replacement, code_bytes, sizeof(code_bytes));
  ASSERT_NOT_NULL(item_set_code(itemstore_root(store), "wrap",
                                sizeof(code_bytes), replacement).item);
  ASSERT_EQ_INT(0, store->context.payload_revision);
  ASSERT_EQ_INT(42, store->context.payload_revision_epoch);
  VALUE_t wrapped = run_runtime_verify_test(&ctx, code);
  ASSERT_EQ_INT(VALUE_nil, wrapped.type);
  value_free(&wrapped);
  ASSERT_EQ_INT(3, runtime_verify_invocations_for_tests(&ctx));
  runtime_destroy(&ctx);
  teardown_result_semantics_runtime();
}

void test_runtime_verification_cache_revision_token_saturation_bypasses(void) {
  static const uint8_t code_bytes[] = {0, 0, 'h'};
  setup_result_semantics_runtime();
  ITEM_t *code = install_runtime_verify_test_code("saturated", code_bytes,
                                                 sizeof(code_bytes));
  RuntimeContext ctx;
  runtime_context_init(&ctx, config.vm);
  VALUE_t first = run_runtime_verify_test(&ctx, code);
  ASSERT_EQ_INT(VALUE_nil, first.type);
  value_free(&first);
  ASSERT_EQ_INT(1, runtime_verify_invocations_for_tests(&ctx));

  ITEMSTORE_t *store = config.itemstore_ctx;
  store->context.payload_revision = UINT64_MAX;
  store->context.payload_revision_epoch = UINT64_MAX;
  store->context.payload_revision_token_exhausted = false;
  uint8_t *replacement = malloc(sizeof(code_bytes));
  ASSERT_NOT_NULL(replacement);
  memcpy(replacement, code_bytes, sizeof(code_bytes));
  ASSERT_NOT_NULL(item_set_code(itemstore_root(store), "saturated",
                                sizeof(code_bytes), replacement).item);
  ASSERT_TRUE(itemstore_payload_revision_token_exhausted(store));
  ASSERT_EQ_INT(UINT64_MAX, itemstore_payload_revision(store));

  VALUE_t exhausted_first = run_runtime_verify_test(&ctx, code);
  ASSERT_EQ_INT(VALUE_nil, exhausted_first.type);
  value_free(&exhausted_first);
  VALUE_t exhausted_second = run_runtime_verify_test(&ctx, code);
  ASSERT_EQ_INT(VALUE_nil, exhausted_second.type);
  value_free(&exhausted_second);
  ASSERT_EQ_INT(3, runtime_verify_invocations_for_tests(&ctx));
  runtime_destroy(&ctx);
  teardown_result_semantics_runtime();
}

void test_runtime_verification_cache_topology_token_wrap_and_saturation(void) {
  static const uint8_t code_bytes[] = {0, 0, 'h'};
  setup_result_semantics_runtime();
  ITEM_t *code = install_runtime_verify_test_code("topology_code", code_bytes,
                                                 sizeof(code_bytes));
  RuntimeContext ctx;
  runtime_context_init(&ctx, config.vm);
  VALUE_t first = run_runtime_verify_test(&ctx, code);
  ASSERT_EQ_INT(VALUE_nil, first.type);
  value_free(&first);
  ASSERT_EQ_INT(1, runtime_verify_invocations_for_tests(&ctx));

  ITEMSTORE_t *store = config.itemstore_ctx;
  uint64_t payload_before = itemstore_payload_revision(store);
  store->context.topology_revision = UINT64_MAX;
  store->context.topology_revision_epoch = 57u;
  store->context.topology_revision_token_exhausted = false;
  ASSERT_NOT_NULL(item_set_value(itemstore_root(store), "topology.wrap",
                                 (VALUE_t){.type = VALUE_int, .i = 1}).item);
  ASSERT_EQ_INT(0, store->context.topology_revision);
  ASSERT_EQ_INT(58, store->context.topology_revision_epoch);
  ASSERT_EQ_INT(payload_before, itemstore_payload_revision(store));
  VALUE_t wrapped = run_runtime_verify_test(&ctx, code);
  ASSERT_EQ_INT(VALUE_nil, wrapped.type);
  value_free(&wrapped);
  ASSERT_EQ_INT(2, runtime_verify_invocations_for_tests(&ctx));

  store->context.topology_revision = UINT64_MAX;
  store->context.topology_revision_epoch = UINT64_MAX;
  store->context.topology_revision_token_exhausted = false;
  ASSERT_NOT_NULL(item_set_value(itemstore_root(store), "topology.saturate",
                                 (VALUE_t){.type = VALUE_int, .i = 2}).item);
  ASSERT_TRUE(itemstore_topology_revision_token_exhausted(store));
  ASSERT_EQ_INT(payload_before, itemstore_payload_revision(store));
  VALUE_t exhausted_first = run_runtime_verify_test(&ctx, code);
  ASSERT_EQ_INT(VALUE_nil, exhausted_first.type);
  value_free(&exhausted_first);
  VALUE_t exhausted_second = run_runtime_verify_test(&ctx, code);
  ASSERT_EQ_INT(VALUE_nil, exhausted_second.type);
  value_free(&exhausted_second);
  ASSERT_EQ_INT(4, runtime_verify_invocations_for_tests(&ctx));
  runtime_destroy(&ctx);
  teardown_result_semantics_runtime();
}

void test_runtime_verification_cache_ownerless_items_bypass(void) {
  static const uint8_t code_bytes[] = {0, 0, 'h'};
  uint8_t *owned = malloc(sizeof(code_bytes));
  ASSERT_NOT_NULL(owned);
  memcpy(owned, code_bytes, sizeof(code_bytes));
  ITEM_t *raw = make_item("raw", NULL, ITEM_code, VALUE_NIL, owned,
                          (int)sizeof(code_bytes));
  ASSERT_NOT_NULL(raw);
  setup_result_semantics_runtime();
  RuntimeContext ctx;
  runtime_context_init(&ctx, config.vm);
  VALUE_t first = run_runtime_verify_test(&ctx, raw);
  ASSERT_EQ_INT(VALUE_nil, first.type);
  value_free(&first);
  VALUE_t second = run_runtime_verify_test(&ctx, raw);
  ASSERT_EQ_INT(VALUE_nil, second.type);
  value_free(&second);
  ASSERT_EQ_INT(2, runtime_verify_invocations_for_tests(&ctx));
  runtime_destroy(&ctx);
  teardown_result_semantics_runtime();
  destroy_raw_runtime_item(raw);
}

void test_runtime_verification_cache_isolates_live_itemstores(void) {
  static const uint8_t code_bytes[] = {0, 0, 'h'};
  ITEMSTORE_t *first_store = itemstore_create("cache_first");
  ITEMSTORE_t *second_store = itemstore_create("cache_second");
  ASSERT_NOT_NULL(first_store);
  ASSERT_NOT_NULL(second_store);
  ITEM_t *first_code = install_runtime_verify_test_code_in_store(
      first_store, "code", code_bytes, sizeof(code_bytes));
  ITEM_t *second_code = install_runtime_verify_test_code_in_store(
      second_store, "code", code_bytes, sizeof(code_bytes));
  VM_t *vm = make_vm();
  ASSERT_NOT_NULL(vm);
  RuntimeContext ctx;
  runtime_context_init(&ctx, vm);

  ctx.itemstore = first_store;
  VALUE_t first = interpret(&ctx, first_code);
  ASSERT_EQ_INT(VALUE_nil, first.type);
  value_free(&first);
  ASSERT_EQ_INT(1, runtime_verify_invocations_for_tests(&ctx));
  ASSERT_TRUE(!item_is_in_use(first_code));

  ctx.itemstore = second_store;
  VALUE_t second = interpret(&ctx, second_code);
  ASSERT_EQ_INT(VALUE_nil, second.type);
  value_free(&second);
  ASSERT_EQ_INT(2, runtime_verify_invocations_for_tests(&ctx));
  ASSERT_TRUE(!item_is_in_use(second_code));

  ctx.itemstore = first_store;
  VALUE_t first_again = interpret(&ctx, first_code);
  ASSERT_EQ_INT(VALUE_nil, first_again.type);
  value_free(&first_again);
  ASSERT_EQ_INT(3, runtime_verify_invocations_for_tests(&ctx));
  ASSERT_TRUE(!item_is_in_use(first_code));
  ASSERT_TRUE(!item_is_in_use(second_code));

  runtime_destroy(&ctx);
  destroy_vm(vm);
  itemstore_destroy(second_store);
  itemstore_destroy(first_store);
}

void test_runtime_verification_cache_eviction_is_bounded(void) {
  static const uint8_t code_bytes[] = {0, 0, 'h'};
  ITEM_t *items[RUNTIME_VERIFY_CACHE_SIZE + 2u];
  char name[32];
  setup_result_semantics_runtime();
  for (size_t i = 0u; i < sizeof(items) / sizeof(items[0]); i++) {
    int written = snprintf(name, sizeof(name), "cache.%zu", i);
    ASSERT_TRUE(written > 0 && (size_t)written < sizeof(name));
    items[i] = install_runtime_verify_test_code(name, code_bytes,
                                                sizeof(code_bytes));
  }
  RuntimeContext ctx;
  runtime_context_init(&ctx, config.vm);
  for (size_t i = 0u; i < sizeof(items) / sizeof(items[0]); i++) {
    VALUE_t result = run_runtime_verify_test(&ctx, items[i]);
    ASSERT_EQ_INT(VALUE_nil, result.type);
    value_free(&result);
  }
  ASSERT_EQ_INT((int)(sizeof(items) / sizeof(items[0])),
                runtime_verify_invocations_for_tests(&ctx));
  VALUE_t evicted = run_runtime_verify_test(&ctx, items[0]);
  ASSERT_EQ_INT(VALUE_nil, evicted.type);
  value_free(&evicted);
  ASSERT_EQ_INT((int)(sizeof(items) / sizeof(items[0]) + 1u),
                runtime_verify_invocations_for_tests(&ctx));
  runtime_destroy(&ctx);
  teardown_result_semantics_runtime();
}

void test_interpret_baseline_bytecode_safety_in_default_and_strict_modes(void) {
  static const uint8_t short_header[] = {0};
  static const uint8_t truncated_operand[] = {0, 0, 'e'};
  static const uint8_t missing_halt[] = {0, 0, 'b', 1};
  static const uint8_t invalid_opcode[] = {0, 0, 0x7f, 'h'};
  static const uint8_t stack_underflow[] = {0, 0, 'a', 'h'};
  static const uint8_t invalid_local[] = {0, 0, 'e', 1, 'h'};
  static const uint8_t jump_into_operand[] = {
    0, 0, 'l', 3, 0, 'a', 'b', 'c', 'j', 0xFD, 0xFF, 'h'
  };
  struct {
    const char *name;
    const uint8_t *bytes;
    size_t len;
    const char *diagnostic;
  } cases[] = {
    {"short_header", short_header, sizeof(short_header),
     "truncated bytecode header"},
    {"truncated_operand", truncated_operand, sizeof(truncated_operand),
     "truncated LOAD_LOCAL"},
    {"missing_halt", missing_halt, sizeof(missing_halt),
     "final physical instruction must be HALT"},
    {"invalid_opcode", invalid_opcode, sizeof(invalid_opcode),
     "opcode 0x7F (.): invalid opcode; recompile from Sinistra source"},
    {"stack_underflow", stack_underflow, sizeof(stack_underflow),
     "stack underflow"},
    {"invalid_local", invalid_local, sizeof(invalid_local),
     "local index"},
    {"jump_into_operand", jump_into_operand, sizeof(jump_into_operand),
     "not a top-level instruction boundary"},
    {"null_bytecode", NULL, 0, "null bytecode pointer"},
  };

  for (int strict = 0; strict <= 1; strict++) {
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
      setup_result_semantics_runtime();
      config.strict_validation = strict != 0;
      uint8_t *owned = NULL;
      if (cases[i].len > 0) {
        owned = malloc(cases[i].len);
        ASSERT_NOT_NULL(owned);
        memcpy(owned, cases[i].bytes, cases[i].len);
      }
      ITEM_t *code = test_item_set_code(itemstore_root(config.itemstore_ctx), cases[i].name,
                                      (uint32_t)cases[i].len, owned);
      ASSERT_NOT_NULL(code);
      RuntimeContext ctx;
      runtime_context_init(&ctx, config.vm);
      ctx.itemstore = config.itemstore_ctx;
      VALUE_t result = interpret(&ctx, code);
      ASSERT_EQ_INT(VALUE_nil, result.type);
      ITEM_t *err = find_item(itemstore_root(config.itemstore_ctx), "error");
      ASSERT_NOT_NULL(err);
      ASSERT_EQ_INT(ERR_RUNTIME_BYTECODE, item_value(err)->i);
      ITEM_t *msg = find_item(itemstore_root(config.itemstore_ctx), "error.msg");
      ASSERT_NOT_NULL(msg);
      ASSERT_EQ_INT(VALUE_str, item_value(msg)->type);
      ASSERT_TRUE(strstr(item_value(msg)->s, cases[i].diagnostic) != NULL);
      ITEM_t *error_item = find_item(itemstore_root(config.itemstore_ctx), "error.item");
      ASSERT_NOT_NULL(error_item);
      ASSERT_EQ_INT(VALUE_str, item_value(error_item)->type);
      ASSERT_TRUE(strcmp(item_value(error_item)->s, cases[i].name) == 0);
      runtime_destroy(&ctx);
      teardown_result_semantics_runtime();
    }
  }
}

void test_interpret_legacy_and_v1_headers_execute_equivalently(void) {
  const uint8_t legacy[] = {1, 0, 'b', 1, 'Q', 'h'};
  const uint8_t v1[] = {0, 0xff, 'S', 'B', 1, 0, 1, 0, 'b', 1, 'Q', 'h'};
  setup_result_semantics_runtime();
  uint8_t *legacy_copy = malloc(sizeof(legacy));
  uint8_t *v1_copy = malloc(sizeof(v1));
  ASSERT_NOT_NULL(legacy_copy);
  ASSERT_NOT_NULL(v1_copy);
  memcpy(legacy_copy, legacy, sizeof(legacy));
  memcpy(v1_copy, v1, sizeof(v1));
  ITEM_t *a = test_item_set_code(itemstore_root(config.itemstore_ctx),
                                 "legacy_exec", sizeof(legacy), legacy_copy);
  ASSERT_NOT_NULL(a);
  ITEM_t *b = test_item_set_code(itemstore_root(config.itemstore_ctx),
                                 "v1_exec", sizeof(v1), v1_copy);
  ASSERT_NOT_NULL(b);
  RuntimeContext ctx;
  runtime_context_init(&ctx, config.vm);
  ctx.itemstore = config.itemstore_ctx;
  VALUE_t av = interpret(&ctx, a);
  VALUE_t bv = interpret(&ctx, b);
  ASSERT_EQ_INT(VALUE_bool, av.type);
  ASSERT_EQ_INT(VALUE_bool, bv.type);
  ASSERT_TRUE(av.i == 1);
  ASSERT_TRUE(bv.i == 1);
  value_free(&av); value_free(&bv);
  runtime_destroy(&ctx);
  teardown_result_semantics_runtime();
}

void test_interpret_legacy_conversion_semantics(void) {
  const uint8_t legacy[] = {1, 0, 'b', 1, 'Q', 'h'};
  VALUE_t direct;
  VALUE_t converted_value;

  setup_result_semantics_runtime();
  uint8_t *direct_bytes = malloc(sizeof legacy);
  ASSERT_NOT_NULL(direct_bytes);
  memcpy(direct_bytes, legacy, sizeof legacy);
  ITEM_t *direct_item = test_item_set_code(
      itemstore_root(config.itemstore_ctx), "legacy_direct", sizeof legacy,
      direct_bytes);
  ASSERT_NOT_NULL(direct_item);
  RuntimeContext direct_ctx;
  runtime_context_init(&direct_ctx, config.vm);
  direct_ctx.itemstore = config.itemstore_ctx;
  direct = interpret(&direct_ctx, direct_item);
  runtime_destroy(&direct_ctx);
  teardown_result_semantics_runtime();

  BC_ConvertResult conversion = bc_convert_latest(legacy, sizeof legacy);
  ASSERT_EQ_INT(BC_CONVERT_SUCCESS, conversion.status);
  setup_result_semantics_runtime();
  ITEM_t *converted_item = test_item_set_code(
      itemstore_root(config.itemstore_ctx), "legacy_converted",
      (uint32_t)conversion.length, conversion.data);
  ASSERT_NOT_NULL(converted_item);
  conversion.data = NULL;
  RuntimeContext converted_ctx;
  runtime_context_init(&converted_ctx, config.vm);
  converted_ctx.itemstore = config.itemstore_ctx;
  converted_value = interpret(&converted_ctx, converted_item);
  runtime_destroy(&converted_ctx);
  teardown_result_semantics_runtime();

  ASSERT_EQ_INT(VALUE_bool, direct.type);
  ASSERT_EQ_INT(VALUE_bool, converted_value.type);
  ASSERT_EQ_INT(direct.i ? 1 : 0, converted_value.i ? 1 : 0);
  value_free(&direct);
  value_free(&converted_value);
  bc_convert_result_free(&conversion);
}

void test_interpret_embedded_code_boundary_lengths(void) {
  const size_t lengths[] = {0x50u, 0x150u, 0xff50u};
  setup_result_semantics_runtime();

  for (size_t i = 0; i < sizeof(lengths) / sizeof(lengths[0]); i++) {
    char target_name[40];
    char outer_name[40];
    ASSERT_TRUE(snprintf(target_name, sizeof(target_name),
                         "boundary.target%zu", i) > 0);
    ASSERT_TRUE(snprintf(outer_name, sizeof(outer_name),
                         "boundary.outer%zu", i) > 0);

    char *source = malloc(lengths[i] + 1u);
    ASSERT_NOT_NULL(source);
    memcpy(source, "return 7;", 9u);
    memset(source + 9u, ' ', lengths[i] - 9u);
    source[lengths[i]] = '\0';

    IR_Unit *unit = t_new_unit();
    ASSERT_NOT_NULL(unit);
    int32_t payload = ir_add_embedded_code_payload(
        unit, (IR_EmbeddedCodePayload){.source = source});
    ASSERT_TRUE(payload >= 0);
    t_emit(unit, (IR_Inst){.op = IR_OP_PUSH_STRING,
                           .imm = (int64_t)(intptr_t)target_name});
    t_emit(unit, (IR_Inst){.op = IR_OP_ITEM_SAVE_CODE, .a = payload});
    t_emit(unit, (IR_Inst){.op = IR_OP_HALT});

    OUTPUT_t output = {0};
    output.maxsize = lengths[i] + 64u;
    output.bytecode = malloc(output.maxsize);
    output.nextbyte = output.bytecode;
    ASSERT_NOT_NULL(output.bytecode);
    char *errdetail = NULL;
    ASSERT_EQ_INT(ERR_NOERROR,
                  t_emit_bytecode(unit, 0, 0, &output, &errdetail));
    ASSERT_TRUE(errdetail == NULL);
    size_t output_len = (size_t)(output.nextbyte - output.bytecode);
    ASSERT_TRUE(output_len <= UINT32_MAX);

    ITEM_t *outer = test_item_set_code(
        itemstore_root(config.itemstore_ctx), outer_name, (uint32_t)output_len,
        output.bytecode);
    ASSERT_NOT_NULL(outer);
    output.bytecode = NULL;

    RuntimeContext assign_ctx;
    runtime_context_init(&assign_ctx, config.vm);
    assign_ctx.itemstore = config.itemstore_ctx;
    VALUE_t assignment = interpret(&assign_ctx, outer);
    ASSERT_EQ_INT(VALUE_nil, assignment.type);
    value_free(&assignment);
    runtime_destroy(&assign_ctx);

    ITEM_t *target = find_item(itemstore_root(config.itemstore_ctx),
                               target_name);
    ASSERT_NOT_NULL(target);
    ASSERT_EQ_INT(ITEM_code, item_kind(target));
    RuntimeContext execute_ctx;
    runtime_context_init(&execute_ctx, config.vm);
    execute_ctx.itemstore = config.itemstore_ctx;
    VALUE_t result = interpret(&execute_ctx, target);
    ASSERT_EQ_INT(VALUE_int, result.type);
    ASSERT_EQ_INT(7, result.i);
    value_free(&result);
    runtime_destroy(&execute_ctx);

    ir_destroy_unit(unit);
    free(source);
    free(errdetail);
  }

  teardown_result_semantics_runtime();
}

void test_runtime_jump_diagnostic_uses_absolute_header_offset(void) {
  const uint8_t legacy[] = {0, 0, 'j', 0xf6, 0xff, 'h'};
  const uint8_t v1[] = {0, 0xff, 'S', 'B', 1, 0, 0, 0,
                        'j', 0xf6, 0xff, 'h'};
  const uint8_t *blocks[] = {legacy, v1};
  const size_t lengths[] = {sizeof(legacy), sizeof(v1)};
  const uint32_t expected_offsets[] = {3u, 9u};
  for (size_t i = 0; i < 2; i++) {
    setup_result_semantics_runtime();
    uint8_t *copy = malloc(lengths[i]);
    ASSERT_NOT_NULL(copy);
    memcpy(copy, blocks[i], lengths[i]);
    ITEM_t *item = test_item_set_code(itemstore_root(config.itemstore_ctx),
                                      "jump_diag", (uint32_t)lengths[i], copy);
    ASSERT_NOT_NULL(item);
    RuntimeContext ctx;
    runtime_context_init(&ctx, config.vm);
    ctx.itemstore = config.itemstore_ctx;
    ctx.current_item = item;
    const uint8_t *base = item_bytecode(item);
    runtime_decoder_init(&ctx.decoder, base + (i == 0 ? 2u : 8u),
                         base + lengths[i]);
    ASSERT_TRUE(op_jump(&ctx, (uint8_t *)base + expected_offsets[i], item) == NULL);
    ITEM_t *message = find_item(itemstore_root(config.itemstore_ctx), "error.msg");
    ASSERT_NOT_NULL(message);
    ASSERT_TRUE(strstr(item_value(message)->s, "outside the bytecode frame") != NULL);
    char offset[32];
    snprintf(offset, sizeof(offset), "offset %u", expected_offsets[i]);
    ASSERT_TRUE(strstr(item_value(message)->s, offset) != NULL);
    runtime_destroy(&ctx);
    teardown_result_semantics_runtime();
  }
}
