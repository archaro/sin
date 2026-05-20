#include "libcall.h"
#include "test_assert.h"

void test_libcall_registry_roundtrip(void) {
  ASSERT_TRUE(libcall_init_registry());
  ASSERT_TRUE(libcall_validate_registry());

  uint8_t lib = 0;
  uint8_t call = 0;
  uint8_t args = 0;
  ASSERT_TRUE(libcall_lookup("sys", "log", &lib, &call, &args));
  ASSERT_EQ_INT(1, lib);
  ASSERT_EQ_INT(1, call);
  ASSERT_EQ_INT(1, args);

  ASSERT_NOT_NULL(libcall_func(lib, call));
  ASSERT_TRUE(libcall_func(99, 99) == NULL);
}
