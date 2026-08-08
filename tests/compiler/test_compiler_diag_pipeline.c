#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "compiler/compiler_pipeline.h"
#include "compiler/emitbc.h"
#include "compiler/compdiag.h"
#include "compiler/semant.h"
#include "compiler/lower.h"
#include "compiler/ir.h"
#include "error.h"
#include "memory.h"
#include "test_helpers.h"
#include "version.h"
#include "test_assert.h"

static void assert_capture_stdout_matches_file(const TestProcessResult *result,
                                               const char *expected_path,
                                               const char *context) {
  FILE *expected_file = fopen(expected_path, "rb");
  ASSERT_NOT_NULL(expected_file);
  ASSERT_EQ_INT(0, fseek(expected_file, 0, SEEK_END));
  long expected_size = ftell(expected_file);
  ASSERT_TRUE(expected_size >= 0);
  ASSERT_EQ_INT(0, fseek(expected_file, 0, SEEK_SET));

  size_t expected_length = (size_t)expected_size;
  uint8_t *expected = malloc(expected_length ? expected_length : 1);
  ASSERT_NOT_NULL(expected);
  size_t read_length = fread(expected, 1, expected_length, expected_file);
  ASSERT_EQ_INT(0, fclose(expected_file));
  ASSERT_EQ_INT((int)expected_length, (int)read_length);
  ASSERT_EQ_INT((int)expected_length, (int)result->stdout_length);
  assert_bytes_equal_with_diag(expected, expected_length,
                               (const uint8_t *)result->stdout_text,
                               result->stdout_length, context);
  free(expected);
}

static void assert_cli_metadata_case(const char *tool, const char *flag,
                                     int expected_status,
                                     const char *stdout_contains,
                                     const char *stdout_exact,
                                     const char *stderr_contains,
                                     int expect_empty_stdout,
                                     int expect_empty_stderr) {
  char *argv[] = {"./scomp", (char *)flag, NULL};
  if (strcmp(tool, "sin") == 0) argv[0] = "./sin";
  else if (strcmp(tool, "sdiss") == 0) argv[0] = "./sdiss";
  else if (strcmp(tool, "sconv") == 0) argv[0] = "./sconv";

  TestProcessResult result = {0};
  ASSERT_EQ_INT(0, test_run_argv_capture(argv, 0, &result));
  ASSERT_EQ_INT(expected_status, result.exit_code);
  if (expect_empty_stdout) ASSERT_EQ_INT(0, (int)strlen(result.stdout_text));
  if (expect_empty_stderr) ASSERT_EQ_INT(0, (int)strlen(result.stderr_text));
  if (stdout_contains) {
    ASSERT_TRUE(strstr(result.stdout_text, stdout_contains) != NULL);
  }
  if (stdout_exact) ASSERT_TRUE(strcmp(result.stdout_text, stdout_exact) == 0);
  if (stderr_contains) {
    ASSERT_TRUE(strstr(result.stderr_text, stderr_contains) != NULL);
  }
  test_process_result_free(&result);
}

static void test_shared_argv_capture_stdin_eof(void) {
  char *const argv[] = {"/bin/cat", NULL};
  TestProcessResult result = {0};
  ASSERT_EQ_INT(0, test_run_argv_capture(argv, 1000, &result));
  ASSERT_EQ_INT(0, result.exit_code);
  ASSERT_EQ_INT(0, result.timed_out);
  ASSERT_EQ_INT(0, (int)result.stdout_length);
  ASSERT_EQ_INT(0, (int)result.stderr_length);
  test_process_result_free(&result);
}

void test_cli_metadata_stdout_stderr_and_status(void) {
  const char *tools[] = {"sin", "scomp", "sdiss", "sconv"};
  const char *usage[] = {"Syntax: sin <options>",
                         "scomp <input file> <output file>",
                         "Usage: sdiss -o <object file>",
                         "sconv <input itemstore> <output itemstore>"};
  char expected_version[64];

  for (size_t i = 0; i < sizeof(tools) / sizeof(tools[0]); i++) {
    snprintf(expected_version, sizeof(expected_version), "%s %s\n", tools[i], SINVERSION);
    assert_cli_metadata_case(tools[i], "--help", 0, usage[i], NULL, NULL, 0, 1);
    assert_cli_metadata_case(tools[i], "-h", 0, usage[i], NULL, NULL, 0, 1);
    assert_cli_metadata_case(tools[i], "--version", 0, NULL, expected_version, NULL, 0, 1);
    assert_cli_metadata_case(tools[i], "--definitely-invalid-option", 1, NULL, NULL,
                             "Try '", 1, 0);
    assert_cli_metadata_case(tools[i], "--definitely-invalid-option", 1, NULL, NULL,
                             "--help", 1, 0);
  }

  assert_cli_metadata_case("sin", "--help", 0, "--loadonly", NULL, NULL, 0, 1);
  assert_cli_metadata_case("sin", "--help", 0, "--verbose", NULL, NULL, 0, 1);
  assert_cli_metadata_case("sin", "--bootonly", 1, NULL, NULL, "invalid option", 1, 0);
  assert_cli_metadata_case("sin", "-b", 1, NULL, NULL, "invalid option", 1, 0);
}

