#include "libcall.h"
#include "test_assert.h"

void test_libcall_lookup_precomputed(void) {
  uint8_t lib = 0;
  uint8_t call = 0;
  uint8_t args = 0;

  ASSERT_TRUE(libcall_lookup("task", "newgametask", &lib, &call, &args));
  ASSERT_EQ_INT(2, lib);
  ASSERT_EQ_INT(0, call);
  ASSERT_EQ_INT(3, args);

  ASSERT_TRUE(!libcall_lookup("missing", "missing", &lib, &call, &args));
}
