#pragma once

#include "test_framework.h"

#define TEST_FAILF(...) \
  do { \
    tf_assertf(__FILE__, __LINE__, __VA_ARGS__); \
  } while (0)

#define ASSERT_TRUE(value) TF_ASSERT_TRUE(value)
#define ASSERT_EQ_INT(expected, actual) \
  TF_ASSERT_I64((int64_t)(expected), (int64_t)(actual))
#define ASSERT_NOT_NULL(pointer) TF_ASSERT_TRUE((pointer) != NULL)
