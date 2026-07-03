#include <string.h>
#include <stdlib.h>
#include "compiler_pipeline.h"
#include "semant.h"
#include "lower.h"
#include "ir.h"
#include "error.h"
#include "test_assert.h"

void test_compiler_diag_pipeline(void){
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
  free(errdetail);

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
  free(errdetail);
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
  free(errdetail);
  as_delete(ast);
  sem_delete_ctx(sem);

  compiler_diag_reset(&d);
  IR_Unit *ir = NULL;
  errdetail = NULL;
  rc = lower_ast_to_ir_diag(NULL, NULL, NULL, &errdetail, &d);
  ASSERT_TRUE(rc != ERR_NOERROR);
  (void)ir;
  ASSERT_EQ_INT(DIAG_PHASE_LOWER, d.phase);
  free(errdetail);

  compiler_diag_reset(&d);
  errdetail = NULL;
  rc = ir_validate_diag(NULL, 0, &errdetail, &d);
  ASSERT_EQ_INT(ERR_COMP_SYNTAX, rc);
  ASSERT_EQ_INT(DIAG_PHASE_IR_VALIDATE, d.phase);
  free(errdetail);

  compiler_diag_reset(&d);
  errdetail = NULL;
  rc = emit_bytecode_diag(NULL, 0, 0, NULL, &errdetail, &d);
  ASSERT_EQ_INT(ERR_COMP_SYNTAX, rc);
  ASSERT_EQ_INT(DIAG_PHASE_EMITBC, d.phase);
  free(errdetail);

  compiler_diag_reset(&d);
  rc = compile_source_to_bytecode_diag(NULL, 0, &out, &d);
  ASSERT_EQ_INT(ERR_COMP_SYNTAX, rc);
  ASSERT_EQ_INT(DIAG_PHASE_COMPILE, d.phase);

  compiler_diag_reset(&d);
}
