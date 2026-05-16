#pragma once

#include <stdio.h>
#include <stdlib.h>

#define TEST_FAILF(fmt, ...) \
  do { \
    fprintf(stderr, "ASSERTION FAILED at %s:%d: " fmt "\n", __FILE__, __LINE__, ##__VA_ARGS__); \
    exit(1); \
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
