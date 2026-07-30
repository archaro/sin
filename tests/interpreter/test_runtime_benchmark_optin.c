#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "libcall.h"
#include "item.h"
#include "stack.h"
#include "value.h"
#include "vm.h"
#include "interpret.h"
#include "runtime_context.h"
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

static uint64_t median_u64(uint64_t *samples, size_t count) {
  for (size_t i = 1; i < count; i++) {
    uint64_t value = samples[i];
    size_t j = i;
    while (j > 0 && samples[j - 1] > value) {
      samples[j] = samples[j - 1];
      j--;
    }
    samples[j] = value;
  }
  return samples[count / 2];
}

static double per_op_ratio(uint64_t numerator, size_t numerator_iters,
                           uint64_t denominator, size_t denominator_iters) {
  if (numerator_iters == 0 || denominator == 0) return 0.0;
  return ((double)numerator / (double)numerator_iters) /
         ((double)denominator / (double)denominator_iters);
}

static uint64_t bench_value_ops(size_t iters) {
  uint64_t start = now_ns();
  for (size_t i = 0; i < iters; i++) {
    VALUE_t src = {.type = VALUE_int, .i = (int64_t)i};
    VALUE_t clone = value_clone(&src);
    VALUE_t moved = VALUE_NIL;
    value_move(&moved, &clone);
    value_replace(&src, moved);
    value_free(&src);
  }
  return now_ns() - start;
}

static uint64_t bench_stack_ops(size_t iters, int strings) {
  STACK_t *stack = make_stack();
  ASSERT_NOT_NULL(stack);
  uint64_t start = now_ns();
  for (size_t i = 0; i < iters; i++) {
    VALUE_t value = strings
        ? (VALUE_t){.type = VALUE_str, .s = strdup("benchmark")}
        : (VALUE_t){.type = VALUE_int, .i = (int64_t)i};
    push_stack(stack, value);
    VALUE_t out = pop_stack(stack);
    value_free(&out);
  }
  uint64_t elapsed = now_ns() - start;
  destroy_stack(stack);
  return elapsed;
}

static uint64_t bench_item_ops(size_t iters) {
  ITEMSTORE_t *store = itemstore_create("bench");
  ASSERT_NOT_NULL(store);
  ITEM_t *root = itemstore_root(store);
  ASSERT_NOT_NULL(item_set_value(root, "bench.value",
                                 (VALUE_t){.type = VALUE_int, .i = 1}).item);
  ITEM_t *item = find_item(root, "bench.value");
  ASSERT_NOT_NULL(item);
  uint64_t start = now_ns();
  for (size_t i = 0; i < iters; i++) {
    ASSERT_NOT_NULL(item_value(item));
    ITEM_MUTATION_RESULT_t result = item_set_value(
        root, "bench.value", (VALUE_t){.type = VALUE_int, .i = (int64_t)i});
    ASSERT_NOT_NULL(result.item);
    item = result.item;
  }
  uint64_t elapsed = now_ns() - start;
  itemstore_destroy(store);
  return elapsed;
}

typedef struct {
  ITEMSTORE_t *store;
  VM_t *vm;
  RuntimeContext ctx;
  ITEM_t *caller;
} InterpreterBenchmark_t;

static ITEM_t *set_benchmark_code(ITEMSTORE_t *store, const char *name,
                                  const uint8_t *bytes, size_t length) {
  uint8_t *code = malloc(length);
  ASSERT_NOT_NULL(code);
  memcpy(code, bytes, length);
  ITEM_t *item = item_set_code(itemstore_root(store), name, (uint32_t)length,
                               code).item;
  ASSERT_NOT_NULL(item);
  return item;
}

static void prepare_interpreter_benchmark(InterpreterBenchmark_t *benchmark,
                                          int with_argument) {
  static const uint8_t argument_callee[] = {1, 1, 'e', 0, 'Q', 'h'};
  static const uint8_t argument_caller[] = {
      0, 0, 'p', 7, 0, 0, 0, 0, 0, 0, 0,
      'l', 3, 0, 'a', 'r', 'g', 'F', 1, 0, 'Q', 'h'};
  static const uint8_t control_callee[] = {0, 0, 'h'};
  static const uint8_t control_caller[] = {
      0, 0, 'l', 7, 0, 'c', 'o', 'n', 't', 'r', 'o', 'l', 'F', 0, 0, 'Q', 'h'};

  benchmark->store = itemstore_create("interp-bench");
  benchmark->vm = make_vm();
  ASSERT_NOT_NULL(benchmark->store);
  ASSERT_NOT_NULL(benchmark->vm);
  if (with_argument) {
    set_benchmark_code(benchmark->store, "arg", argument_callee,
                       sizeof(argument_callee));
    benchmark->caller = set_benchmark_code(benchmark->store, "arg.caller",
                                            argument_caller,
                                            sizeof(argument_caller));
  } else {
    set_benchmark_code(benchmark->store, "control", control_callee,
                       sizeof(control_callee));
    benchmark->caller = set_benchmark_code(benchmark->store, "control.caller",
                                            control_caller,
                                            sizeof(control_caller));
  }
  runtime_context_init(&benchmark->ctx, benchmark->vm);
  benchmark->ctx.itemstore = benchmark->store;
}

