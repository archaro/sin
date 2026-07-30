#include "item.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

#include "config.h"
#include "compiler/compiler_pipeline.h"
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

extern CONFIG_t config;
extern uint8_t *op_build_list(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item);

typedef struct {
  const char *name;
  const char *src_path;
  const char *fixture_path;
  const char *expected_code_items[2];
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
  TestProcessResult generated = {0};
  int run_capture_rc = test_run_argv_capture(run_argv, 2000, &generated);
  ASSERT_EQ_INT(0, remove(generated_obj_path));
  normalize_runtime_path(generated.stdout_text, srcroot_path, "srcroot");
  normalize_runtime_path(generated.stdout_text, itemstore_path, "items.dat");
  test_normalize_text(generated.stderr_text);
  ASSERT_EQ_INT(0, run_capture_rc);
  assert_persisted_code_items(tc, itemstore_path);
  ASSERT_EQ_INT(0, remove(itemstore_path));
  remove_persisted_source_files(tc, srcroot_path);
  ASSERT_EQ_INT(0, rmdir(srcroot_path));
  ASSERT_EQ_INT(0, rmdir(run_dir));

  char *fixture = test_read_text_file(tc->fixture_path);
  ASSERT_NOT_NULL(fixture);
  char *expected_stdout = test_extract_fixture_block(fixture, "===stdout===\n");
  char *expected_stderr = test_extract_fixture_block(fixture, "===stderr===\n");
  char *expected_exit = test_extract_fixture_block(fixture, "===exit===\n");
  ASSERT_NOT_NULL(expected_stdout);
  ASSERT_NOT_NULL(expected_stderr);
  ASSERT_NOT_NULL(expected_exit);
  test_normalize_text(expected_stdout);
  test_normalize_text(expected_stderr);

  TestProcessResult expected = {.stdout_text = expected_stdout,
                                .stderr_text = expected_stderr,
                                .exit_code = atoi(expected_exit)};
  assert_run_matches(tc->name, "generated_obj", &generated, &expected);

  test_process_result_free(&generated);
  test_process_result_free(&expected);
  free(expected_exit);
  free(fixture);
}

void test_interpret_semantics_golden(void) {
  const InterpretGoldenCase cases[] = {
      {"chat_boot", "examples/chat-boot.src",
       "tests/fixtures/interpret/chat-boot.expected.txt", {NULL, NULL}},
      {"chat_load", "examples/chat-load.src",
       "tests/fixtures/interpret/chat-load.expected.txt", {"input", "docommand"}},
      {"echo_boot", "examples/echo-boot.src",
       "tests/fixtures/interpret/echo-boot.expected.txt", {NULL, NULL}},
      {"echo_load", "examples/echo-load.src",
       "tests/fixtures/interpret/echo-load.expected.txt", {"input", NULL}},
      {"break_log", "tests/fixtures/interpret/break-log.src",
       "tests/fixtures/interpret/break-log.expected.txt", {NULL, NULL}},
      {"continue_log", "tests/fixtures/interpret/continue-log.src",
       "tests/fixtures/interpret/continue-log.expected.txt", {NULL, NULL}},
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
  ctx.strict_validation = config.strict_validation;
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
  ctx.strict_validation = config.strict_validation;
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

void test_interpret_baseline_bytecode_safety_in_default_and_strict_modes(void) {
  static const uint8_t short_header[] = {0};
  static const uint8_t truncated_operand[] = {0, 0, 'e'};
  static const uint8_t missing_halt[] = {0, 0, 'b', 1};
  static const uint8_t invalid_opcode[] = {0, 0, 0x7f, 'h'};
  struct {
    const char *name;
    const uint8_t *bytes;
    size_t len;
    const char *diagnostic;
  } cases[] = {
    {"short_header", short_header, sizeof(short_header),
     "missing two-byte locals/params header"},
    {"truncated_operand", truncated_operand, sizeof(truncated_operand),
     "truncated LOAD_LOCAL"},
    {"missing_halt", missing_halt, sizeof(missing_halt),
     "final physical instruction must be HALT"},
    {"invalid_opcode", invalid_opcode, sizeof(invalid_opcode),
     "opcode 0x7F (.): invalid opcode; recompile from Sinistra source"},
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
      ctx.strict_validation = config.strict_validation;
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
