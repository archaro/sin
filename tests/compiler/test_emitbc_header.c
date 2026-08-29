#include <stdint.h>
#include <stdlib.h>

#include "compiler/ir.h"
#include "compiler/compdiag.h"
#include "test_assert.h"
#include "test_helpers.h"

static void run_header_case(uint8_t local_count, uint8_t param_count) {
  IR_Unit *unit = t_new_unit();
  ASSERT_NOT_NULL(unit);

  t_emit(unit, (IR_Inst){.op = IR_OP_HALT});

  OUTPUT_t out = {0};
  out.maxsize = 2;
  out.bytecode = malloc(out.maxsize);
  out.nextbyte = out.bytecode;
  ASSERT_NOT_NULL(out.bytecode);

  CompilerDiagnostic diag;
  compiler_diag_init(&diag);
  int8_t rc = t_emit_bytecode_diag(unit, local_count, param_count, &out, &diag);
  ASSERT_EQ_INT(0, rc);
  ASSERT_TRUE(diag.message == NULL);

  size_t out_len = (size_t)(out.nextbyte - out.bytecode);
  ASSERT_TRUE(out_len >= 8);
  ASSERT_EQ_INT(0, out.bytecode[0]);
  ASSERT_EQ_INT(0xff, out.bytecode[1]);
  ASSERT_EQ_INT('S', out.bytecode[2]);
  ASSERT_EQ_INT('B', out.bytecode[3]);
  ASSERT_EQ_INT(1, out.bytecode[4]);
  ASSERT_EQ_INT(0, out.bytecode[5]);
  ASSERT_EQ_INT(local_count, out.bytecode[6]);
  ASSERT_EQ_INT(param_count, out.bytecode[7]);
  ASSERT_EQ_INT(9, out_len);
  ASSERT_EQ_INT('h', out.bytecode[8]);

  free(out.bytecode);
  compiler_diag_reset(&diag);
  ir_destroy_unit(unit);
}

void test_emitbc_header(void) {
  run_header_case(0, 0);
  run_header_case(UINT8_MAX, UINT8_MAX);
  run_header_case(5, 3);
}
