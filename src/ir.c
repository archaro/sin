// Intermediate representation for bytecode/codegen emit stage.
//
// Licensed under the MIT License - see LICENSE file for details.

#include "ir.h"

#include <stdlib.h>

#include "memory.h"

static void ir_grow_code(IR_CTX *fn) {
  if (fn->count < fn->capacity) {
    return;
  }
  uint32_t oldcap = fn->capacity;
  fn->capacity = GROW_CAPACITY(oldcap);
  fn->code = GROW_ARRAY(IR_INST, fn->code, oldcap, fn->capacity);
}

static void ir_grow_labels(IR_CTX *fn) {
  if (fn->label_count < fn->label_capacity) {
    return;
  }
  uint16_t oldcap = fn->label_capacity;
  fn->label_capacity = GROW_CAPACITY(oldcap);
  fn->labels = GROW_ARRAY(IR_LABEL, fn->labels, oldcap, fn->label_capacity);
}

static void ir_grow_fixups(IR_CTX *fn) {
  if (fn->fixup_count < fn->fixup_capacity) {
    return;
  }
  uint32_t oldcap = fn->fixup_capacity;
  fn->fixup_capacity = GROW_CAPACITY(oldcap);
  fn->fixups = GROW_ARRAY(IR_FIXUP, fn->fixups, oldcap, fn->fixup_capacity);
}

IR_CTX *ir_new(void) {
  IR_CTX *fn = GROW_ARRAY(IR_CTX, NULL, 0, 1);
  fn->code = NULL;
  fn->count = 0;
  fn->capacity = 0;

  fn->labels = NULL;
  fn->label_count = 0;
  fn->label_capacity = 0;

  fn->fixups = NULL;
  fn->fixup_count = 0;
  fn->fixup_capacity = 0;

  fn->max_stack = 0;

  return fn;
}

void ir_free(IR_CTX *fn) {
  if (!fn) {
    return;
  }
  FREE_ARRAY(IR_INST, fn->code, fn->capacity);
  FREE_ARRAY(IR_LABEL, fn->labels, fn->label_capacity);
  FREE_ARRAY(IR_FIXUP, fn->fixups, fn->fixup_capacity);
  FREE_ARRAY(IR_CTX, fn, 1);
}

uint16_t ir_new_label(IR_CTX *fn) {
  ir_grow_labels(fn);
  uint16_t label = fn->label_count;
  fn->labels[label].bound = false;
  fn->labels[label].inst_index = 0;
  fn->label_count++;
  return label;
}

void ir_bind_label(IR_CTX *fn, uint16_t label) {
  if (label >= fn->label_count) {
    return;
  }
  fn->labels[label].bound = true;
  fn->labels[label].inst_index = fn->count;
}

static void ir_emit_inst(IR_CTX *fn, IR_INST inst) {
  ir_grow_code(fn);
  fn->code[fn->count++] = inst;
}

void ir_emit_op(IR_CTX *fn, IR_OP op) {
  IR_INST inst;
  inst.op = op;
  inst.operand.i64 = 0;
  ir_emit_inst(fn, inst);
}

void ir_emit_i64(IR_CTX *fn, IR_OP op, int64_t value) {
  IR_INST inst;
  inst.op = op;
  inst.operand.i64 = value;
  ir_emit_inst(fn, inst);
}

void ir_emit_sid(IR_CTX *fn, IR_OP op, const char *sid) {
  IR_INST inst;
  inst.op = op;
  inst.operand.sid = sid;
  ir_emit_inst(fn, inst);
}

void ir_emit_local(IR_CTX *fn, IR_OP op, uint8_t local) {
  IR_INST inst;
  inst.op = op;
  inst.operand.local = local;
  ir_emit_inst(fn, inst);
}

void ir_emit_label(IR_CTX *fn, IR_OP op, uint16_t label) {
  IR_INST inst;
  inst.op = op;
  inst.operand.label = label;
  ir_emit_inst(fn, inst);

  ir_grow_fixups(fn);
  fn->fixups[fn->fixup_count].inst_index = fn->count - 1;
  fn->fixups[fn->fixup_count].label = label;
  fn->fixup_count++;
}

void ir_emit_argc(IR_CTX *fn, IR_OP op, uint8_t argc) {
  IR_INST inst;
  inst.op = op;
  inst.operand.argc = argc;
  ir_emit_inst(fn, inst);
}

bool ir_patch_labels(IR_CTX *fn) {
  for (uint32_t i = 0; i < fn->fixup_count; i++) {
    IR_FIXUP *fixup = &fn->fixups[i];
    if (fixup->label >= fn->label_count || !fn->labels[fixup->label].bound) {
      return false;
    }
    fn->code[fixup->inst_index].operand.label = fn->labels[fixup->label].inst_index;
  }
  return true;
}
