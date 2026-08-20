#include "test_framework.h"

void test_list_libcall_registry_contract(void);
void test_list_libcall_valid_operations_and_ownership(void);
void test_list_libcall_invalid_types_and_ranges(void);
void test_list_libcall_source_integration(void);
void test_list_libcall_islist(void);

static const TF_TestDescriptor tests[] = {
    {"rewrite.runtime.test_list_libcall_registry_contract", test_list_libcall_registry_contract, "exclusive", 30000, "baseline.legacy.unified.runtime.test_list_libcall_registry_contract"},
    {"rewrite.runtime.test_list_libcall_valid_operations_and_ownership", test_list_libcall_valid_operations_and_ownership, "exclusive", 30000, "api.libcall.list,baseline.legacy.unified.runtime.test_list_libcall_valid_operations_and_ownership,language.expression.list,libcall.list.append,libcall.list.concat,libcall.list.get,libcall.list.length,libcall.list.set,libcall.list.slice"},
    {"rewrite.runtime.test_list_libcall_invalid_types_and_ranges", test_list_libcall_invalid_types_and_ranges, "exclusive", 30000, "api.libcall.list,baseline.legacy.unified.runtime.test_list_libcall_invalid_types_and_ranges,libcall.list.get"},
    {"rewrite.runtime.test_list_libcall_source_integration", test_list_libcall_source_integration, "exclusive", 30000, "api.libcall.list,baseline.legacy.unified.runtime.test_list_libcall_source_integration"},
    {"rewrite.runtime.test_list_libcall_islist", test_list_libcall_islist, "exclusive", 30000, "baseline.legacy.unified.runtime.test_list_libcall_islist,libcall.list.islist"},
};

int main(int argc, char **argv) {
  return tf_main(argc, argv, tests, sizeof tests / sizeof tests[0]);
}
