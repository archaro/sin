#include "libcall.h"
#include "test_assert.h"

void test_libcall_lookup_precomputed(void) {
  uint8_t token = 0;
  uint8_t args = 0;

  ASSERT_TRUE(libcall_lookup_token("task", "newgametask", &token, &args));
  ASSERT_TRUE(token >= 0);
  ASSERT_EQ_INT(3, args);

  ASSERT_TRUE(!libcall_lookup_token("missing", "missing", &token, &args));
}
