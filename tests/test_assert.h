#pragma once

#ifdef SIN_TEST_FRAMEWORK_COMPAT

#include "test_framework.h"

void tf_legacy_failf(const char *file, int line, const char *format, ...);

#define TEST_FAILF(...) \
  do { \
    tf_legacy_failf(__FILE__, __LINE__, __VA_ARGS__); \
  } while (0)

#define ASSERT_TRUE(value) TF_ASSERT_TRUE(value)
#define ASSERT_EQ_INT(expected, actual) \
  TF_ASSERT_I64((int64_t)(expected), (int64_t)(actual))
#define ASSERT_NOT_NULL(pointer) TF_ASSERT_TRUE((pointer) != NULL)

#else

const char *test_harness_current_suite(void);
const char *test_harness_current_test(void);
void test_harness_failf(const char *file, int line, const char *fmt, ...)
    __attribute__((format(printf, 3, 4)));

#define TEST_FAILF(...) \
  do { \
    test_harness_failf(__FILE__, __LINE__, __VA_ARGS__); \
  } while (0)

#define ASSERT_TRUE(cond) \
  do { \
    if (!(cond)) { \
      TEST_FAILF("%s", #cond); \
    } \
  } while (0)

#define ASSERT_EQ_INT(expected, actual) \
  do { \
    long long _exp = (long long)(expected); \
    long long _act = (long long)(actual); \
    if (_exp != _act) { \
      TEST_FAILF("expected %lld, got %lld", _exp, _act); \
    } \
  } while (0)

#define ASSERT_NOT_NULL(ptr) \
  do { \
    if ((ptr) == NULL) { \
      TEST_FAILF("%s was NULL", #ptr); \
    } \
  } while (0)

#endif
