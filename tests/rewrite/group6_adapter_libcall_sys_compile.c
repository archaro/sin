#include "test_framework.h"

void test_sys_itemref_dynamic_calls(void);
void test_sys_compile_libcall_runtime(void);
void test_sys_compile_rejects_error_namespace_mutations(void);

static const TF_TestDescriptor tests[] = {
    {"rewrite.runtime.test_sys_compile_rejects_error_namespace_mutations", test_sys_compile_rejects_error_namespace_mutations,
     "exclusive", 30000, "api.libcall.sys,api.runtime.runtime-item-ops,api.itemstore.item-error"},
    {"rewrite.runtime.test_sys_compile_libcall_runtime", test_sys_compile_libcall_runtime, "exclusive", 30000, "language.expression.libcall,api.libcall.sys,libcall.sys.compile"},
    {"rewrite.runtime.test_sys_itemref_dynamic_calls", test_sys_itemref_dynamic_calls, "exclusive", 30000, "language.expression.libcall,language.expression.item-reference,bytecode.encoding.item"},
};

int main(int argc, char **argv) {
  return tf_main(argc, argv, tests, sizeof tests / sizeof tests[0]);
}
