#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "compiler/compiler_pipeline.h"
#include "compiler/compdiag.h"
#include "compiler/semant.h"
#include "compiler/lower.h"
#include "compiler/ir.h"
#include "error.h"
#include "version.h"
#include "test_assert.h"

static char *read_text_file_for_diag_test(const char *path) {
  FILE *f = fopen(path, "rb");
  ASSERT_NOT_NULL(f);
  ASSERT_EQ_INT(0, fseek(f, 0, SEEK_END));
  long n = ftell(f);
  ASSERT_TRUE(n >= 0);
  ASSERT_EQ_INT(0, fseek(f, 0, SEEK_SET));
  char *buf = malloc((size_t)n + 1);
  ASSERT_NOT_NULL(buf);
  ASSERT_EQ_INT((int)n, (int)fread(buf, 1, (size_t)n, f));
  buf[n] = '\0';
  ASSERT_EQ_INT(0, fclose(f));
  return buf;
}

static void assert_cli_metadata_case(const char *tool, const char *flag,
                                     int expected_status,
                                     const char *stdout_contains,
                                     const char *stdout_exact,
                                     const char *stderr_contains,
                                     int expect_empty_stdout,
                                     int expect_empty_stderr) {
  char out_path[256];
  char err_path[256];
  char cmd[1024];
  int cmd_len = 0;
  snprintf(out_path, sizeof(out_path), "tests/fixtures/%s-%s.out.tmp.txt",
           tool, flag[0] == '-' && flag[1] == '-' ? flag + 2 : flag + 1);
  snprintf(err_path, sizeof(err_path), "tests/fixtures/%s-%s.err.tmp.txt",
           tool, flag[0] == '-' && flag[1] == '-' ? flag + 2 : flag + 1);

  cmd_len = snprintf(cmd, sizeof(cmd),
                     "./%s %s > %s 2> %s; test $? -eq %d",
                     tool, flag, out_path, err_path, expected_status);
  ASSERT_TRUE(cmd_len > 0 && (size_t)cmd_len < sizeof(cmd));
  ASSERT_EQ_INT(0, system(cmd));

  char *out = read_text_file_for_diag_test(out_path);
  char *err = read_text_file_for_diag_test(err_path);
  if (expect_empty_stdout) ASSERT_EQ_INT(0, (int)strlen(out));
  if (expect_empty_stderr) ASSERT_EQ_INT(0, (int)strlen(err));
  if (stdout_contains) ASSERT_TRUE(strstr(out, stdout_contains) != NULL);
  if (stdout_exact) ASSERT_TRUE(strcmp(out, stdout_exact) == 0);
  if (stderr_contains) ASSERT_TRUE(strstr(err, stderr_contains) != NULL);

  free(out);
  free(err);
  remove(out_path);
  remove(err_path);
}

void test_cli_metadata_stdout_stderr_and_status(void) {
  const char *tools[] = {"sin", "scomp", "sdiss"};
  const char *usage[] = {"Syntax: sin <options>",
                         "scomp <input file> <output file>",
                         "Syntax: sdiss <options>"};
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
}


