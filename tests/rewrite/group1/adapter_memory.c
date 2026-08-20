#include "test_framework.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include "memory.h"

static void memory_allocation_boundaries(void) {
  size_t result = 17;
  void *buffer = malloc(8);
  size_t capacity = 1;
  const unsigned char original[] = {1, 2, 3, 4};

  TF_ASSERT_TRUE(buffer != NULL);
  memcpy(buffer, original, sizeof original);
  TF_ASSERT_TRUE(alloc_add_overflow(SIZE_MAX, 1, &result));
  TF_ASSERT_U64(17, result);
  TF_ASSERT_TRUE(alloc_mul_overflow(SIZE_MAX, 2, &result));
  TF_ASSERT_U64(17, result);
  TF_ASSERT_TRUE(alloc_grow_capacity(SIZE_MAX / 2 + 1, SIZE_MAX, &result) == false);

  alloc_test_fail_after(0);
  TF_ASSERT_FALSE(alloc_grow_array_capacity(&buffer, &capacity, 2, sizeof(unsigned char)));
  alloc_test_fail_after(-1);
  TF_ASSERT_U64(1, capacity);
  TF_ASSERT_TRUE(buffer != NULL);
  TF_ASSERT_BYTES(original, sizeof original, buffer, sizeof original);

  TF_ASSERT_TRUE(alloc_grow_array(&buffer, 0, sizeof(unsigned char)));
  TF_ASSERT_TRUE(buffer == NULL);
  TF_ASSERT_TRUE(alloc_grow_capacity(0, 0, &result));
  TF_ASSERT_U64(8, result);
}

static const TF_TestDescriptor tests[] = {
    {"rewrite.common.memory_allocation_boundaries",
     memory_allocation_boundaries, "exclusive", 30000,
     "api.common.memory"},
};

int main(int argc, char **argv) {
  return tf_main(argc, argv, tests, sizeof tests / sizeof tests[0]);
}
