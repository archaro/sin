#include <string.h>
#include "compiler_pipeline.h"
#include "error.h"
#include "test_assert.h"

void test_compiler_diag_pipeline(void){
  OUTPUT_t *out=NULL; CompilerDiagnostic d; compiler_diag_init(&d);
  int8_t rc = compile_source_to_bytecode_diag("^;",2,&out,&d);
  ASSERT_EQ_INT(ERR_COMP_UNKNOWNCHAR, rc); ASSERT_EQ_INT(DIAG_PHASE_PARSE, d.phase); ASSERT_NOT_NULL(d.message);
  compiler_diag_reset(&d);
  rc = compile_source_to_bytecode_diag("@x;",3,&out,&d);
  ASSERT_EQ_INT(ERR_COMP_LOCALBEFOREDEF, rc); ASSERT_EQ_INT(DIAG_PHASE_SEMANT, d.phase); ASSERT_NOT_NULL(d.message);
  compiler_diag_reset(&d);
}
