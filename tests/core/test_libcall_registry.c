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

void test_libcall_name_duplicate_detection(void) {
  const LIBCALL_t dup_calls[] = {
    {"sys", "log", 1, 1, 1, NULL},
    {"sys", "log", 1, 9, 1, NULL},
    {NULL, NULL, -1, -1, 0, NULL}
  };

  ASSERT_TRUE(!libcall_names_unique(dup_calls));
}
