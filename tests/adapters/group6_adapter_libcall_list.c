#include "test_framework.h"

void test_list_libcall_registry_contract(void);
void test_list_libcall_valid_operations_and_ownership(void);
void test_list_libcall_invalid_types_and_ranges(void);
void test_list_libcall_source_integration(void);
void test_list_libcall_islist(void);
void test_list_libcall_ordering(void);

static const TF_TestDescriptor tests[] = {
    {"test.runtime.test_list_libcall_registry_contract", test_list_libcall_registry_contract, "exclusive", 30000, "test.runtime.test_list_libcall_registry_contract"},
    {"test.runtime.test_list_libcall_valid_operations_and_ownership", test_list_libcall_valid_operations_and_ownership, "exclusive", 30000, "language.expression.list,api.libcall.list,libcall.list.length,libcall.list.get,libcall.list.append,libcall.list.set,libcall.list.concat,libcall.list.slice"},
    {"test.runtime.test_list_libcall_invalid_types_and_ranges", test_list_libcall_invalid_types_and_ranges, "exclusive", 30000, "api.libcall.list,libcall.list.get"},
    {"test.runtime.test_list_libcall_source_integration", test_list_libcall_source_integration, "exclusive", 30000, "api.libcall.list"},
    {"test.runtime.test_list_libcall_islist", test_list_libcall_islist, "exclusive", 30000, "libcall.list.islist"},
    {"test.runtime.test_list_libcall_ordering", test_list_libcall_ordering, "exclusive", 30000, "api.libcall.list,libcall.list.reverse,libcall.list.asc,libcall.list.desc"},
};

int main(int argc, char **argv) {
  return tf_main(argc, argv, tests, sizeof tests / sizeof tests[0]);
}