static void test_shared_logging_cli_levels(void) {
  const char *src_path = "tests/fixtures/log-level.tmp.src";
  const char *obj_path = "tests/fixtures/log-level.tmp.obj";
  const char *stdout_path = "tests/fixtures/log-level.tmp.out";
  const char *stderr_path = "tests/fixtures/log-level.tmp.err";
  const char *bad_obj_path = "tests/fixtures/log-level-missing.tmp.obj";
  FILE *src = fopen(src_path, "wb");
  ASSERT_NOT_NULL(src);
  const char *program = "@x = 1;\n@x;\n";
  ASSERT_EQ_INT((int)strlen(program), (int)fwrite(program, 1, strlen(program), src));
  ASSERT_EQ_INT(0, fclose(src));

  char cmd[1024];
  int cmd_len = snprintf(cmd, sizeof(cmd), "./scomp --quiet -i %s -o %s > %s 2> %s",
                         src_path, obj_path, stdout_path, stderr_path);
  ASSERT_TRUE(cmd_len > 0 && (size_t)cmd_len < sizeof(cmd));
  ASSERT_EQ_INT(0, system(cmd));
  char *out = read_text_file_for_diag_test(stdout_path);
  char *err = read_text_file_for_diag_test(stderr_path);
  ASSERT_EQ_INT(0, (int)strlen(out));
  ASSERT_EQ_INT(0, (int)strlen(err));
  free(out);
  free(err);

  cmd_len = snprintf(cmd, sizeof(cmd), "./scomp --quiet -i %s -o %s > %s 2> %s",
                     "tests/fixtures/does-not-exist.src", bad_obj_path, stdout_path, stderr_path);
  ASSERT_TRUE(cmd_len > 0 && (size_t)cmd_len < sizeof(cmd));
  ASSERT_TRUE(system(cmd) != 0);
  out = read_text_file_for_diag_test(stdout_path);
  err = read_text_file_for_diag_test(stderr_path);
  ASSERT_EQ_INT(0, (int)strlen(out));
  ASSERT_TRUE(strstr(err, "Error:") != NULL);
  ASSERT_TRUE(strstr(err, "Diagnostic") != NULL);
  ASSERT_TRUE(strstr(err, "No such file") != NULL);
  free(out);
  free(err);

  cmd_len = snprintf(cmd, sizeof(cmd), "./sdiss --quiet -o %s > %s 2> %s",
                     bad_obj_path, stdout_path, stderr_path);
  ASSERT_TRUE(cmd_len > 0 && (size_t)cmd_len < sizeof(cmd));
  ASSERT_TRUE(system(cmd) != 0);
  out = read_text_file_for_diag_test(stdout_path);
  err = read_text_file_for_diag_test(stderr_path);
  ASSERT_EQ_INT(0, (int)strlen(out));
  ASSERT_TRUE(strstr(err, "Unable to read object file") != NULL);
  ASSERT_TRUE(strstr(err, "No such file") != NULL);
  free(out);
  free(err);

  cmd_len = snprintf(cmd, sizeof(cmd), "./sin --quiet -o %s > %s 2> %s",
                     bad_obj_path, stdout_path, stderr_path);
  ASSERT_TRUE(cmd_len > 0 && (size_t)cmd_len < sizeof(cmd));
  ASSERT_TRUE(system(cmd) != 0);
  out = read_text_file_for_diag_test(stdout_path);
  err = read_text_file_for_diag_test(stderr_path);
  ASSERT_EQ_INT(0, (int)strlen(out));
  ASSERT_TRUE(strstr(err, "Unable to read object file") != NULL);
  ASSERT_TRUE(strstr(err, "No such file") != NULL);
  free(out);
  free(err);

  cmd_len = snprintf(cmd, sizeof(cmd), "./scomp --verbose -i %s -o %s > %s 2> %s",
                     src_path, obj_path, stdout_path, stderr_path);
  ASSERT_TRUE(cmd_len > 0 && (size_t)cmd_len < sizeof(cmd));
  ASSERT_EQ_INT(0, system(cmd));
  out = read_text_file_for_diag_test(stdout_path);
  err = read_text_file_for_diag_test(stderr_path);
  ASSERT_EQ_INT(0, (int)strlen(out));
  ASSERT_TRUE(strstr(err, "Source loaded:") != NULL);
  ASSERT_TRUE(strstr(err, "Compilation completed:") != NULL);
  free(out);
  free(err);

  cmd_len = snprintf(cmd, sizeof(cmd), "./scomp --verbose -i %s -o - > %s 2> %s",
                     src_path, stdout_path, stderr_path);
  ASSERT_TRUE(cmd_len > 0 && (size_t)cmd_len < sizeof(cmd));
  ASSERT_EQ_INT(0, system(cmd));
  out = read_text_file_for_diag_test(stdout_path);
  err = read_text_file_for_diag_test(stderr_path);
  ASSERT_TRUE(strlen(out) > 0);
  ASSERT_TRUE(strstr(out, "Compiling") == NULL);
  ASSERT_TRUE(strstr(err, "Compiling") != NULL);
  free(out);
  free(err);

  cmd_len = snprintf(cmd, sizeof(cmd), "./sdiss --quiet --no-header -o %s > %s 2> %s",
                     obj_path, stdout_path, stderr_path);
  ASSERT_TRUE(cmd_len > 0 && (size_t)cmd_len < sizeof(cmd));
  ASSERT_EQ_INT(0, system(cmd));
  out = read_text_file_for_diag_test(stdout_path);
  err = read_text_file_for_diag_test(stderr_path);
  ASSERT_TRUE(strlen(out) > 0);
  ASSERT_TRUE(strstr(out, "Beginning disassembly") == NULL);
  ASSERT_EQ_INT(0, (int)strlen(err));
  free(out);
  free(err);

  cmd_len = snprintf(cmd, sizeof(cmd), "./sin --quiet -o %s > %s 2> %s",
                     obj_path, stdout_path, stderr_path);
  ASSERT_TRUE(cmd_len > 0 && (size_t)cmd_len < sizeof(cmd));
  ASSERT_TRUE(system(cmd) != 0);
  out = read_text_file_for_diag_test(stdout_path);
  err = read_text_file_for_diag_test(stderr_path);
  ASSERT_TRUE(strstr(out, "Using 'srcroot'") == NULL);
  ASSERT_TRUE(strstr(err, "Using 'srcroot'") == NULL);
  free(out);
  free(err);

  cmd_len = snprintf(cmd, sizeof(cmd), "./sin --verbose -o %s > %s 2> %s",
                     obj_path, stdout_path, stderr_path);
  ASSERT_TRUE(cmd_len > 0 && (size_t)cmd_len < sizeof(cmd));
  ASSERT_TRUE(system(cmd) != 0);
  out = read_text_file_for_diag_test(stdout_path);
  err = read_text_file_for_diag_test(stderr_path);
  ASSERT_TRUE(strstr(err, "Runtime options:") != NULL);
  ASSERT_TRUE(strstr(out, "Runtime options:") == NULL);
  free(out);
  free(err);

  remove(src_path);
  remove(obj_path);
  remove(stdout_path);
  remove(stderr_path);
  remove(bad_obj_path);
}

