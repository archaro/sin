#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "error.h"
#include "compiler/ir.h"
#include "test_assert.h"
#include "test_helpers.h"
#include "bytecode_format.h"

enum { TEST_SEED = 0x5EED1234u };

static uint32_t lcg_next(uint32_t *state) {
  *state = (*state * 1664525u) + 1013904223u;
  return *state;
}

static OUTPUT_t make_out(size_t cap) {
  OUTPUT_t out = {0};
  out.maxsize = cap;
  out.bytecode = malloc(cap);
  out.nextbyte = out.bytecode;
  ASSERT_NOT_NULL(out.bytecode);
  return out;
}

static void assert_class_layout(IR_Inst inst, size_t expected_body_len, size_t expected_arity_bytes,
                                int expect_jump_width) {
  IR_Unit *unit = t_new_unit();
  ASSERT_NOT_NULL(unit);
  if (inst.op == IR_OP_JUMP || inst.op == IR_OP_JUMP_IF_FALSE) {
    int32_t l = ir_new_label(unit);
    inst.a = l;
    t_emit(unit, inst);
    t_bind(unit, l);
    t_emit(unit, (IR_Inst){.op = IR_OP_LABEL, .a = l});
  } else {
    t_emit(unit, inst);
  }
  if (inst.op != IR_OP_HALT) {
    t_emit(unit, (IR_Inst){.op = IR_OP_HALT});
  }

  OUTPUT_t out = make_out(16);
  char *errdetail = NULL;
  int8_t rc = t_emit_bytecode(unit, UINT8_MAX, UINT8_MAX, &out, &errdetail);
  ASSERT_EQ_INT(ERR_NOERROR, rc);
  ASSERT_TRUE(errdetail == NULL);

  size_t len = (size_t)(out.nextbyte - out.bytecode);
  ASSERT_EQ_INT(UINT8_MAX, out.bytecode[6]);
  ASSERT_EQ_INT(UINT8_MAX, out.bytecode[7]);
  ASSERT_EQ_INT(BC_V1_HEADER_SIZE + expected_body_len + (inst.op == IR_OP_HALT ? 0 : 1), len);

  if (expect_jump_width) {
    ASSERT_TRUE(len >= 5);
    /* jump offset is always 16-bit little-endian. */
    (void)(out.bytecode[BC_V1_HEADER_SIZE + 1] | (out.bytecode[BC_V1_HEADER_SIZE + 2] << 8));
  }
  if (expected_arity_bytes > 0) {
    ASSERT_TRUE(len >= 3 + expected_arity_bytes);
  }

  free(out.bytecode);
  ir_destroy_unit(unit);
}

static void test_emitbc_op_class_invariants(void) {
  assert_class_layout((IR_Inst){.op = IR_OP_HALT}, 1, 0, 0);
  assert_class_layout((IR_Inst){.op = IR_OP_PUSH_INT, .imm = 123}, 9, 0, 0);
  assert_class_layout((IR_Inst){.op = IR_OP_PUSH_FLOAT, .imm = (int64_t)UINT64_C(0x7ff8000000000042)}, 9, 0, 0);
  assert_class_layout((IR_Inst){.op = IR_OP_PUSH_STRING, .imm = (int64_t)(intptr_t)"abc"}, 6, 0, 0);
  assert_class_layout((IR_Inst){.op = IR_OP_ADD}, 1, 0, 0);
  assert_class_layout((IR_Inst){.op = IR_OP_LOAD_LOCAL, .a = 4}, 2, 1, 0);
  assert_class_layout((IR_Inst){.op = IR_OP_LIBCALL, .a = 1, .b = 0}, 3, 2, 1);
  assert_class_layout((IR_Inst){.op = IR_OP_CALL, .a = 2}, 3, 2, 0);
  assert_class_layout((IR_Inst){.op = IR_OP_JUMP}, 3, 2, 1);
  assert_class_layout((IR_Inst){.op = IR_OP_JUMP_IF_FALSE}, 3, 2, 1);

  IR_Unit *u = t_new_unit();
  ASSERT_NOT_NULL(u);
  IR_EmbeddedCodePayload payload = {.source = "code"};
  int32_t idx = ir_add_embedded_code_payload(u, payload);
  t_emit(u, (IR_Inst){.op = IR_OP_ITEM_SAVE_CODE, .a = idx});
  t_emit(u, (IR_Inst){.op = IR_OP_HALT});
  OUTPUT_t out = make_out(16);
  char *errdetail = NULL;
  ASSERT_EQ_INT(ERR_NOERROR, t_emit_bytecode(u, 2, 2, &out, &errdetail));
  ASSERT_TRUE(errdetail == NULL);
  ASSERT_EQ_INT(2, out.bytecode[6]);
  ASSERT_EQ_INT(2, out.bytecode[7]);
  ASSERT_EQ_INT(BC_V1_HEADER_SIZE + 1 + 2 + 4 + 1, (size_t)(out.nextbyte - out.bytecode));
  free(out.bytecode);
  ir_destroy_unit(u);
}