static void test_compiler_cli_help_inventory_and_missing_arguments(void) {
  assert_cli_metadata_case("scomp", "--help", 0, "--input", NULL, NULL, 0, 1);
  assert_cli_metadata_case("scomp", "--help", 0, "--output", NULL, NULL, 0, 1);
  assert_cli_metadata_case("scomp", "--help", 0, "--quiet", NULL, NULL, 0, 1);
  assert_cli_metadata_case("scomp", "--help", 0, "--verbose", NULL, NULL, 0, 1);
  assert_cli_metadata_case("sdiss", "--help", 0, "--object", NULL, NULL, 0, 1);
  assert_cli_metadata_case("sdiss", "--help", 0, "--raw", NULL, NULL, 0, 1);
  assert_cli_metadata_case("sdiss", "--help", 0, "--no-header", NULL, NULL, 0, 1);
  assert_cli_metadata_case("sdiss", "--help", 0, "--quiet", NULL, NULL, 0, 1);
  assert_cli_metadata_case("sdiss", "--help", 0, "--verbose", NULL, NULL, 0, 1);

  assert_cli_metadata_case("sconv", "--help", 0, "--itemstore-durability", NULL, NULL, 0, 1);
  assert_cli_metadata_case("sconv", "--help", 0, "--replace", NULL, NULL, 0, 1);
  assert_cli_metadata_case("sconv", "--help", 0, "--quiet", NULL, NULL, 0, 1);
  assert_cli_metadata_case("sconv", "--help", 0, "--verbose", NULL, NULL, 0, 1);
  assert_cli_metadata_case("scomp", "--input", 1, NULL, NULL,
                           "invalid option", 1, 0);
  assert_cli_metadata_case("scomp", "--output", 1, NULL, NULL,
                           "invalid option", 1, 0);
  assert_cli_metadata_case("sdiss", "--object", 1, NULL, NULL,
                           "invalid option", 1, 0);
  assert_cli_metadata_case("sconv", "--input", 1, NULL, NULL,
                           "invalid option", 1, 0);
}


static void test_shared_logging_cli_levels(void) {
  const char *src_path = "tests/fixtures/log-level.tmp.src";
  const char *obj_path = "tests/fixtures/log-level.tmp.obj";
  const char *itemstore_path = "tests/fixtures/log-level.tmp.items";
  FILE *src = fopen(src_path, "wb");
  ASSERT_NOT_NULL(src);
  const char *program = "@x = 1;\nreturn @x;\n";
  ASSERT_EQ_INT((int)strlen(program), (int)fwrite(program, 1, strlen(program), src));
  ASSERT_EQ_INT(0, fclose(src));

  char *const quiet_compile_argv[] = {
      "./scomp", "--quiet", "-i", (char *)src_path, "-o", (char *)obj_path,
      NULL};
  TestProcessResult result = {0};
  ASSERT_EQ_INT(0, test_run_argv_capture(quiet_compile_argv, 0, &result));
  ASSERT_EQ_INT(0, result.exit_code);
  ASSERT_EQ_INT(0, (int)strlen(result.stdout_text));
  ASSERT_EQ_INT(0, (int)strlen(result.stderr_text));
  test_process_result_free(&result);

  char *const verbose_compile_argv[] = {
      "./scomp", "--verbose", "-i", (char *)src_path, "-o", (char *)obj_path,
      NULL};
  ASSERT_EQ_INT(0, test_run_argv_capture(verbose_compile_argv, 0, &result));
  ASSERT_EQ_INT(0, result.exit_code);
  ASSERT_TRUE(strstr(result.stdout_text, "Source loaded:") != NULL);
  ASSERT_TRUE(strstr(result.stdout_text, "Compilation completed:") != NULL);
  ASSERT_EQ_INT(0, (int)strlen(result.stderr_text));
  test_process_result_free(&result);

  char *const quiet_disassemble_argv[] = {
      "./sdiss", "--quiet", "--no-header", "-o", (char *)obj_path, NULL};
  ASSERT_EQ_INT(0, test_run_argv_capture(quiet_disassemble_argv, 0, &result));
  ASSERT_EQ_INT(0, result.exit_code);
  ASSERT_TRUE(strlen(result.stdout_text) > 0);
  ASSERT_TRUE(strstr(result.stdout_text, "Beginning disassembly") == NULL);
  ASSERT_EQ_INT(0, (int)strlen(result.stderr_text));
  test_process_result_free(&result);

  char *const verbose_disassemble_argv[] = {
      "./sdiss", "--verbose", "--no-header", "-o", (char *)obj_path, NULL};
  ASSERT_EQ_INT(0, test_run_argv_capture(verbose_disassemble_argv, 0, &result));
  ASSERT_EQ_INT(0, result.exit_code);
  ASSERT_TRUE(strstr(result.stdout_text, "Beginning disassembly") != NULL);
  ASSERT_TRUE(strstr(result.stdout_text, "Finishing up.") != NULL);
  ASSERT_EQ_INT(0, (int)strlen(result.stderr_text));
  test_process_result_free(&result);

  char *const quiet_sin_argv[] = {
      "./sin", "--quiet", "--loadonly", "-i", (char *)itemstore_path,
      "-s", "tests/fixtures", "-o", (char *)obj_path, NULL};
  ASSERT_EQ_INT(0, test_run_argv_capture(quiet_sin_argv, 0, &result));
  ASSERT_EQ_INT(0, result.exit_code);
  ASSERT_TRUE(strstr(result.stdout_text, "Using 'srcroot'") == NULL);
  ASSERT_TRUE(strstr(result.stderr_text, "Using 'srcroot'") == NULL);
  test_process_result_free(&result);

  char *const default_sin_argv[] = {
      "./sin", "--loadonly", "-i", (char *)itemstore_path, "-s",
      "tests/fixtures", "-o", (char *)obj_path, NULL};
  ASSERT_EQ_INT(0, test_run_argv_capture(default_sin_argv, 0, &result));
  ASSERT_EQ_INT(0, result.exit_code);
  ASSERT_TRUE(strstr(result.stderr_text, "Bytecode interpreter returned") == NULL);
  ASSERT_TRUE(strstr(result.stdout_text,
                     "Using 'tests/fixtures' as the source root.") != NULL);
  ASSERT_TRUE(strstr(result.stderr_text,
                     "Using 'tests/fixtures' as the source root.") == NULL);
  test_process_result_free(&result);

  char *const verbose_sin_argv[] = {
      "./sin", "--verbose", "--loadonly", "-i", (char *)itemstore_path,
      "-s", "tests/fixtures", "-o", (char *)obj_path, NULL};
  ASSERT_EQ_INT(0, test_run_argv_capture(verbose_sin_argv, 0, &result));
  ASSERT_EQ_INT(0, result.exit_code);
  ASSERT_TRUE(strstr(result.stdout_text, "Runtime options:") != NULL);
  ASSERT_TRUE(strstr(result.stderr_text, "Bytecode interpreter returned:") != NULL);
  test_process_result_free(&result);

  remove(src_path);
  remove(obj_path);
  remove(itemstore_path);
}

