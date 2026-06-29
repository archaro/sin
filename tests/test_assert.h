#pragma once

const char *test_harness_current_suite(void);
const char *test_harness_current_test(void);
void test_harness_failf(const char *file, int line, const char *fmt, ...)
    __attribute__((format(printf, 3, 4)));

#define TEST_FAILF(fmt, ...) \
  do { \
    test_harness_failf(__FILE__, __LINE__, fmt, ##__VA_ARGS__); \
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
