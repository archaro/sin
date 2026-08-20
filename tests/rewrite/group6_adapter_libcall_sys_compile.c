#include "test_framework.h"

void test_sys_itemref_dynamic_calls(void);
void test_sys_compile_libcall_runtime(void);

static const TF_TestDescriptor tests[] = {
    {"rewrite.runtime.test_sys_compile_libcall_runtime", test_sys_compile_libcall_runtime, "exclusive", 30000, "api.libcall.sys,baseline.legacy.unified.runtime.test_sys_compile_libcall_runtime,language.expression.libcall,libcall.sys.compile"},
    {"rewrite.runtime.test_sys_itemref_dynamic_calls", test_sys_itemref_dynamic_calls, "exclusive", 30000, "baseline.legacy.unified.runtime.test_sys_itemref_dynamic_calls,bytecode.encoding.item,language.expression.item-reference,language.expression.libcall"},
};

int main(int argc, char **argv) {
  return tf_main(argc, argv, tests, sizeof tests / sizeof tests[0]);
}
