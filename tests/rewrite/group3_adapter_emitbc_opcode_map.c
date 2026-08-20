#include "test_framework.h"

void test_emitbc_libcall_pair_bytes(void);
void test_emitbc_opcode_map(void);
void test_emitbc_lists_and_itemrefs_emission(void);
void test_emitbc_push_float_immediate_layout(void);
void test_emitbc_push_int_immediate_layout(void);
void test_emitbc_opcode_map_call_item_deref_alias_layout(void);
void test_emitbc_opcode_map_unsupported_ir_op(void);

static const TF_TestDescriptor tests[] = {
    {"rewrite.compiler.test_emitbc_libcall_pair_bytes", test_emitbc_libcall_pair_bytes, "", 30000,
     "baseline.legacy.unified.compiler.test_emitbc_libcall_pair_bytes,bytecode.encoding.libcall,bytecode.ir.ir_op_libcall,bytecode.opcode.libcall,bytecode.verifier.libcall-pair"},
    {"rewrite.compiler.test_emitbc_opcode_map", test_emitbc_opcode_map, "exclusive", 30000,
     "baseline.legacy.unified.compiler.test_emitbc_opcode_map,bytecode.ir.ir_op_add,bytecode.ir.ir_op_and,bytecode.ir.ir_op_build_list,bytecode.ir.ir_op_call,bytecode.ir.ir_op_dec_local,bytecode.ir.ir_op_discard,bytecode.ir.ir_op_div,bytecode.ir.ir_op_eq,bytecode.ir.ir_op_ge,bytecode.ir.ir_op_gt,bytecode.ir.ir_op_halt,bytecode.ir.ir_op_inc_local,bytecode.ir.ir_op_item_begin,bytecode.ir.ir_op_item_begin_rel,bytecode.ir.ir_op_item_deref,bytecode.ir.ir_op_item_end,bytecode.ir.ir_op_item_push_deref,bytecode.ir.ir_op_item_push_deref_local,bytecode.ir.ir_op_item_push_layer,bytecode.ir.ir_op_item_save,bytecode.ir.ir_op_item_save_code,bytecode.ir.ir_op_jump,bytecode.ir.ir_op_jump_if_false,bytecode.ir.ir_op_label,bytecode.ir.ir_op_le,bytecode.ir.ir_op_libcall,bytecode.ir.ir_op_load_local,bytecode.ir.ir_op_lt,bytecode.ir.ir_op_make_itemref,bytecode.ir.ir_op_mod,bytecode.ir.ir_op_mul,bytecode.ir.ir_op_neg,bytecode.ir.ir_op_neq,bytecode.ir.ir_op_not,bytecode.ir.ir_op_or,bytecode.ir.ir_op_push_bool,bytecode.ir.ir_op_push_float,bytecode.ir.ir_op_push_int,bytecode.ir.ir_op_push_nil,bytecode.ir.ir_op_push_string,bytecode.ir.ir_op_return,bytecode.ir.ir_op_store_local,bytecode.ir.ir_op_sub,bytecode.opcode.add,bytecode.opcode.and,bytecode.opcode.build_list,bytecode.opcode.call,bytecode.opcode.dec_local,bytecode.opcode.discard,bytecode.opcode.div,bytecode.opcode.eq,bytecode.opcode.ge,bytecode.opcode.gt,bytecode.opcode.halt,bytecode.opcode.inc_local,bytecode.opcode.item_begin,bytecode.opcode.item_begin_rel,bytecode.opcode.item_deref,bytecode.opcode.item_end,bytecode.opcode.item_push_deref,bytecode.opcode.item_push_deref_local,bytecode.opcode.item_push_layer,bytecode.opcode.item_save,bytecode.opcode.item_save_code,bytecode.opcode.jump,bytecode.opcode.jump_if_false,bytecode.opcode.label,bytecode.opcode.le,bytecode.opcode.libcall,bytecode.opcode.load_local,bytecode.opcode.lt,bytecode.opcode.make_itemref,bytecode.opcode.mod,bytecode.opcode.mul,bytecode.opcode.neg,bytecode.opcode.neq,bytecode.opcode.not,bytecode.opcode.or,bytecode.opcode.push_bool,bytecode.opcode.push_float,bytecode.opcode.push_int,bytecode.opcode.push_nil,bytecode.opcode.push_string,bytecode.opcode.return,bytecode.opcode.store_local,bytecode.opcode.sub"},
    {"rewrite.compiler.test_emitbc_lists_and_itemrefs_emission", test_emitbc_lists_and_itemrefs_emission, "exclusive", 30000,
     "baseline.legacy.unified.compiler.test_emitbc_lists_and_itemrefs_emission,bytecode.encoding.item"},
    {"rewrite.compiler.test_emitbc_push_float_immediate_layout", test_emitbc_push_float_immediate_layout, "", 30000,
     "baseline.legacy.unified.compiler.test_emitbc_push_float_immediate_layout,bytecode.encoding.float,bytecode.ir.ir_op_push_float,bytecode.opcode.push_float"},
    {"rewrite.compiler.test_emitbc_push_int_immediate_layout", test_emitbc_push_int_immediate_layout, "", 30000,
     "baseline.legacy.unified.compiler.test_emitbc_push_int_immediate_layout,bytecode.encoding.integer,bytecode.ir.ir_op_push_int,bytecode.opcode.push_int"},
    {"rewrite.compiler.test_emitbc_opcode_map_call_item_deref_alias_layout", test_emitbc_opcode_map_call_item_deref_alias_layout, "exclusive", 30000,
     "baseline.legacy.unified.compiler.test_emitbc_opcode_map_call_item_deref_alias_layout"},
    {"rewrite.compiler.test_emitbc_opcode_map_unsupported_ir_op", test_emitbc_opcode_map_unsupported_ir_op, "exclusive", 30000,
     "baseline.legacy.unified.compiler.test_emitbc_opcode_map_unsupported_ir_op"},
};

int main(int argc, char **argv) {
  return tf_main(argc, argv, tests, sizeof tests / sizeof tests[0]);
}