static void test_scomp_cli_options(void) {
  const char *src_path = "tests/fixtures/scomp-cli-options.tmp.src";
  const char *pos_obj_path = "tests/fixtures/scomp-cli-options-pos.tmp.obj";
  const char *opt_obj_path = "tests/fixtures/scomp-cli-options-opt.tmp.obj";
  FILE *src = fopen(src_path, "wb");
  ASSERT_NOT_NULL(src);
  const char *program = "@x = 1;\n@x;\n";
  ASSERT_EQ_INT((int)strlen(program), (int)fwrite(program, 1, strlen(program), src));
  ASSERT_EQ_INT(0, fclose(src));

  char *const positional_argv[] = {
      "./scomp", (char *)src_path, (char *)pos_obj_path, NULL};
  TestProcessResult result = {0};
  ASSERT_EQ_INT(0, test_run_argv_capture(positional_argv, 0, &result));
  ASSERT_EQ_INT(0, result.exit_code);
  test_process_result_free(&result);

  char *const option_argv[] = {"./scomp", "-q", "-i", (char *)src_path,
                               "-o", (char *)opt_obj_path, NULL};
  ASSERT_EQ_INT(0, test_run_argv_capture(option_argv, 0, &result));
  ASSERT_EQ_INT(0, result.exit_code);
  test_process_result_free(&result);
  assert_file_bytes_equal(pos_obj_path, opt_obj_path,
                          "scomp positional and option output");

  char *const stdio_argv[] = {"./scomp", "-q", "-i", "-", "-o", "-", NULL};
  ASSERT_EQ_INT(0, test_run_argv_capture_with_stdin(
                      stdio_argv, program, strlen(program), 0, &result));
  ASSERT_EQ_INT(0, result.exit_code);
  ASSERT_EQ_INT(0, (int)result.stderr_length);
  ASSERT_TRUE(result.stdout_length > 0);
  ASSERT_TRUE(memchr(result.stdout_text, '\0', result.stdout_length) != NULL);
  assert_capture_stdout_matches_file(&result, pos_obj_path,
                                     "scomp positional and stdio output");
  test_process_result_free(&result);

  remove(src_path);
  remove(pos_obj_path);
  remove(opt_obj_path);
}

