#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "compiler_pipeline.h"
#include "compdiag.h"
#include "semant.h"
#include "lower.h"
#include "ir.h"
#include "error.h"
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

  compiler_diag_reset(&d);
  const char *unknown_source = "@x = 1;\n@yy = 2;\n☃;";
  rc = compile_source_to_bytecode_diag(unknown_source, strlen(unknown_source), &out, &d);
  ASSERT_EQ_INT(ERR_COMP_UNKNOWNCHAR, rc);
  ASSERT_EQ_INT(DIAG_PHASE_PARSE, d.phase);
  ASSERT_EQ_INT(3, d.line);
  ASSERT_EQ_INT(1, d.column);
  ASSERT_EQ_INT(1, d.span);
  ASSERT_TRUE(d.has_loc);

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
