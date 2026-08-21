#include "test_framework.h"

void test_ir_validate(void);

static const TF_TestDescriptor tests[] = {
    {"rewrite.core.test_ir_validate", test_ir_validate, "exclusive", 30000,
     "bytecode.ast.n_add,bytecode.ast.n_sub,bytecode.ast.n_mul,bytecode.ast.n_div,bytecode.ast.n_mod,bytecode.ast.n_equal,bytecode.ast.n_noteq,bytecode.ast.n_lt,bytecode.ast.n_lteq,bytecode.ast.n_gt,bytecode.ast.n_gteq,api.compiler.ir-unit"},
};

int main(int argc, char **argv) {
  return tf_main(argc, argv, tests, sizeof tests / sizeof tests[0]);
}
