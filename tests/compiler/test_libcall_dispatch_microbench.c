#include <stdint.h>
#include <stdio.h>
#include <time.h>

#include "libcall.h"
#include "test_assert.h"

static uint64_t now_ns(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return ((uint64_t)ts.tv_sec * 1000000000ull) + (uint64_t)ts.tv_nsec;
}

void test_libcall_dispatch_microbench(void) {
  uint8_t token = 0, args = 0;
  ASSERT_TRUE(libcall_lookup_token("str", "upper", &token, &args));
  ASSERT_NOT_NULL(libcall_func_token(token));

  const int iters = 5000000;
  volatile uintptr_t sink = 0;

  uint64_t t0 = now_ns();
  for (int i = 0; i < iters; i++) {
    OP_t fn = libcall_func_token(token);
    sink ^= (uintptr_t)fn;
  }
  uint64_t t1 = now_ns();

  uint64_t t2 = now_ns();
  for (int i = 0; i < iters; i++) {
    OP_t fn = libcall_func_token(token);
    sink ^= (uintptr_t)fn;
  }
  uint64_t t3 = now_ns();

  ASSERT_TRUE(sink == 0 || sink != 0);
  printf("[microbench] libcall token dispatch run1: %llu ns total (%d iters)\n", (unsigned long long)(t1 - t0), iters);
  printf("[microbench] libcall token dispatch run2: %llu ns total (%d iters)\n", (unsigned long long)(t3 - t2), iters);
}
