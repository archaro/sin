#include <stdint.h>
#include <stdlib.h>

#include "ir.h"
#include "test_assert.h"
#include "test_helpers.h"

static void run_header_case(uint8_t local_count, uint8_t param_count,
                            size_t expected_total_len, int add_halt) {
  IR_Unit *unit = t_new_unit();
  ASSERT_NOT_NULL(unit);

  if (add_halt) {
    t_emit(unit, (IR_Inst){.op = IR_OP_HALT});
  }

  OUTPUT_t out = {0};
  out.maxsize = 2;
  out.bytecode = malloc(out.maxsize);
  out.nextbyte = out.bytecode;
  ASSERT_NOT_NULL(out.bytecode);

  char *errdetail = NULL;
  int8_t rc = t_emit_bytecode(unit, local_count, param_count, &out, &errdetail);
  ASSERT_EQ_INT(0, rc);
  ASSERT_TRUE(errdetail == NULL);

  size_t out_len = (size_t)(out.nextbyte - out.bytecode);
  ASSERT_TRUE(out_len >= 2);
  ASSERT_EQ_INT(local_count, out.bytecode[0]);
  ASSERT_EQ_INT(param_count, out.bytecode[1]);
  ASSERT_EQ_INT(expected_total_len, out_len);

  if (add_halt) {
    ASSERT_EQ_INT('h', out.bytecode[2]);
  }

  free(out.bytecode);
  ir_destroy_unit(unit);
}

void test_emitbc_header(void) {
  run_header_case(0, 0, 2, 0);
  run_header_case(UINT8_MAX, UINT8_MAX, 2, 0);
  run_header_case(3, 5, 3, 1);
}