static void test_scomp_cli_malformed_diagnostic_shape(void) {
  const char *src_path = "tests/fixtures/scomp-cli-malformed.tmp.src";
  const char *obj_path = "tests/fixtures/scomp-cli-malformed.tmp.obj";
  FILE *src = fopen(src_path, "wb");
  ASSERT_NOT_NULL(src);
  const char *malformed = "@x = 1;\n@yy = 2;\n^;";
  ASSERT_EQ_INT((int)strlen(malformed), (int)fwrite(malformed, 1, strlen(malformed), src));
  ASSERT_EQ_INT(0, fclose(src));

  char *const argv[] = {"./scomp", (char *)src_path, (char *)obj_path, NULL};
  TestProcessResult result = {0};
  ASSERT_EQ_INT(0, test_run_argv_capture(argv, 0, &result));
  ASSERT_TRUE(result.exit_code != 0);

  ASSERT_TRUE(strstr(result.stderr_text, "Diagnostic SIN-PARSE-") != NULL);
  ASSERT_TRUE(strstr(result.stderr_text, "stage: PARSE") != NULL);
  ASSERT_TRUE(strstr(result.stderr_text,
                     "file: tests/fixtures/scomp-cli-malformed.tmp.src") != NULL);
  ASSERT_TRUE(strstr(result.stderr_text, "line: 3") != NULL);
  ASSERT_TRUE(strstr(result.stderr_text, "column: 1") != NULL);
  ASSERT_TRUE(strstr(result.stderr_text, "message:") != NULL);
  ASSERT_TRUE(strstr(result.stderr_text, "errno: ERR_") != NULL);
  ASSERT_TRUE(strstr(result.stderr_text, "source:") != NULL);
  ASSERT_TRUE(strstr(result.stderr_text, "    ^;") != NULL);
  ASSERT_TRUE(strstr(result.stderr_text, "    ^") != NULL);
  ASSERT_TRUE(strstr(result.stderr_text, "Diag: code=") == NULL);

  test_process_result_free(&result);
  remove(src_path);
  remove(obj_path);
}

static void test_compiler_diag_repeated_set_reset_cycles(void) {
  CompilerDiagnostic d;
  compiler_diag_init(&d);

  for (int i = 0; i < 256; i++) {
    compiler_diag_set(&d, ERR_COMP_SYNTAX, DIAG_PHASE_PARSE, "parse: repeated diagnostic");
    compiler_diag_set_source_name(&d, "repeat.sin");
    compiler_diag_set_excerpt(&d, "^;");
    ASSERT_NOT_NULL(d.message);
    ASSERT_NOT_NULL(d.stable_code);
    ASSERT_NOT_NULL(d.source_name);
    ASSERT_NOT_NULL(d.excerpt);
    compiler_diag_reset(&d);
    ASSERT_TRUE(d.message == NULL);
    ASSERT_TRUE(d.stable_code == NULL);
    ASSERT_TRUE(d.source_name == NULL);
    ASSERT_TRUE(d.excerpt == NULL);
    ASSERT_TRUE(!d.has_loc);
  }

  char *errdetail = NULL;
  for (int i = 0; i < 256; i++) {
    int8_t errnum = ERR_NOERROR;
    ASSERT_TRUE(compdiag_setf_once(&errnum, &errdetail, ERR_COMP_SYNTAX, "diag",
                                   "repeated detail %d", i));
    ASSERT_EQ_INT(ERR_COMP_SYNTAX, errnum);
    ASSERT_NOT_NULL(errdetail);
    compdiag_reset_detail(&errdetail);
    ASSERT_TRUE(errdetail == NULL);
  }
}

static void test_compiler_diag_rejects_256_locals(void) {
  char source[8192];
  size_t used = 0;
  for (int i = 0; i < 256; i++) {
    int written = snprintf(source + used, sizeof(source) - used,
                           "@local_%d = %d;\n", i, i);
    ASSERT_TRUE(written > 0);
    ASSERT_TRUE((size_t)written < sizeof(source) - used);
    used += (size_t)written;
  }

  OUTPUT_t *out = NULL;
  CompilerDiagnostic d;
  compiler_diag_init(&d);
  int8_t rc = compile_source_to_bytecode_diag(source, used, &out, &d);
  ASSERT_EQ_INT(ERR_COMP_TOOMANYLOCALS, rc);
  ASSERT_EQ_INT(ERR_COMP_TOOMANYLOCALS, d.code);
  ASSERT_EQ_INT(DIAG_PHASE_SEMANT, d.phase);
  ASSERT_TRUE(out == NULL);
  compiler_diag_reset(&d);
}

void test_compiler_diag_rejects_deep_foreach_with_dedicated_detail(void) {
  char source[16384];
  size_t used = 0;
  for (int i = 0; i < 64; i++) {
    int written = snprintf(source + used, sizeof(source) - used,
                           "foreach @x_%d in #[] do\n", i);
    ASSERT_TRUE(written > 0);
    ASSERT_TRUE((size_t)written < sizeof(source) - used);
    used += (size_t)written;
  }
  for (int i = 0; i < 64; i++) {
    int written = snprintf(source + used, sizeof(source) - used, "endfor;\n");
    ASSERT_TRUE(written > 0);
    ASSERT_TRUE((size_t)written < sizeof(source) - used);
    used += (size_t)written;
  }

  OUTPUT_t *out = NULL;
  CompilerDiagnostic d;
  compiler_diag_init(&d);
  int8_t rc = compile_source_to_bytecode_diag(source, used, &out, &d);
  ASSERT_EQ_INT(ERR_COMP_TOOMANYLOCALS, rc);
  ASSERT_EQ_INT(DIAG_PHASE_SEMANT, d.phase);
  ASSERT_TRUE(d.message != NULL);
  ASSERT_TRUE(strcmp(d.message, "semant: foreach nesting exceeds the local budget") == 0);
  ASSERT_TRUE(out == NULL);
  compiler_diag_reset(&d);
}

