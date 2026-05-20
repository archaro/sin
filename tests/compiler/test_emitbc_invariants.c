#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "error.h"
#include "ir.h"
#include "test_assert.h"
#include "test_helpers.h"

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

  OUTPUT_t out = make_out(16);
  char *errdetail = NULL;
  int8_t rc = t_emit_bytecode(unit, 7, 9, &out, &errdetail);
  ASSERT_EQ_INT(ERR_NOERROR, rc);
  ASSERT_TRUE(errdetail == NULL);

  size_t len = (size_t)(out.nextbyte - out.bytecode);
  ASSERT_EQ_INT(7, out.bytecode[0]);
  ASSERT_EQ_INT(9, out.bytecode[1]);
  ASSERT_EQ_INT(2 + expected_body_len, len);

  if (expect_jump_width) {
    ASSERT_TRUE(len >= 5);
    /* jump offset is always 16-bit little-endian. */
    (void)(out.bytecode[3] | (out.bytecode[4] << 8));
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
  assert_class_layout((IR_Inst){.op = IR_OP_PUSH_STRING, .imm = (int64_t)(intptr_t)"abc"}, 6, 0, 0);
  assert_class_layout((IR_Inst){.op = IR_OP_ADD}, 1, 0, 0);
  assert_class_layout((IR_Inst){.op = IR_OP_LOAD_LOCAL, .a = 4}, 2, 1, 0);
  assert_class_layout((IR_Inst){.op = IR_OP_LIBCALL_TOKEN, .a = 1}, 2, 1, 0);
  assert_class_layout((IR_Inst){.op = IR_OP_CALL, .a = 513}, 3, 2, 0);
  assert_class_layout((IR_Inst){.op = IR_OP_JUMP}, 3, 2, 1);
  assert_class_layout((IR_Inst){.op = IR_OP_JUMP_IF_FALSE}, 3, 2, 1);
  assert_class_layout((IR_Inst){.op = IR_OP_ITEM_PUSH_LAYER, .imm = (int64_t)(intptr_t)"xy"}, 4, 0, 0);

  IR_Unit *u = t_new_unit();
  ASSERT_NOT_NULL(u);
  IR_EmbeddedCodePayload payload = {.source = "code"};
  int32_t idx = ir_add_embedded_code_payload(u, payload);
  t_emit(u, (IR_Inst){.op = IR_OP_ITEM_SAVE_CODE, .a = idx});
  OUTPUT_t out = make_out(16);
  char *errdetail = NULL;
  ASSERT_EQ_INT(ERR_NOERROR, t_emit_bytecode(u, 1, 2, &out, &errdetail));
  ASSERT_TRUE(errdetail == NULL);
  ASSERT_EQ_INT(1, out.bytecode[0]);
  ASSERT_EQ_INT(2, out.bytecode[1]);
  ASSERT_EQ_INT(2 + 1 + 2 + 4, (size_t)(out.nextbyte - out.bytecode));
  free(out.bytecode);
  ir_destroy_unit(u);
}

static void emit_random_program(IR_Unit *u, uint32_t *seed, int count) {
  int32_t labels[8] = {0};
  size_t label_count = 0;
  for (int i = 0; i < count; i++) {
    uint32_t r = lcg_next(seed);
    switch (r % 8u) {
      case 0: t_emit(u, (IR_Inst){.op = IR_OP_PUSH_INT, .imm = (int64_t)(r & 0x7FFF)}); break;
      case 1: t_emit(u, (IR_Inst){.op = IR_OP_ADD}); break;
      case 2: t_emit(u, (IR_Inst){.op = IR_OP_LOAD_LOCAL, .a = (int32_t)(r % 8u)}); break;
      case 3: t_emit(u, (IR_Inst){.op = IR_OP_STORE_LOCAL, .a = (int32_t)(r % 8u)}); break;
      case 4: t_emit(u, (IR_Inst){.op = IR_OP_LIBCALL_TOKEN, .a = (int32_t)(r % 4u)}); break;
      case 5: {
        if (label_count < 8) labels[label_count++] = ir_new_label(u);
        t_emit(u, (IR_Inst){.op = IR_OP_ITEM_BEGIN});
      } break;
      case 6: t_emit(u, (IR_Inst){.op = IR_OP_ITEM_END}); break;
      case 7: {
        if (label_count > 0) {
          int32_t l = labels[r % label_count];
          if ((r & 1u) == 0) t_emit(u, (IR_Inst){.op = IR_OP_JUMP, .a = l});
          else t_emit(u, (IR_Inst){.op = IR_OP_JUMP_IF_FALSE, .a = l});
        } else {
          t_emit(u, (IR_Inst){.op = IR_OP_NEG});
        }
      } break;
    }
    if ((r % 11u) == 0 && label_count > 0) {
      int32_t l = labels[r % label_count];
      t_bind(u, l);
      t_emit(u, (IR_Inst){.op = IR_OP_LABEL, .a = l});
    }
  }
  for (size_t i = 0; i < label_count; i++) {
    t_bind(u, labels[i]);
    t_emit(u, (IR_Inst){.op = IR_OP_LABEL, .a = labels[i]});
  }
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
  ASSERT_EQ_INT(ERR_NOERROR, t_emit_bytecode(a, 3, 1, &oa, &errdetail));
  ASSERT_TRUE(errdetail == NULL);
  ASSERT_EQ_INT(ERR_NOERROR, t_emit_bytecode(b, 3, 1, &ob, &errdetail));
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

static void test_emitbc_label_heavy_jump_targets_in_bounds(void) {
  IR_Unit *u = t_new_unit();
  ASSERT_NOT_NULL(u);
  int32_t labels[32];
  for (int i = 0; i < 32; i++) labels[i] = ir_new_label(u);
  for (int i = 0; i < 32; i++) {
    t_bind(u, labels[i]);
    t_emit(u, (IR_Inst){.op = IR_OP_LABEL, .a = labels[i]});
    t_emit(u, (IR_Inst){.op = IR_OP_PUSH_INT, .imm = i});
    t_emit(u, (IR_Inst){.op = (i % 2 == 0) ? IR_OP_JUMP : IR_OP_JUMP_IF_FALSE, .a = labels[(i * 7) % 32]});
  }

  OUTPUT_t out = make_out(128);
  char *errdetail = NULL;
  ASSERT_EQ_INT(ERR_NOERROR, t_emit_bytecode(u, 0, 0, &out, &errdetail));
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
    if (op == 'p') pc += 8;
    else if (op == 'l' || op == 'B') {
      uint16_t n = (uint16_t)out.bytecode[pc] | ((uint16_t)out.bytecode[pc + 1] << 8);
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
}