static void test_scomp_cli_options(void) {
  const char *src_path = "tests/fixtures/scomp-cli-options.tmp.src";
  const char *pos_obj_path = "tests/fixtures/scomp-cli-options-pos.tmp.obj";
  const char *opt_obj_path = "tests/fixtures/scomp-cli-options-opt.tmp.obj";
  const char *stdio_obj_path = "tests/fixtures/scomp-cli-options-stdio.tmp.obj";
  const char *help_path = "tests/fixtures/scomp-cli-options-help.tmp.txt";
  const char *version_path = "tests/fixtures/scomp-cli-options-version.tmp.txt";
  FILE *src = fopen(src_path, "wb");
  ASSERT_NOT_NULL(src);
  const char *program = "@x = 1;\n@x;\n";
  ASSERT_EQ_INT((int)strlen(program), (int)fwrite(program, 1, strlen(program), src));
  ASSERT_EQ_INT(0, fclose(src));

  ASSERT_EQ_INT(0, system("./scomp --help > tests/fixtures/scomp-cli-options-help.tmp.txt 2>/dev/null"));
  char *help = read_text_file_for_diag_test(help_path);
  ASSERT_TRUE(strstr(help, "scomp <input file> <output file>") != NULL);
  ASSERT_TRUE(strstr(help, "scomp -i <input file> -o <output file> [options]") != NULL);
  free(help);

  ASSERT_EQ_INT(0, system("./scomp --version > tests/fixtures/scomp-cli-options-version.tmp.txt 2>/dev/null"));
  char *version = read_text_file_for_diag_test(version_path);
  ASSERT_TRUE(strstr(version, "scomp ") != NULL);
  free(version);

  char cmd[1024];
  int cmd_len = snprintf(cmd, sizeof(cmd), "./scomp %s %s >/dev/null 2>/dev/null", src_path, pos_obj_path);
  ASSERT_TRUE(cmd_len > 0 && (size_t)cmd_len < sizeof(cmd));
  ASSERT_EQ_INT(0, system(cmd));

  cmd_len = snprintf(cmd, sizeof(cmd), "./scomp -q -i %s -o %s >/dev/null 2>/dev/null", src_path, opt_obj_path);
  ASSERT_TRUE(cmd_len > 0 && (size_t)cmd_len < sizeof(cmd));
  ASSERT_EQ_INT(0, system(cmd));

  cmd_len = snprintf(cmd, sizeof(cmd), "cmp %s %s >/dev/null", pos_obj_path, opt_obj_path);
  ASSERT_TRUE(cmd_len > 0 && (size_t)cmd_len < sizeof(cmd));
  ASSERT_EQ_INT(0, system(cmd));

  cmd_len = snprintf(cmd, sizeof(cmd), "./scomp -q -i - -o - < %s > %s 2>/dev/null", src_path, stdio_obj_path);
  ASSERT_TRUE(cmd_len > 0 && (size_t)cmd_len < sizeof(cmd));
  ASSERT_EQ_INT(0, system(cmd));

  cmd_len = snprintf(cmd, sizeof(cmd), "cmp %s %s >/dev/null", pos_obj_path, stdio_obj_path);
  ASSERT_TRUE(cmd_len > 0 && (size_t)cmd_len < sizeof(cmd));
  ASSERT_EQ_INT(0, system(cmd));

  remove(src_path);
  remove(pos_obj_path);
  remove(opt_obj_path);
  remove(stdio_obj_path);
  remove(help_path);
  remove(version_path);
}