static void destroy_interpreter_benchmark(InterpreterBenchmark_t *benchmark) {
  runtime_destroy(&benchmark->ctx);
  destroy_vm(benchmark->vm);
  itemstore_destroy(benchmark->store);
}

static uint64_t bench_interpreter_ops(InterpreterBenchmark_t *benchmark,
                                      size_t iters, int with_argument) {
  uint64_t start = now_ns();
  for (size_t i = 0; i < iters; i++) {
    VALUE_t result = interpret(&benchmark->ctx, benchmark->caller);
    value_free(&result);
  }
  uint64_t elapsed = now_ns() - start;

  VALUE_t result = interpret(&benchmark->ctx, benchmark->caller);
  if (with_argument) {
    ASSERT_EQ_INT(VALUE_int, result.type);
    ASSERT_EQ_INT(7, result.i);
  } else {
    ASSERT_EQ_INT(VALUE_nil, result.type);
  }
  value_free(&result);
  return elapsed;
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

  const size_t sample_count = 5;
  const size_t value_iters = 100000;
  uint64_t value_samples[5], scalar_samples[5], string_samples[5], item_samples[5];
  for (size_t i = 0; i < sample_count; i++) {
    value_samples[i] = bench_value_ops(value_iters);
    scalar_samples[i] = bench_stack_ops(value_iters, 0);
    string_samples[i] = bench_stack_ops(value_iters, 1);
    item_samples[i] = bench_item_ops(value_iters / 10);
  }
  uint64_t value_median = median_u64(value_samples, sample_count);
  uint64_t scalar_median = median_u64(scalar_samples, sample_count);
  uint64_t string_median = median_u64(string_samples, sample_count);
  uint64_t item_median = median_u64(item_samples, sample_count);
  uint64_t interp_control[sample_count], interp_args[sample_count];
  InterpreterBenchmark_t control_benchmark = {0};
  InterpreterBenchmark_t argument_benchmark = {0};
  prepare_interpreter_benchmark(&control_benchmark, 0);
  prepare_interpreter_benchmark(&argument_benchmark, 1);
  for (size_t i = 0; i < sample_count; i++) {
    interp_control[i] = bench_interpreter_ops(&control_benchmark,
                                              value_iters / 10, 0);
    interp_args[i] = bench_interpreter_ops(&argument_benchmark,
                                           value_iters / 10, 1);
  }
  destroy_interpreter_benchmark(&argument_benchmark);
  destroy_interpreter_benchmark(&control_benchmark);
  uint64_t control_median = median_u64(interp_control, sample_count);
  uint64_t args_median = median_u64(interp_args, sample_count);
  printf("[bench] value_clone_move_replace_free median=%llu ns (%llu ns/op)\n",
         (unsigned long long)value_median,
         (unsigned long long)(value_median / value_iters));
  printf("[bench] stack_push_pop_scalar median=%llu ns (%llu ns/op)\n",
         (unsigned long long)scalar_median,
         (unsigned long long)(scalar_median / value_iters));
  printf("[bench] stack_push_pop_string median=%llu ns (%llu ns/op)\n",
         (unsigned long long)string_median,
         (unsigned long long)(string_median / value_iters));
  printf("[bench] item_value_fetch_assignment median=%llu ns (%llu ns/op)\n",
         (unsigned long long)item_median,
         (unsigned long long)(item_median / (value_iters / 10)));
  printf("[bench] scalar ratios: stack/value=%.3f string/scalar=%.3f "
         "item/value=%.3f\n",
         per_op_ratio(scalar_median, value_iters, value_median, value_iters),
         per_op_ratio(string_median, value_iters, scalar_median, value_iters),
         per_op_ratio(item_median, value_iters / 10, value_median, value_iters));
  printf("[bench] interpreter_control median=%llu ns\n", (unsigned long long)control_median);
  printf("[bench] interpreter_argument_return median=%llu ns ratio=%.3f\n",
         (unsigned long long)args_median,
         control_median == 0 ? 0.0 : (double)args_median / (double)control_median);

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
