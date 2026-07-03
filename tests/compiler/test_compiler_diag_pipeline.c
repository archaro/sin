#include <string.h>
#include "compiler_pipeline.h"
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
}
