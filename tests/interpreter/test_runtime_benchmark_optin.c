#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "libcall.h"
#include "item.h"
#include "stack.h"
#include "value.h"
#include "vm.h"
#include "interpret.h"
#include "runtime_context.h"
#include "list.h"
#include "itemref.h"
#include "item_persist_internal.h"
#include "test_assert.h"

extern uint8_t *op_build_list(RuntimeContext *ctx, uint8_t *nextop,
                              ITEM_t *item);

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

static int extended_bench_enabled(void) {
  const char *flag = getenv("SIN_EXTENDED_BENCH");
  return flag != NULL && (strcmp(flag, "1") == 0 ||
                          strcmp(flag, "true") == 0 ||
                          strcmp(flag, "TRUE") == 0);
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

static SIN_LIST_t *bench_make_list(size_t count) {
  VALUE_t *values = calloc(count, sizeof(*values));
  ASSERT_TRUE(count == 0 || values != NULL);
  for (size_t i = 0; i < count; i++) {
    values[i] = (VALUE_t){.type = VALUE_int, .i = (int64_t)i};
  }
  SIN_LIST_t *list = sin_list_build_owned(values, count);
  free(values);
  ASSERT_NOT_NULL(list);
  return list;
}

static uint64_t bench_runtime_build_list(size_t count, size_t iters,
                                         volatile uintptr_t *sink) {
  VM_t *vm = make_vm();
  ASSERT_NOT_NULL(vm);
  RuntimeContext ctx;
  runtime_context_init(&ctx, vm);
  uint8_t frame[4];
  frame[0] = (uint8_t)(count & 0xffu);
  frame[1] = (uint8_t)((count >> 8u) & 0xffu);
  frame[2] = (uint8_t)((count >> 16u) & 0xffu);
  frame[3] = (uint8_t)((count >> 24u) & 0xffu);
  runtime_decoder_init(&ctx.decoder, frame, frame + sizeof(frame));
  uint64_t start = now_ns();
  for (size_t n = 0; n < iters; n++) {
    for (size_t i = 0; i < count; i++) {
      push_stack(vm->stack, (VALUE_t){.type = VALUE_int, .i = (int64_t)i});
    }
    uint8_t *next = op_build_list(&ctx, frame, NULL);
    ASSERT_TRUE(next == frame + sizeof(frame));
    VALUE_t result = pop_stack(vm->stack);
    ASSERT_EQ_INT(VALUE_list, result.type);
    *sink ^= sin_list_count(result.list);
    value_free(&result);
  }
  uint64_t elapsed = now_ns() - start;
  destroy_vm(vm);
  return elapsed;
}

static uint64_t bench_construct(size_t count, size_t iters,
                                volatile uintptr_t *sink) {
  uint64_t start = now_ns();
  for (size_t n = 0; n < iters; n++) {
    SIN_LIST_t *list = bench_make_list(count);
    *sink ^= sin_list_count(list);
    sin_list_release(list);
  }
  return now_ns() - start;
}

static uint64_t bench_clone(SIN_LIST_t *list, size_t iters,
                            volatile uintptr_t *sink) {
  uint64_t start = now_ns();
  for (size_t n = 0; n < iters; n++) {
    SIN_LIST_t *clone = sin_list_retain(list);
    *sink ^= sin_list_count(clone);
    sin_list_release(clone);
  }
  return now_ns() - start;
}

static uint64_t bench_random_get(SIN_LIST_t *list, size_t iters,
                                 volatile uintptr_t *sink) {
  uint64_t start = now_ns();
  for (size_t n = 0; n < iters; n++) {
    for (size_t i = 0; i < 8u; i++) {
      size_t index = (i * 17u) % sin_list_count(list);
      const VALUE_t *element = sin_list_get(list, index);
      ASSERT_NOT_NULL(element);
      *sink ^= (uintptr_t)element->i;
    }
  }
  return now_ns() - start;
}

static uint64_t bench_sequential_get(SIN_LIST_t *list, size_t iters,
                                     volatile uintptr_t *sink) {
  size_t count = sin_list_count(list);
  uint64_t start = now_ns();
  for (size_t n = 0; n < iters; n++) {
    for (size_t i = 0; i < count; i++) {
      const VALUE_t *element = sin_list_get(list, i);
      ASSERT_NOT_NULL(element);
      *sink ^= (uintptr_t)element->i;
    }
  }
  return now_ns() - start;
}

static uint64_t bench_append(SIN_LIST_t *list, size_t iters,
                             volatile uintptr_t *sink) {
  VALUE_t value = {.type = VALUE_int, .i = 9};
  uint64_t start = now_ns();
  for (size_t n = 0; n < iters; n++) {
    SIN_LIST_t *next = sin_list_append(list, &value);
    ASSERT_NOT_NULL(next);
    *sink ^= sin_list_count(next);
    sin_list_release(next);
  }
  return now_ns() - start;
}

static uint64_t bench_set(SIN_LIST_t *list, size_t count, size_t iters,
                          volatile uintptr_t *sink) {
  VALUE_t value = {.type = VALUE_int, .i = 9};
  uint64_t start = now_ns();
  for (size_t n = 0; n < iters; n++) {
    SIN_LIST_t *next = sin_list_set(list, count / 2u, &value);
    ASSERT_NOT_NULL(next);
    *sink ^= sin_list_count(next);
    sin_list_release(next);
  }
  return now_ns() - start;
}

static uint64_t bench_concat(SIN_LIST_t *left, SIN_LIST_t *right,
                             size_t iters, volatile uintptr_t *sink) {
  uint64_t start = now_ns();
  for (size_t n = 0; n < iters; n++) {
    SIN_LIST_t *next = sin_list_concat(left, right);
    ASSERT_NOT_NULL(next);
    *sink ^= sin_list_count(next);
    sin_list_release(next);
  }
  return now_ns() - start;
}

static uint64_t bench_slice(SIN_LIST_t *list, size_t count, size_t iters,
                            volatile uintptr_t *sink) {
  uint64_t start = now_ns();
  for (size_t n = 0; n < iters; n++) {
    SIN_LIST_t *next = sin_list_slice(list, count / 4u, count / 2u);
    ASSERT_NOT_NULL(next);
    *sink ^= sin_list_count(next);
    sin_list_release(next);
  }
  return now_ns() - start;
}

static uint64_t bench_equal(SIN_LIST_t *left, SIN_LIST_t *right, size_t iters,
                            volatile uintptr_t *sink) {
  uint64_t start = now_ns();
  for (size_t n = 0; n < iters; n++) {
    *sink ^= sin_list_equal(left, right);
  }
  return now_ns() - start;
}

static void print_list_row(const char *operation, size_t size, uint64_t median,
                           size_t invocations, const char *unit) {
  printf("[bench][list] op=%s size=%zu median_ns=%llu ns/%s=%llu\n",
         operation, size, (unsigned long long)median,
         unit, (unsigned long long)(median / invocations));
}

static uint64_t sample_list(uint64_t (*operation)(size_t, size_t,
                                                   volatile uintptr_t *),
                            size_t size, size_t iters,
                            volatile uintptr_t *sink) {
  uint64_t samples[3];
  for (size_t i = 0; i < 3; i++) {
    samples[i] = operation(size, iters, sink);
  }
  return median_u64(samples, 3);
}

static void run_extended_list_benchmarks(void) {
  const size_t samples = 3;
  const size_t iters = 80;
  volatile uintptr_t sink = 0;
  const size_t construct_sizes[] = {0, 8, 1024};
  for (size_t i = 0; i < 3; i++) {
    uint64_t median = sample_list(bench_construct, construct_sizes[i], iters,
                                  &sink);
    print_list_row("construct", construct_sizes[i], median, iters,
                   "invocation");
    SIN_LIST_t *list = bench_make_list(construct_sizes[i]);
    uint64_t clone_samples[3];
    for (size_t n = 0; n < samples; n++) {
      clone_samples[n] = bench_clone(list, iters, &sink);
    }
    print_list_row("clone_release", construct_sizes[i],
                   median_u64(clone_samples, samples), iters, "invocation");
    sin_list_release(list);
  }
  const size_t access_sizes[] = {8, 1024};
  for (size_t i = 0; i < 2; i++) {
    SIN_LIST_t *list = bench_make_list(access_sizes[i]);
    uint64_t random_samples[3];
    uint64_t sequential_samples[3];
    for (size_t n = 0; n < samples; n++) {
      random_samples[n] = bench_random_get(list, iters, &sink);
      sequential_samples[n] = bench_sequential_get(list, iters, &sink);
    }
    print_list_row("random_get", access_sizes[i],
                   median_u64(random_samples, samples), iters * 8u, "access");
    print_list_row("sequential_get", access_sizes[i],
                   median_u64(sequential_samples, samples),
                   iters * access_sizes[i], "access");
    uint64_t set_samples[3];
    uint64_t concat_samples[3];
    uint64_t slice_samples[3];
    for (size_t n = 0; n < samples; n++) {
      set_samples[n] = bench_set(list, access_sizes[i], iters, &sink);
      concat_samples[n] = bench_concat(list, list, iters, &sink);
      slice_samples[n] = bench_slice(list, access_sizes[i], iters, &sink);
    }
    print_list_row("set", access_sizes[i], median_u64(set_samples, samples),
                   iters, "invocation");
    print_list_row("concat", access_sizes[i],
                   median_u64(concat_samples, samples), iters, "invocation");
    print_list_row("slice", access_sizes[i],
                   median_u64(slice_samples, samples), iters, "invocation");
    sin_list_release(list);
  }
  const size_t append_boundaries[] = {31, 32, 1055, 1056};
  for (size_t s = 0;
       s < sizeof(append_boundaries) / sizeof(append_boundaries[0]); s++) {
    uint64_t timings[samples];
    for (size_t n = 0; n < samples; n++) {
      SIN_LIST_t *list = bench_make_list(append_boundaries[s]);
      timings[n] = bench_append(list, iters, &sink);
      sin_list_release(list);
    }
    uint64_t median = median_u64(timings, samples);
    printf("[bench][list] op=append input=%zu output=%zu median_ns=%llu ns/invocation=%llu\n",
           append_boundaries[s], append_boundaries[s] + 1u,
           (unsigned long long)median, (unsigned long long)(median / iters));
  }
  SIN_LIST_t *equal_left = bench_make_list(1024);
  SIN_LIST_t *equal_right = bench_make_list(1024);
  VALUE_t unequal_value = {.type = VALUE_int, .i = -1};
  SIN_LIST_t *early = sin_list_set(equal_right, 0, &unequal_value);
  SIN_LIST_t *late = sin_list_set(equal_right, 1023, &unequal_value);
  ASSERT_NOT_NULL(early);
  ASSERT_NOT_NULL(late);
  uint64_t equal_samples[3];
  uint64_t early_samples[3];
  uint64_t late_samples[3];
  for (size_t n = 0; n < samples; n++) {
    equal_samples[n] = bench_equal(equal_left, equal_right, iters, &sink);
    early_samples[n] = bench_equal(equal_left, early, iters, &sink);
    late_samples[n] = bench_equal(equal_left, late, iters, &sink);
  }
  print_list_row("equal", 1024, median_u64(equal_samples, samples), iters,
                 "invocation");
  print_list_row("early_unequal", 1024, median_u64(early_samples, samples),
                 iters, "invocation");
  print_list_row("late_unequal", 1024, median_u64(late_samples, samples),
                 iters, "invocation");
  sin_list_release(early);
  sin_list_release(late);
  sin_list_release(equal_left);
  sin_list_release(equal_right);
  uint64_t literal_samples[samples];
  for (size_t n = 0; n < samples; n++) {
    literal_samples[n] = bench_runtime_build_list(33, iters, &sink);
  }
  uint64_t literal_median = median_u64(literal_samples, samples);
  printf("[bench][list] op=runtime_BUILD_LIST_literal size=33 median_ns=%llu ns/invocation=%llu\n",
         (unsigned long long)literal_median,
         (unsigned long long)(literal_median / iters));
  printf("[bench][list] sink=%llu\n", (unsigned long long)sink);
}

static void run_extended_itemstore_benchmarks(void) {
  char path[] = "/tmp/sin-list-bench-XXXXXX";
  int fd = mkstemp(path);
  ASSERT_TRUE(fd >= 0);
  int close_ok = close(fd) == 0;
  int initial_unlink_ok = unlink(path) == 0 || errno == ENOENT;
  ASSERT_TRUE(close_ok);
  ASSERT_TRUE(initial_unlink_ok);
  ITEMSTORE_t *store = itemstore_create("list-bench");
  ASSERT_NOT_NULL(store);
  SIN_LIST_t *list = bench_make_list(33);
  VALUE_t stored = {.type = VALUE_list, .list = list};
  ASSERT_NOT_NULL(item_set_value(itemstore_root(store), "payload", stored).item);
  uint64_t save_samples[3];
  uint64_t load_samples[3];
  int save_ok = itemstore_save(path, store);
  for (size_t i = 0; i < 3; i++) {
    uint64_t start = now_ns();
    int ok = itemstore_save(path, store);
    save_samples[i] = now_ns() - start;
    save_ok = save_ok && ok;
  }
  int load_ok = 1;
  for (size_t i = 0; i < 3; i++) {
    uint64_t start = now_ns();
    ITEMSTORE_t *loaded = itemstore_load(path);
    if (loaded == NULL) {
      load_ok = 0;
    } else {
      itemstore_destroy(loaded);
    }
    load_samples[i] = now_ns() - start;
  }
  printf("[bench][itemstore_v2] list33 save_median_ns=%llu load_median_ns=%llu\n",
         (unsigned long long)median_u64(save_samples, 3),
         (unsigned long long)median_u64(load_samples, 3));
  itemstore_destroy(store);
  int final_unlink_ok = unlink(path) == 0 || errno == ENOENT;
  ASSERT_TRUE(final_unlink_ok);
  ASSERT_TRUE(save_ok);
  ASSERT_TRUE(load_ok);
}

static void run_extended_itemref_and_syscall_benchmarks(void) {
  volatile uintptr_t sink = 0;
  uint64_t create_samples[3];
  uint64_t resolve_samples[3];
  ITEMSTORE_t *store = itemstore_create("itemref-bench");
  ASSERT_NOT_NULL(store);
  VALUE_t target_value = {.type = VALUE_int, .i = 3};
  ASSERT_NOT_NULL(item_set_value(itemstore_root(store), "players.3",
                                 target_value).item);
  SIN_ITEMREF_t *prepared = sin_itemref_create("players.3");
  ASSERT_NOT_NULL(prepared);
  for (size_t n = 0; n < 3; n++) {
    uint64_t start = now_ns();
    for (size_t i = 0; i < 1000; i++) {
      SIN_ITEMREF_t *ref = sin_itemref_create("players.3");
      ASSERT_NOT_NULL(ref);
      sink ^= (uintptr_t)sin_itemref_path_length(ref);
      sin_itemref_release(ref);
    }
    create_samples[n] = now_ns() - start;
    start = now_ns();
    for (size_t i = 0; i < 1000; i++) {
      ITEM_t *target = find_item(itemstore_root(store), sin_itemref_path(prepared));
      ASSERT_NOT_NULL(target);
      sink ^= (uintptr_t)item_value(target)->i;
    }
    resolve_samples[n] = now_ns() - start;
  }
  printf("[bench][itemref] create median_ns=%llu ns/op=%llu resolve_existing_path median_ns=%llu ns/op=%llu\n",
         (unsigned long long)median_u64(create_samples, 3),
         (unsigned long long)(median_u64(create_samples, 3) / 1000u),
         (unsigned long long)median_u64(resolve_samples, 3),
         (unsigned long long)(median_u64(resolve_samples, 3) / 1000u));
  sin_itemref_release(prepared);
  itemstore_destroy(store);
  SIN_LIST_t *list = bench_make_list(8);
  uint64_t transfer_samples[3];
  for (size_t n = 0; n < 3; n++) {
    uint64_t start = now_ns();
    for (size_t i = 0; i < 1000; i++) {
      for (size_t j = 0; j < sin_list_count(list); j++) {
        VALUE_t clone = VALUE_NIL;
        ASSERT_TRUE(value_clone_fallible(sin_list_get(list, j), &clone));
        sink ^= (uintptr_t)clone.i;
        value_free(&clone);
      }
    }
    transfer_samples[n] = now_ns() - start;
  }
  printf("[bench][sys.call_transfer_proxy] list_element_lookup_clone "
         "median_ns=%llu ns/list=%llu ns/element=%llu "
         "(per list, %zu elements; not full sys.call)\n",
         (unsigned long long)median_u64(transfer_samples, 3),
         (unsigned long long)(median_u64(transfer_samples, 3) / 1000u),
         (unsigned long long)(median_u64(transfer_samples, 3) /
                              (1000u * sin_list_count(list))),
         sin_list_count(list));
  sin_list_release(list);
  printf("[bench][itemref_syscall] sink=%llu\n", (unsigned long long)sink);
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

  if (extended_bench_enabled()) {
    run_extended_list_benchmarks();
    run_extended_itemstore_benchmarks();
    run_extended_itemref_and_syscall_benchmarks();
  } else {
    printf("[bench] extended matrix disabled (set SIN_EXTENDED_BENCH=1)\n");
  }

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