void test_compiler_diag_allows_sequential_foreach_hidden_local_reuse(void) {
  char source[16384];
  size_t used = 0;
  for (int i = 0; i < 252; i++) {
    int written = snprintf(source + used, sizeof(source) - used,
                           "@local_%d = %d;\n", i, i);
    ASSERT_TRUE(written > 0);
    ASSERT_TRUE((size_t)written < sizeof(source) - used);
    used += (size_t)written;
  }
  int written = snprintf(source + used, sizeof(source) - used,
                         "foreach @local_0 in #[] do endfor;\n"
                         "foreach @local_1 in #[] do endfor;\n");
  ASSERT_TRUE(written > 0);
  ASSERT_TRUE((size_t)written < sizeof(source) - used);
  used += (size_t)written;

  OUTPUT_t *out = NULL;
  CompilerDiagnostic d;
  compiler_diag_init(&d);
  int8_t rc = compile_source_to_bytecode_diag(source, used, &out, &d);
  ASSERT_EQ_INT(ERR_NOERROR, rc);
  ASSERT_EQ_INT(ERR_NOERROR, d.code);
  ASSERT_TRUE(out != NULL);
  free(out->bytecode);
  free(out);
  compiler_diag_reset(&d);
}

static void free_pipeline_output(OUTPUT_t *out) {
  if (!out) return;
  free(out->bytecode);
  free(out);
}

static size_t pipeline_output_size(const OUTPUT_t *out) {
  if (!out || !out->bytecode || !out->nextbyte) return 0;
  return (size_t)(out->nextbyte - out->bytecode);
}

static void test_compiler_pipeline_legacy_diag_success_parity(void) {
  static const char *sources[] = {
      "1;",
      "@x = 7; @x;",
      "if 1 then 2; endif;",
      "sys.log{\"hello\"};",
  };

  for (size_t i = 0; i < sizeof(sources) / sizeof(sources[0]); i++) {
    OUTPUT_t *legacy_out = NULL;
    OUTPUT_t *diag_out = NULL;
    char *errdetail = NULL;
    CompilerDiagnostic diag;
    compiler_diag_init(&diag);

    int8_t legacy_rc = compile_source_to_bytecode(
        sources[i], strlen(sources[i]), &legacy_out, &errdetail);
    int8_t diag_rc = compile_source_to_bytecode_diag(
        sources[i], strlen(sources[i]), &diag_out, &diag);

    ASSERT_EQ_INT(ERR_NOERROR, legacy_rc);
    ASSERT_EQ_INT(legacy_rc, diag_rc);
    ASSERT_NOT_NULL(legacy_out);
    ASSERT_NOT_NULL(diag_out);
    ASSERT_TRUE(errdetail == NULL);
    ASSERT_EQ_INT((int)pipeline_output_size(legacy_out),
                  (int)pipeline_output_size(diag_out));
    ASSERT_TRUE(memcmp(legacy_out->bytecode, diag_out->bytecode,
                       pipeline_output_size(legacy_out)) == 0);

    free_pipeline_output(legacy_out);
    free_pipeline_output(diag_out);
    compiler_diag_reset(&diag);
  }
}

static void test_compiler_pipeline_legacy_diag_error_parity(void) {
  static const char *sources[] = {
      "^;",
      "@x;",
      "@x = ;",
  };

  for (size_t i = 0; i < sizeof(sources) / sizeof(sources[0]); i++) {
    OUTPUT_t *legacy_out = (OUTPUT_t *)(uintptr_t)1;
    OUTPUT_t *diag_out = (OUTPUT_t *)(uintptr_t)1;
    char *errdetail = NULL;
    CompilerDiagnostic diag;
    compiler_diag_init(&diag);

    int8_t legacy_rc = compile_source_to_bytecode(
        sources[i], strlen(sources[i]), &legacy_out, &errdetail);
    int8_t diag_rc = compile_source_to_bytecode_diag(
        sources[i], strlen(sources[i]), &diag_out, &diag);

    ASSERT_TRUE(legacy_rc != ERR_NOERROR);
    ASSERT_EQ_INT(legacy_rc, diag_rc);
    ASSERT_TRUE(legacy_out == NULL);
    ASSERT_TRUE(diag_out == NULL);
    ASSERT_NOT_NULL(errdetail);
    ASSERT_NOT_NULL(diag.message);
    ASSERT_TRUE(strcmp(errdetail, diag.message) == 0);
    ASSERT_EQ_INT(legacy_rc, diag.code);

    compdiag_reset_detail(&errdetail);
    compiler_diag_reset(&diag);
  }
}

