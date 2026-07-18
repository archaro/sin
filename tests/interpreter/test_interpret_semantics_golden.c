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

extern CONFIG_t config;

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
    ASSERT_EQ_INT(ITEM_code, item->type);
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
  config.itemroot = make_root_item("root");
  ASSERT_NOT_NULL(config.itemroot);
  config.vm = make_vm();
  ASSERT_NOT_NULL(config.vm);
}

static void teardown_result_semantics_runtime(void) {
  destroy_vm(config.vm);
  destroy_item(config.itemroot);
  memset(&config, 0, sizeof(config));
}

static ITEM_t *compile_result_semantics_item(const char *label, const char *source) {
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
  char item_name[32];
  int n = snprintf(item_name, sizeof(item_name), "result.t%u", next_item_id++);
  ASSERT_TRUE(n > 0 && (size_t)n < sizeof(item_name));
  ITEM_t *item = insert_code_item(config.itemroot, item_name, (uint32_t)bytecode_len, bytecode);
  ASSERT_NOT_NULL(item);

  free(out);
  free(errdetail);
  return item;
}

static VALUE_t run_result_semantics_source(const char *label, const char *source) {
  ITEM_t *item = compile_result_semantics_item(label, source);
  RuntimeContext ctx;
  runtime_context_init(&ctx, config.vm);
  ctx.itemroot = config.itemroot;
  ctx.strict_validation = config.strict_validation;
  ctx.strict_runtime_contracts = config.strict_runtime_contracts;
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

void test_interpret_result_semantics(void) {
  setup_result_semantics_runtime();

  assert_result_int("result.final_expression", "42;", 42);
  assert_result_int("result.expression_statement_discard", "1; 2;", 2);
  assert_result_int("result.middle_expression_discard", "@x = 7; @x; 8;", 8);
  assert_result_nil("result.assignment_has_no_result", "@x = 7;");

  assert_result_nil("result.if_statement_discards_branch_value",
                    "if true then 7; else 8; endif;");
  assert_result_int("result.if_statement_before_final_expression",
                    "if false then 7; else 8; endif; 9;", 9);

  assert_result_nil("result.while_statement_discards_body_value",
                    "@i = 0; while @i < 1 do @i++; 44; endwhile;");
  assert_result_int("result.while_statement_before_final_expression",
                    "@i = 0; while @i < 3 do @i++; @i; endwhile; @i;", 3);

  assert_result_bool("result.final_libcall", "sys.exists{\"result.missing\"};", false);
  assert_result_int("result.nonfinal_libcall_discard",
                    "sys.exists{\"result.missing\"}; 5;", 5);

  assert_result_bool("result.final_sys_compile",
                     "sys.compile{\"result.compiled.value = 17;\"};", true);
  ITEM_t *compiled_value = find_item(config.itemroot, "result.compiled.value");
  ASSERT_NOT_NULL(compiled_value);
  ASSERT_EQ_INT(VALUE_int, compiled_value->value.type);
  ASSERT_EQ_INT(17, (int)compiled_value->value.i);

  assert_result_int("result.nonfinal_sys_compile_discard",
                    "sys.compile{\"result.compiled.discard = 23;\"}; 99;", 99);
  ITEM_t *compiled_discard = find_item(config.itemroot, "result.compiled.discard");
  ASSERT_NOT_NULL(compiled_discard);
  ASSERT_EQ_INT(VALUE_int, compiled_discard->value.type);
  ASSERT_EQ_INT(23, (int)compiled_discard->value.i);

  teardown_result_semantics_runtime();
}


void test_interpret_rejects_malformed_bytecode_before_execution(void) {
  memset(&config, 0, sizeof(config));
  init_errmsg();
  config.strict_validation = true;
  config.itemroot = make_root_item("root");
  ASSERT_NOT_NULL(config.itemroot);
  config.vm = make_vm();
  ASSERT_NOT_NULL(config.vm);

  uint8_t bytecode[] = {0, 0, 'l', 3, 0, 'a'};
  uint8_t *owned = malloc(sizeof(bytecode));
  ASSERT_NOT_NULL(owned);
  memcpy(owned, bytecode, sizeof(bytecode));
  ITEM_t *code = insert_code_item(config.itemroot, "malformed", sizeof(bytecode), owned);
  ASSERT_NOT_NULL(code);

  RuntimeContext ctx;
  runtime_context_init(&ctx, config.vm);
  ctx.itemroot = config.itemroot;
  ctx.strict_validation = config.strict_validation;
  ctx.strict_runtime_contracts = config.strict_runtime_contracts;
  VALUE_t result = interpret(&ctx, code);
  ASSERT_EQ_INT(VALUE_nil, result.type);
  ITEM_t *err = find_item(config.itemroot, "error");
  ASSERT_NOT_NULL(err);
  ASSERT_EQ_INT(ERR_RUNTIME_BYTECODE, err->value.i);
  ITEM_t *msg = find_item(config.itemroot, "error.msg");
  ASSERT_NOT_NULL(msg);
  ASSERT_EQ_INT(VALUE_str, msg->value.type);
  ASSERT_TRUE(strstr(msg->value.s, "Runtime bytecode validation failed") != NULL);
  ASSERT_TRUE(strstr(msg->value.s, "truncated") != NULL);
  ITEM_t *error_item = find_item(config.itemroot, "error.item");
  ASSERT_NOT_NULL(error_item);
  ASSERT_EQ_INT(VALUE_str, error_item->value.type);
  ASSERT_TRUE(strcmp(error_item->value.s, "malformed") == 0);

  destroy_vm(config.vm);
  destroy_item(config.itemroot);
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
     "missing terminating HALT opcode"},
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
      ITEM_t *code = insert_code_item(config.itemroot, cases[i].name,
                                      (uint32_t)cases[i].len, owned);
      ASSERT_NOT_NULL(code);
      RuntimeContext ctx;
      runtime_context_init(&ctx, config.vm);
      ctx.itemroot = config.itemroot;
      ctx.strict_validation = config.strict_validation;
      VALUE_t result = interpret(&ctx, code);
      ASSERT_EQ_INT(VALUE_nil, result.type);
      ITEM_t *err = find_item(config.itemroot, "error");
      ASSERT_NOT_NULL(err);
      ASSERT_EQ_INT(ERR_RUNTIME_BYTECODE, err->value.i);
      ITEM_t *msg = find_item(config.itemroot, "error.msg");
      ASSERT_NOT_NULL(msg);
      ASSERT_EQ_INT(VALUE_str, msg->value.type);
      ASSERT_TRUE(strstr(msg->value.s, cases[i].diagnostic) != NULL);
      ITEM_t *error_item = find_item(config.itemroot, "error.item");
      ASSERT_NOT_NULL(error_item);
      ASSERT_EQ_INT(VALUE_str, error_item->value.type);
      ASSERT_TRUE(strcmp(error_item->value.s, cases[i].name) == 0);
      runtime_destroy(&ctx);
      teardown_result_semantics_runtime();
    }
  }
}
