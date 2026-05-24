#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "libcall.h"
#include "test_assert.h"

static uint64_t now_ns(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return ((uint64_t)ts.tv_sec * 1000000000ull) + (uint64_t)ts.tv_nsec;
}

static int strict_bench_enabled(void) {
  const char *flag = getenv("SIN_STRICT_BENCH");
  if (flag == NULL) {
    return 0;
  }
  return strcmp(flag, "1") == 0 || strcmp(flag, "true") == 0 || strcmp(flag, "TRUE") == 0;
}

void test_runtime_benchmark_optin(void) {
  uint8_t token = 0;
  uint8_t args = 0;
  ASSERT_TRUE(libcall_lookup_token("str", "upper", &token, &args));
  ASSERT_NOT_NULL(libcall_func_token(token));

  const int lookup_iters = 150000;
  const int dispatch_iters = 3000000;
  volatile uintptr_t sink = 0;

  uint64_t t0 = now_ns();
  for (int i = 0; i < lookup_iters; i++) {
    uint8_t tk = 0;
    uint8_t ar = 0;
    int ok = libcall_lookup_token("str", "upper", &tk, &ar);
    sink ^= (uintptr_t)tk;
    sink ^= (uintptr_t)ar;
    ASSERT_TRUE(ok);
  }
  uint64_t t1 = now_ns();

  uint64_t t2 = now_ns();
  for (int i = 0; i < dispatch_iters; i++) {
    OP_t fn = libcall_func_token(token);
    sink ^= (uintptr_t)fn;
    ASSERT_NOT_NULL(fn);
  }
  uint64_t t3 = now_ns();

  uint64_t lookup_total = t1 - t0;
  uint64_t dispatch_total = t3 - t2;
  uint64_t lookup_per_op = lookup_total / (uint64_t)lookup_iters;
  uint64_t dispatch_per_op = dispatch_total / (uint64_t)dispatch_iters;

  printf("[bench] lookup_token(str.upper): total=%llu ns iters=%d per_op=%llu ns\n",
         (unsigned long long)lookup_total, lookup_iters, (unsigned long long)lookup_per_op);
  printf("[bench] dispatch_token(str.upper): total=%llu ns iters=%d per_op=%llu ns\n",
         (unsigned long long)dispatch_total, dispatch_iters, (unsigned long long)dispatch_per_op);

  ASSERT_TRUE(sink == 0 || sink != 0);

  if (!strict_bench_enabled()) {
    printf("[bench] strict thresholds disabled (set SIN_STRICT_BENCH=1 to enforce budget checks)\n");
    return;
  }

  const uint64_t lookup_per_op_max_ns = 3000;
  const uint64_t dispatch_per_op_max_ns = 500;

  printf("[bench] strict thresholds enabled: lookup<=%llu ns/op dispatch<=%llu ns/op\n",
         (unsigned long long)lookup_per_op_max_ns, (unsigned long long)dispatch_per_op_max_ns);

  ASSERT_TRUE(lookup_per_op <= lookup_per_op_max_ns);
  ASSERT_TRUE(dispatch_per_op <= dispatch_per_op_max_ns);
}