static void test_compiler_pipeline_parameter_seeding(void) {
  const char *params[] = {"@who"};
  OUTPUT_t *out = (OUTPUT_t *)(uintptr_t)1;
  char *errdetail = NULL;

  int8_t rc = compile_source_to_bytecode_with_params(
      "@who;", strlen("@who;"), params, 1, &out, &errdetail);
  ASSERT_EQ_INT(ERR_NOERROR, rc);
  ASSERT_NOT_NULL(out);
  ASSERT_TRUE(errdetail == NULL);
  ASSERT_TRUE(pipeline_output_size(out) >= 2);
  ASSERT_EQ_INT(1, out->bytecode[6]);
  ASSERT_EQ_INT(1, out->bytecode[7]);
  free_pipeline_output(out);

  out = (OUTPUT_t *)(uintptr_t)1;
  rc = compile_source_to_bytecode("@who;", strlen("@who;"), &out,
                                  &errdetail);
  ASSERT_EQ_INT(ERR_COMP_LOCALBEFOREDEF, rc);
  ASSERT_TRUE(out == NULL);
  ASSERT_NOT_NULL(errdetail);
  compdiag_reset_detail(&errdetail);
}

static void test_compiler_pipeline_invalid_inputs_clear_output(void) {
  ParseInput invalid_input = {NULL, 0, "invalid.sin"};
  OUTPUT_t *out = (OUTPUT_t *)(uintptr_t)1;
  char *errdetail = strdup("stale detail");
  ASSERT_NOT_NULL(errdetail);

  int8_t rc = compile_source_to_bytecode(NULL, 0, &out, &errdetail);
  ASSERT_EQ_INT(ERR_COMP_SYNTAX, rc);
  ASSERT_TRUE(out == NULL);
  ASSERT_TRUE(errdetail == NULL);

  out = (OUTPUT_t *)(uintptr_t)1;
  rc = compile_parse_input_to_bytecode(&invalid_input, &out, &errdetail);
  ASSERT_EQ_INT(ERR_COMP_SYNTAX, rc);
  ASSERT_TRUE(out == NULL);
  ASSERT_TRUE(errdetail == NULL);

  CompilerDiagnostic diag;
  compiler_diag_init(&diag);
  out = (OUTPUT_t *)(uintptr_t)1;
  rc = compile_source_to_bytecode_diag(NULL, 0, &out, &diag);
  ASSERT_EQ_INT(ERR_COMP_SYNTAX, rc);
  ASSERT_TRUE(out == NULL);
  ASSERT_EQ_INT(DIAG_PHASE_COMPILE, diag.phase);
  compiler_diag_reset(&diag);

  out = (OUTPUT_t *)(uintptr_t)1;
  rc = compile_parse_input_to_bytecode_diag(&invalid_input, &out, &diag);
  ASSERT_EQ_INT(ERR_COMP_SYNTAX, rc);
  ASSERT_TRUE(out == NULL);
  ASSERT_EQ_INT(DIAG_PHASE_COMPILE, diag.phase);
  ASSERT_NOT_NULL(diag.source_name);
  ASSERT_TRUE(strcmp("invalid.sin", diag.source_name) == 0);
  compiler_diag_reset(&diag);
}

static void test_compiler_pipeline_failure_cleanup(void) {
  const char *source = "if 1 then 2; elsif 0 then 3; else 4; endif;";
  bool legacy_saw_failure = false;
  bool legacy_saw_success = false;
  bool diag_saw_failure = false;
  bool diag_saw_success = false;

  for (long fail_at = 0; fail_at < 128; fail_at++) {
    OUTPUT_t *out = (OUTPUT_t *)(uintptr_t)1;
    char *errdetail = NULL;
    alloc_test_fail_after(fail_at);
    int8_t rc = compile_source_to_bytecode(source, strlen(source), &out,
                                            &errdetail);
    alloc_test_fail_after(-1);

    if (rc == ERR_NOERROR) {
      legacy_saw_success = true;
      free_pipeline_output(out);
    } else {
      legacy_saw_failure = true;
      ASSERT_TRUE(out == NULL);
    }
    compdiag_reset_detail(&errdetail);
  }

  for (long fail_at = 0; fail_at < 128; fail_at++) {
    OUTPUT_t *out = (OUTPUT_t *)(uintptr_t)1;
    CompilerDiagnostic diag;
    compiler_diag_init(&diag);
    alloc_test_fail_after(fail_at);
    int8_t rc = compile_source_to_bytecode_diag(source, strlen(source), &out,
                                                &diag);
    alloc_test_fail_after(-1);

    if (rc == ERR_NOERROR) {
      diag_saw_success = true;
      free_pipeline_output(out);
    } else {
      diag_saw_failure = true;
      ASSERT_TRUE(out == NULL);
    }
    compiler_diag_reset(&diag);
  }

  ASSERT_TRUE(legacy_saw_failure);
  ASSERT_TRUE(legacy_saw_success);
  ASSERT_TRUE(diag_saw_failure);
  ASSERT_TRUE(diag_saw_success);
}

static void test_compiler_pipeline_parameter_seeding_oom(void) {
  const char *source = "1;";
  const char *params[] = {"seed"};
  bool saw_seeding_oom = false;

  for (long fail_at = 0; fail_at < 256; fail_at++) {
    OUTPUT_t *out = NULL;
    char *errdetail = NULL;
    alloc_test_fail_after(fail_at);
    int8_t rc = compile_source_to_bytecode_with_params(
        source, strlen(source), params, 1, &out, &errdetail);
    alloc_test_fail_after(-1);

    if (rc == ERR_COMP_UNKNOWN && errdetail &&
        strstr(errdetail, "semant: out of memory growing local table") != NULL) {
      saw_seeding_oom = true;
      ASSERT_TRUE(out == NULL);
      compdiag_reset_detail(&errdetail);
      break;
    }

    if (out) free_pipeline_output(out);
    compdiag_reset_detail(&errdetail);
  }

  ASSERT_TRUE(saw_seeding_oom);
}

