#include "test_framework.h"

void test_sys_source_libcall(void);
void test_sys_item_libcalls(void);
void test_sys_itemref_contracts(void);
void test_sys_persistence_libcalls(void);
void test_sys_introspection_libcalls(void);
void test_sys_wall_milliseconds_boundaries(void);
void test_sys_caller_paramcount_libcalls(void);

static const TF_TestDescriptor tests[] = {
    {"rewrite.runtime.test_sys_item_libcalls", test_sys_item_libcalls,
     "exclusive", 30000,
     "api.libcall.sys,baseline.legacy.unified.runtime.test_sys_item_libcalls,libcall.sys.call,libcall.sys.childcount,libcall.sys.delete,libcall.sys.exists,libcall.sys.itemtype,libcall.sys.nthname,libcall.sys.rootname"},
    {"rewrite.runtime.test_sys_itemref_contracts", test_sys_itemref_contracts,
     "exclusive", 30000,
     "baseline.legacy.unified.runtime.test_sys_itemref_contracts,libcall.sys.fetch,libcall.sys.itemname,libcall.sys.itemref,libcall.sys.parentitem,libcall.sys.thisitem"},
    {"rewrite.runtime.test_sys_persistence_libcalls", test_sys_persistence_libcalls,
     "exclusive", 30000,
     "api.libcall.sys,baseline.legacy.unified.runtime.test_sys_persistence_libcalls,language.item-syntax.item-save,libcall.sys.backup,libcall.sys.save"},
    {"rewrite.runtime.test_sys_introspection_libcalls", test_sys_introspection_libcalls,
     "exclusive", 30000,
     "baseline.legacy.unified.runtime.test_sys_introspection_libcalls,libcall.sys.rootcount,libcall.sys.version"},
    {"rewrite.runtime.test_sys_wall_milliseconds_boundaries", test_sys_wall_milliseconds_boundaries,
     "exclusive", 30000,
     "baseline.legacy.unified.runtime.test_sys_wall_milliseconds_boundaries,libcall.sys.monotime,libcall.sys.now"},
    {"rewrite.runtime.test_sys_caller_paramcount_libcalls", test_sys_caller_paramcount_libcalls,
     "exclusive", 30000,
     "baseline.legacy.unified.runtime.test_sys_caller_paramcount_libcalls,libcall.sys.calleritem,libcall.sys.paramcount"},
    {"rewrite.runtime.test_sys_source_libcall", test_sys_source_libcall,
     "exclusive", 30000,
     "api.common.logging,baseline.legacy.unified.runtime.test_sys_source_libcall,libcall.sys.source"},
};

int main(int argc, char **argv) {
  return tf_main(argc, argv, tests, sizeof tests / sizeof tests[0]);
}