static void test_scomp_cli_malformed_diagnostic_shape(void) {
  const char *src_path = "tests/fixtures/scomp-cli-malformed.tmp.src";
  const char *obj_path = "tests/fixtures/scomp-cli-malformed.tmp.obj";
  const char *err_path = "tests/fixtures/scomp-cli-malformed.tmp.err";
  FILE *src = fopen(src_path, "wb");
  ASSERT_NOT_NULL(src);
  const char *malformed = "@x = 1;\n@yy = 2;\n^;";
  ASSERT_EQ_INT((int)strlen(malformed), (int)fwrite(malformed, 1, strlen(malformed), src));
  ASSERT_EQ_INT(0, fclose(src));

  char cmd[512];
  int cmd_len = snprintf(cmd, sizeof(cmd), "./scomp %s %s > /dev/null 2> %s", src_path, obj_path, err_path);
  ASSERT_TRUE(cmd_len > 0 && (size_t)cmd_len < sizeof(cmd));
  ASSERT_TRUE(system(cmd) != 0);

  char *err = read_text_file_for_diag_test(err_path);
  ASSERT_TRUE(strstr(err, "Diagnostic SIN-PARSE-") != NULL);
  ASSERT_TRUE(strstr(err, "stage: PARSE") != NULL);
  ASSERT_TRUE(strstr(err, "file: tests/fixtures/scomp-cli-malformed.tmp.src") != NULL);
  ASSERT_TRUE(strstr(err, "line: 3") != NULL);
  ASSERT_TRUE(strstr(err, "column: 1") != NULL);
  ASSERT_TRUE(strstr(err, "message:") != NULL);
  ASSERT_TRUE(strstr(err, "legacy: ERR_") != NULL);
  ASSERT_TRUE(strstr(err, "source:") != NULL);
  ASSERT_TRUE(strstr(err, "    ^;") != NULL);
  ASSERT_TRUE(strstr(err, "    ^") != NULL);
  ASSERT_TRUE(strstr(err, "Diag: code=") == NULL);

  free(err);
  remove(src_path);
  remove(obj_path);
  remove(err_path);
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

void test_compiler_diag_pipeline(void){
  test_compiler_diag_repeated_set_reset_cycles();
  test_compiler_diag_rejects_256_locals();
  test_scomp_cli_malformed_diagnostic_shape();
  test_scomp_cli_options();
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