static void emit_random_program(IR_Unit *u, uint32_t *seed, int count) {
  for (int i = 0; i < count; i++) {
    uint32_t r = lcg_next(seed);
    switch (r % 6u) {
      case 0: t_emit(u, (IR_Inst){.op = IR_OP_PUSH_INT, .imm = (int64_t)(r & 0x7FFF)}); break;
      case 1: t_emit(u, (IR_Inst){.op = IR_OP_PUSH_FLOAT, .imm = (int64_t)r}); break;
      case 2: t_emit(u, (IR_Inst){.op = IR_OP_LOAD_LOCAL, .a = (int32_t)(r % 8u)}); break;
      case 3: t_emit(u, (IR_Inst){.op = IR_OP_PUSH_BOOL, .a = (int32_t)(r & 1u)}); break;
      case 4: t_emit(u, (IR_Inst){.op = IR_OP_LIBCALL, .a = 1, .b = (int32_t)(r % 4u)}); break;
      case 5: t_emit(u, (IR_Inst){.op = IR_OP_INC_LOCAL, .a = (int32_t)(r % 8u)}); break;
    }
  }
  t_emit(u, (IR_Inst){.op = IR_OP_HALT});
}

static void test_emitbc_determinism_fixed_seed(void) {
  printf("emitbc invariant seed=0x%08X\n", TEST_SEED);
  IR_Unit *a = t_new_unit();
  IR_Unit *b = t_new_unit();
  ASSERT_NOT_NULL(a);
  ASSERT_NOT_NULL(b);
  uint32_t sa = TEST_SEED;
  uint32_t sb = TEST_SEED;
  emit_random_program(a, &sa, 200);
  emit_random_program(b, &sb, 200);

  OUTPUT_t oa = make_out(32);
  OUTPUT_t ob = make_out(32);
  char *errdetail = NULL;
  ASSERT_EQ_INT(ERR_NOERROR, t_emit_bytecode(a, 8, 0, &oa, &errdetail));
  ASSERT_TRUE(errdetail == NULL);
  ASSERT_EQ_INT(ERR_NOERROR, t_emit_bytecode(b, 8, 0, &ob, &errdetail));
  ASSERT_TRUE(errdetail == NULL);

  size_t lena = (size_t)(oa.nextbyte - oa.bytecode);
  size_t lenb = (size_t)(ob.nextbyte - ob.bytecode);
  ASSERT_EQ_INT(lena, lenb);
  ASSERT_EQ_INT(0, memcmp(oa.bytecode, ob.bytecode, lena));

  free(oa.bytecode);
  free(ob.bytecode);
  ir_destroy_unit(a);
  ir_destroy_unit(b);
}

static void test_emitbc_successful_emission_verifies(void) {
  IR_Unit *unit = t_new_unit();
  ASSERT_NOT_NULL(unit);
  t_emit(unit, (IR_Inst){.op = IR_OP_PUSH_INT, .imm = 42});
  t_emit(unit, (IR_Inst){.op = IR_OP_HALT});

  OUTPUT_t out = make_out(16);
  char *errdetail = NULL;
  int8_t rc = t_emit_bytecode(unit, 0, 0, &out, &errdetail);
  ASSERT_EQ_INT(ERR_NOERROR, rc);
  ASSERT_TRUE(errdetail == NULL);
  ASSERT_EQ_INT('h', out.bytecode[(out.nextbyte - out.bytecode) - 1]);

  free(out.bytecode);
  ir_destroy_unit(unit);
}

static void test_emitbc_label_heavy_jump_targets_in_bounds(void) {
  IR_Unit *u = t_new_unit();
  ASSERT_NOT_NULL(u);
  int32_t labels[32];
  for (int i = 0; i < 32; i++) labels[i] = ir_new_label(u);
  for (int i = 0; i < 32; i++) {
    t_bind(u, labels[i]);
    t_emit(u, (IR_Inst){.op = IR_OP_LABEL, .a = labels[i]});
    t_emit(u, (IR_Inst){.op = IR_OP_PUSH_INT, .imm = i});
    t_emit(u, (IR_Inst){.op = IR_OP_STORE_LOCAL, .a = 0});
    if (i + 1 < 32) {
      t_emit(u, (IR_Inst){.op = IR_OP_JUMP, .a = labels[i + 1]});
    }
  }
  t_emit(u, (IR_Inst){.op = IR_OP_HALT});

  OUTPUT_t out = make_out(128);
  char *errdetail = NULL;
  ASSERT_EQ_INT(ERR_NOERROR, t_emit_bytecode(u, 1, 0, &out, &errdetail));
  ASSERT_TRUE(errdetail == NULL);

  size_t len = (size_t)(out.nextbyte - out.bytecode);
  size_t pc = 2;
  while (pc < len) {
    uint8_t op = out.bytecode[pc++];
    if (op == 'j' || op == 'k') {
      ASSERT_TRUE(pc + 1 < len);
      int16_t diff = (int16_t)((uint16_t)out.bytecode[pc] | ((uint16_t)out.bytecode[pc + 1] << 8));
      long target = (long)pc + (long)diff;
      ASSERT_TRUE(target >= 2);
      ASSERT_TRUE((size_t)target < len);
      pc += 2;
      continue;
    }
    if (op == 'p' || op == 'P') pc += 8;
    else if (op == 'l' || op == 'B') {
      uint16_t n = (uint16_t)(((uint16_t)out.bytecode[pc]) | ((uint16_t)out.bytecode[pc + 1] << 8));
      pc += 2 + n;
    } else if (op == 'e' || op == 'c' || op == 'f' || op == 'g') pc += 1;
    else if (op == 'F') pc += 2;
    else if (op == 'M') pc += 1;
    else if (op == 'L') {
      uint8_t n = out.bytecode[pc++];
      pc += n;
    }
  }
  ASSERT_EQ_INT(len, pc);

  free(out.bytecode);
  ir_destroy_unit(u);
}

void test_emitbc_invariants(void) {
  test_emitbc_op_class_invariants();
  test_emitbc_determinism_fixed_seed();
  test_emitbc_label_heavy_jump_targets_in_bounds();
  test_emitbc_successful_emission_verifies();
}