void test_compiler_diag_pipeline(void){
  test_shared_argv_capture_stdin_eof();
  test_compiler_pipeline_legacy_diag_success_parity();
  test_compiler_pipeline_legacy_diag_error_parity();
  test_compiler_pipeline_parameter_seeding();
  test_compiler_pipeline_invalid_inputs_clear_output();
  test_compiler_pipeline_failure_cleanup();
  test_compiler_pipeline_parameter_seeding_oom();
  test_compiler_diag_repeated_set_reset_cycles();
  test_compiler_diag_rejects_256_locals();
  test_scomp_cli_malformed_diagnostic_shape();
  test_scomp_cli_options();
  test_compiler_cli_help_inventory_and_missing_arguments();
  test_shared_logging_cli_levels();
  OUTPUT_t *out=NULL; CompilerDiagnostic d; compiler_diag_init(&d);
  int8_t rc = compile_source_to_bytecode_diag("^;",2,&out,&d);
  ASSERT_EQ_INT(ERR_COMP_UNKNOWNCHAR, rc); ASSERT_EQ_INT(ERR_COMP_UNKNOWNCHAR, d.code); ASSERT_EQ_INT(DIAG_PHASE_PARSE, d.phase); ASSERT_NOT_NULL(d.message);
  ASSERT_NOT_NULL(d.stable_code); ASSERT_TRUE(strcmp("SIN-PARSE-0005", d.stable_code)==0);
  ASSERT_NOT_NULL(d.source_name); ASSERT_TRUE(strcmp("<memory>", d.source_name)==0);
  ASSERT_EQ_INT(1, d.line); ASSERT_EQ_INT(1, d.column); ASSERT_TRUE(d.has_loc);
  ASSERT_NOT_NULL(d.excerpt); ASSERT_TRUE(strcmp("^;", d.excerpt)==0);
  compiler_diag_reset(&d);
  rc = compile_source_to_bytecode_diag("@x;",3,&out,&d);
  ASSERT_EQ_INT(ERR_COMP_LOCALBEFOREDEF, rc); ASSERT_EQ_INT(ERR_COMP_LOCALBEFOREDEF, d.code); ASSERT_EQ_INT(DIAG_PHASE_SEMANT, d.phase); ASSERT_NOT_NULL(d.message);
  ASSERT_NOT_NULL(d.stable_code); ASSERT_TRUE(strcmp("SIN-SEMANT-0004", d.stable_code)==0);
  ASSERT_NOT_NULL(d.source_name); ASSERT_TRUE(strcmp("<memory>", d.source_name)==0);
  ASSERT_EQ_INT(1, d.line); ASSERT_EQ_INT(1, d.column); ASSERT_TRUE(d.has_loc);
  ASSERT_NOT_NULL(d.excerpt); ASSERT_TRUE(strcmp("@x;", d.excerpt)==0);

  compiler_diag_reset(&d);
  const char *syntax_source = "@x = 1;\n@yy = 2;\n@z = ;";
  rc = compile_source_to_bytecode_diag(syntax_source, strlen(syntax_source), &out, &d);
  ASSERT_EQ_INT(ERR_COMP_SYNTAX, rc);
  ASSERT_EQ_INT(DIAG_PHASE_PARSE, d.phase);
  ASSERT_EQ_INT(3, d.line);
  ASSERT_EQ_INT(6, d.column);
  ASSERT_EQ_INT(1, d.span);
  ASSERT_TRUE(d.has_loc);
  ASSERT_NOT_NULL(d.excerpt);
  ASSERT_TRUE(strcmp("@z = ;", d.excerpt)==0);

  compiler_diag_reset(&d);
  const char *unknown_source = "@x = 1;\n@yy = 2;\n☃;";
  rc = compile_source_to_bytecode_diag(unknown_source, strlen(unknown_source), &out, &d);
  ASSERT_EQ_INT(ERR_COMP_UNKNOWNCHAR, rc);
  ASSERT_EQ_INT(DIAG_PHASE_PARSE, d.phase);
  ASSERT_EQ_INT(3, d.line);
  ASSERT_EQ_INT(1, d.column);
  ASSERT_EQ_INT(1, d.span);
  ASSERT_TRUE(d.has_loc);
  ASSERT_NOT_NULL(d.excerpt);
  ASSERT_TRUE(strcmp("☃;", d.excerpt)==0);

  compiler_diag_reset(&d);
  const char *newline_string_source = "@x = \"abc\n@z = 1;";
  rc = compile_source_to_bytecode_diag(newline_string_source,
                                      strlen(newline_string_source), &out, &d);
  ASSERT_EQ_INT(ERR_COMP_UNKNOWNCHAR, rc);
  ASSERT_EQ_INT(DIAG_PHASE_PARSE, d.phase);
  ASSERT_TRUE(strstr(d.message, "Newline in string.") != NULL);
  ASSERT_EQ_INT(1, d.line);
  ASSERT_EQ_INT(6, d.column);
  ASSERT_EQ_INT(1, d.span);
  ASSERT_TRUE(d.has_loc);
  ASSERT_NOT_NULL(d.excerpt);
  ASSERT_TRUE(strcmp("@x = \"abc", d.excerpt)==0);

  compiler_diag_reset(&d);
  const char *eof_string_source = "@x = \"abc";
  rc = compile_source_to_bytecode_diag(eof_string_source,
                                      strlen(eof_string_source), &out, &d);
  ASSERT_EQ_INT(ERR_COMP_UNKNOWNCHAR, rc);
  ASSERT_EQ_INT(DIAG_PHASE_PARSE, d.phase);
  ASSERT_TRUE(strstr(d.message, "EOF in string.") != NULL);
  ASSERT_EQ_INT(1, d.line);
  ASSERT_EQ_INT(6, d.column);
  ASSERT_EQ_INT(1, d.span);
  ASSERT_TRUE(d.has_loc);
  ASSERT_NOT_NULL(d.excerpt);
  ASSERT_TRUE(strcmp("@x = \"abc", d.excerpt)==0);

  compiler_diag_reset(&d);
  const char *named_source = "@x = ;";
  ParseInput named_input = {named_source, strlen(named_source), "custom_source.sin"};
  char *errdetail = NULL;
  rc = compile_parse_input_to_bytecode(&named_input, &out, &errdetail);
  ASSERT_EQ_INT(ERR_COMP_SYNTAX, rc);
  ASSERT_NOT_NULL(errdetail);
  ASSERT_TRUE(strstr(errdetail, "custom_source.sin") != NULL);
  compdiag_reset_detail(&errdetail);

  compiler_diag_reset(&d);
  rc = compile_parse_input_to_bytecode_diag(&named_input, &out, &d);
  ASSERT_EQ_INT(ERR_COMP_SYNTAX, rc);
  ASSERT_NOT_NULL(d.source_name);
  ASSERT_TRUE(strcmp("custom_source.sin", d.source_name)==0);
  ASSERT_NOT_NULL(d.message);
  ASSERT_TRUE(strstr(d.message, "custom_source.sin") != NULL);


  compiler_diag_reset(&d);
  AS_NODE *ast = NULL;
  SCANNER_STATE_t parse_state = {0};
  ParseInput parse_input = {"^;", 2, "parse_stage.sin"};
  errdetail = NULL;
  rc = parse_source_compiler_diag(&parse_input, &ast, &errdetail, &d, &parse_state);
  ASSERT_EQ_INT(ERR_COMP_UNKNOWNCHAR, rc);
  ASSERT_EQ_INT(DIAG_PHASE_PARSE, d.phase);
  ASSERT_NOT_NULL(errdetail);
  compdiag_reset_detail(&errdetail);
  free(parse_state.offending_token);

  compiler_diag_reset(&d);
  SEM_CTX *sem = sem_create_ctx();
  ParseInput sem_input = {"@x;", 3, "semant_stage.sin"};
  errdetail = NULL;
  rc = parse_source(&sem_input, &ast, &errdetail);
  ASSERT_EQ_INT(ERR_NOERROR, rc);
  rc = sem_check_locals_diag(ast, &errdetail, &d, sem);
  ASSERT_EQ_INT(ERR_COMP_LOCALBEFOREDEF, rc);
  ASSERT_EQ_INT(DIAG_PHASE_SEMANT, d.phase);
  compdiag_reset_detail(&errdetail);
  as_delete(ast);
  sem_delete_ctx(sem);

  compiler_diag_reset(&d);
  IR_Unit *ir = NULL;
  errdetail = NULL;
  rc = lower_ast_to_ir_diag(NULL, NULL, NULL, &errdetail, &d);
  ASSERT_TRUE(rc != ERR_NOERROR);
  (void)ir;
  ASSERT_EQ_INT(DIAG_PHASE_LOWER, d.phase);
  compdiag_reset_detail(&errdetail);

  compiler_diag_reset(&d);
  errdetail = NULL;
  rc = ir_validate_diag(NULL, 0, &errdetail, &d);
  ASSERT_EQ_INT(ERR_COMP_SYNTAX, rc);
  ASSERT_EQ_INT(DIAG_PHASE_IR_VALIDATE, d.phase);
  compdiag_reset_detail(&errdetail);

  compiler_diag_reset(&d);
  errdetail = NULL;
  rc = emit_bytecode_diag(NULL, 0, 0, NULL, &errdetail, &d);
  ASSERT_EQ_INT(ERR_COMP_SYNTAX, rc);
  ASSERT_EQ_INT(DIAG_PHASE_EMITBC, d.phase);
  compdiag_reset_detail(&errdetail);

  compiler_diag_reset(&d);
  rc = compile_source_to_bytecode_diag(NULL, 0, &out, &d);
  ASSERT_EQ_INT(ERR_COMP_SYNTAX, rc);
  ASSERT_EQ_INT(DIAG_PHASE_COMPILE, d.phase);

  compiler_diag_reset(&d);
}
