#include "test_framework.h"

void test_sys_source_libcall(void);
void test_sys_item_libcalls(void);
void test_sys_delete_rejects_error_namespace(void);
void test_sys_itemref_contracts(void);
void test_sys_persistence_libcalls(void);
void test_sys_introspection_libcalls(void);
void test_sys_wall_milliseconds_boundaries(void);
void test_sys_caller_paramcount_libcalls(void);

static const TF_TestDescriptor tests[] = {
    {"rewrite.runtime.test_sys_delete_rejects_error_namespace", test_sys_delete_rejects_error_namespace,
     "exclusive", 30000, "api.libcall.sys,api.itemstore.item-error"},
    {"rewrite.runtime.test_sys_item_libcalls", test_sys_item_libcalls,
     "exclusive", 30000,
     "api.libcall.sys,libcall.sys.exists,libcall.sys.delete,libcall.sys.nthname,libcall.sys.rootname,libcall.sys.itemtype,libcall.sys.childcount,libcall.sys.call"},
    {"rewrite.runtime.test_sys_itemref_contracts", test_sys_itemref_contracts,
     "exclusive", 30000,
     "libcall.sys.thisitem,libcall.sys.parentitem,libcall.sys.itemref,libcall.sys.itemname,libcall.sys.fetch"},
    {"rewrite.runtime.test_sys_persistence_libcalls", test_sys_persistence_libcalls,
     "exclusive", 30000,
     "language.item-syntax.item-save,api.libcall.sys,libcall.sys.backup,libcall.sys.save"},
    {"rewrite.runtime.test_sys_introspection_libcalls", test_sys_introspection_libcalls,
     "exclusive", 30000,
     "libcall.sys.rootcount,libcall.sys.version"},
    {"rewrite.runtime.test_sys_wall_milliseconds_boundaries", test_sys_wall_milliseconds_boundaries,
     "exclusive", 30000,
     "libcall.sys.now,libcall.sys.monotime"},
    {"rewrite.runtime.test_sys_caller_paramcount_libcalls", test_sys_caller_paramcount_libcalls,
     "exclusive", 30000,
     "libcall.sys.calleritem,libcall.sys.paramcount"},
    {"rewrite.runtime.test_sys_source_libcall", test_sys_source_libcall,
     "exclusive", 30000,
     "api.common.logging,libcall.sys.source"},
};

int main(int argc, char **argv) {
  return tf_main(argc, argv, tests, sizeof tests / sizeof tests[0]);
}
