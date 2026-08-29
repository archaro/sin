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
#include "runtime_value.h"
#include "compiler/compiler_pipeline.h"
#include "error.h"
#include "list.h"
#include "itemref.h"
#include "item_persist_internal.h"
#include "test_assert.h"
#include "test_helpers.h"
#include "shared/test_libcall_support.h"

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

static ITEM_t *bench_compile_item(ITEMSTORE_t *store, const char *name,
                                  const char *source, const char **params,
                                  size_t param_count) {
  OUTPUT_t *output = NULL;
  CompilerDiagnostic diag;
  compiler_diag_init(&diag);
  int8_t rc = compile_source_to_bytecode_diag_with_params(
      source, strlen(source), params, param_count, &output, &diag);
  if (rc != ERR_NOERROR) {
    fprintf(stderr, "[benchmark compile] %s: %s\n", name,
            diag.message == NULL ? "<no detail>" : diag.message);
  }
  ASSERT_EQ_INT(ERR_NOERROR, rc);
  ASSERT_NOT_NULL(output);
  ASSERT_NOT_NULL(output->bytecode);
  size_t length = (size_t)(output->nextbyte - output->bytecode);
  ASSERT_TRUE(length <= UINT32_MAX);
  uint8_t *bytecode = output->bytecode;
  output->bytecode = NULL;
  ITEM_t *item = item_set_code(itemstore_root(store), name,
                               (uint32_t)length, bytecode).item;
  free(output);
  compiler_diag_reset(&diag);
  ASSERT_NOT_NULL(item);
  return item;
}

static void bench_list_literal_source(char *source, size_t capacity,
                                      size_t count) {
  int written = snprintf(source, capacity, "return #[");
  ASSERT_TRUE(written > 0 && (size_t)written < capacity);
  size_t used = (size_t)written;
  for (size_t i = 0; i < count; i++) {
    written = snprintf(source + used, capacity - used, "%s%zu",
                       i == 0 ? "" : ", ", i);
    ASSERT_TRUE(written > 0 && (size_t)written < capacity - used);
    used += (size_t)written;
  }
  written = snprintf(source + used, capacity - used, "];");
  ASSERT_TRUE(written > 0 && (size_t)written < capacity - used);
}

static uint64_t bench_compiled_literal(ITEMSTORE_t *store, ITEM_t *item,
                                       size_t iters,
                                       volatile uintptr_t *sink) {
  VM_t *vm = make_vm();
  ASSERT_NOT_NULL(vm);
  RuntimeContext ctx;
  runtime_context_init(&ctx, vm);
  ctx.itemstore = store;
  uint64_t start = now_ns();
  for (size_t i = 0; i < iters; i++) {
    VALUE_t result = interpret(&ctx, item);
    ASSERT_EQ_INT(VALUE_list, result.type);
    *sink ^= sin_list_count(result.list);
    value_free(&result);
  }
  uint64_t elapsed = now_ns() - start;
  runtime_destroy(&ctx);
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

static uint64_t bench_slice_range(SIN_LIST_t *list, size_t offset, size_t length,
                                  size_t iters, volatile uintptr_t *sink) {
  uint64_t begin = now_ns();
  for (size_t n = 0; n < iters; n++) {
    SIN_LIST_t *next = sin_list_slice(list, offset, length);
    ASSERT_NOT_NULL(next);
    *sink ^= sin_list_count(next);
    sin_list_release(next);
  }
  return now_ns() - begin;
}

static uint64_t bench_slice(SIN_LIST_t *list, size_t count, size_t iters,
                            volatile uintptr_t *sink) {
  return bench_slice_range(list, count / 4u, count / 2u, iters, sink);
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
  const struct {
    size_t left;
    size_t right;
    const char *label;
  } concat_shapes[] = {
    {31u, 1025u, "unaligned"},
    {32u, 1024u, "aligned"},
    {1023u, 1025u, "unaligned"},
    {1024u, 1024u, "aligned"},
  };
  for (size_t i = 0; i < sizeof(concat_shapes) / sizeof(concat_shapes[0]);
       ++i) {
    SIN_LIST_t *left = bench_make_list(concat_shapes[i].left);
    SIN_LIST_t *right = bench_make_list(concat_shapes[i].right);
    uint64_t concat_samples[samples];
    for (size_t n = 0; n < samples; n++) {
      concat_samples[n] = bench_concat(left, right, iters, &sink);
    }
    print_list_row(concat_shapes[i].label,
                   concat_shapes[i].left + concat_shapes[i].right,
                   median_u64(concat_samples, samples), iters,
                   "concat_invocation");
    sin_list_release(left);
    sin_list_release(right);
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
  ITEMSTORE_t *literal_store = itemstore_create("compiled_list_bench");
  ASSERT_NOT_NULL(literal_store);
  char literal_source[256];
  bench_list_literal_source(literal_source, sizeof(literal_source), 33);
  ITEM_t *literal_item = bench_compile_item(literal_store, "bench.literal",
                                            literal_source, NULL, 0);
  uint64_t literal_samples[samples];
  for (size_t n = 0; n < samples; n++) {
    literal_samples[n] = bench_compiled_literal(literal_store, literal_item,
                                                 iters, &sink);
  }
  uint64_t literal_median = median_u64(literal_samples, samples);
  printf("[bench][list] op=compiled_source_list_literal size=33 "
         "median_ns=%llu ns/invocation=%llu\n",
         (unsigned long long)literal_median,
         (unsigned long long)(literal_median / iters));
  itemstore_destroy(literal_store);
  const struct {
    size_t size;
    size_t start;
    size_t length;
    const char *label;
  } slice_shapes[] = {
    {1056u, 32u, 992u, "slice_aligned_shared"},
    {2080u, 1024u, 1056u, "slice_aligned_subtree"},
    {65u, 64u, 1u, "slice_aligned_short_tail"},
    {1056u, 31u, 33u, "slice_unaligned_boundary"},
  };
  for (size_t i = 0; i < sizeof(slice_shapes) / sizeof(slice_shapes[0]); ++i) {
    SIN_LIST_t *list = bench_make_list(slice_shapes[i].size);
    uint64_t slice_samples[samples];
    for (size_t n = 0; n < samples; n++) {
      slice_samples[n] = bench_slice_range(list, slice_shapes[i].start,
                                           slice_shapes[i].length, iters,
                                           &sink);
    }
    print_list_row(slice_shapes[i].label, slice_shapes[i].size,
                   median_u64(slice_samples, samples), iters,
                   "slice_invocation");
    sin_list_release(list);
  }
  printf("[bench][list] sink=%llu\n", (unsigned long long)sink);
}

static void run_extended_itemstore_benchmarks(void) {
  char path[4096];
  ASSERT_EQ_INT(0, test_temp_template(path, sizeof path, "sin-list-bench"));
  ITEMSTORE_t *store = itemstore_create("list_bench");
  ASSERT_NOT_NULL(store);
  SIN_LIST_t *list = bench_make_list(33);
  VALUE_t stored = {.type = VALUE_list, .list = list};
  ASSERT_NOT_NULL(item_set_value(itemstore_root(store), "payload", stored).item);
  uint64_t save_samples[3];
  uint64_t load_samples[3];
  int fd = mkstemp(path);
  int setup_ok = fd >= 0;
  int close_ok = 1;
  int initial_unlink_ok = 1;
  int save_ok = 0;
  int load_ok = 0;
  if (setup_ok) {
    close_ok = close(fd) == 0;
    initial_unlink_ok = unlink(path) == 0 || errno == ENOENT;
    save_ok = itemstore_save(path, store);
    for (size_t i = 0; i < 3; i++) {
      uint64_t start = now_ns();
      int ok = itemstore_save(path, store);
      save_samples[i] = now_ns() - start;
      save_ok = save_ok && ok;
    }
    load_ok = 1;
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
  }
  itemstore_destroy(store);
  int final_unlink_ok = !setup_ok || unlink(path) == 0 || errno == ENOENT;
  if (setup_ok) {
    printf("[bench][itemstore_v2] list33 save_median_ns=%llu "
           "load_median_ns=%llu\n",
           (unsigned long long)median_u64(save_samples, 3),
           (unsigned long long)median_u64(load_samples, 3));
  }
  ASSERT_TRUE(setup_ok);
  ASSERT_TRUE(close_ok);
  ASSERT_TRUE(initial_unlink_ok);
  ASSERT_TRUE(final_unlink_ok);
  ASSERT_TRUE(save_ok);
  ASSERT_TRUE(load_ok);
}

static uint64_t bench_syscall(ITEMSTORE_t *store, ITEM_t *caller,
                              int64_t expected, size_t iters) {
  VM_t *vm = make_vm();
  ASSERT_NOT_NULL(vm);
  RuntimeContext ctx;
  runtime_context_init(&ctx, vm);
  ctx.itemstore = store;
  uint64_t start = now_ns();
  for (size_t i = 0; i < iters; i++) {
    VALUE_t result = interpret(&ctx, caller);
    ASSERT_EQ_INT(VALUE_int, result.type);
    ASSERT_EQ_INT((int)expected, (int)result.i);
    value_free(&result);
  }
  uint64_t elapsed = now_ns() - start;
  runtime_destroy(&ctx);
  destroy_vm(vm);
  return elapsed;
}

typedef enum {
  RUNTIME_VERIFY_BENCH_REPEATED,
  RUNTIME_VERIFY_BENCH_ALTERNATING,
  RUNTIME_VERIFY_BENCH_REPLACEMENT,
  RUNTIME_VERIFY_BENCH_COLD
} RuntimeVerifyBenchMode;

typedef struct {
  ITEMSTORE_t *store;
  VM_t *vm;
  RuntimeContext ctx;
  ITEM_t *callee_a;
  ITEM_t *callee_b;
  ITEM_t *caller_a;
  ITEM_t *caller_b;
} RuntimeVerifyBenchFixture;

static ITEM_t *runtime_verify_bench_set_code(ITEMSTORE_t *store,
                                             const char *name,
                                             const uint8_t *bytes,
                                             size_t length) {
  uint8_t *owned = malloc(length);
  ASSERT_NOT_NULL(owned);
  memcpy(owned, bytes, length);
  ASSERT_TRUE(length <= UINT32_MAX);
  ITEM_t *item = item_set_code(itemstore_root(store), name,
                               (uint32_t)length, owned).item;
  ASSERT_NOT_NULL(item);
  return item;
}

static void runtime_verify_bench_fixture_init(
    RuntimeVerifyBenchFixture *fixture) {
  static const uint8_t callee_bytes[] = {0, 0, 'h'};
  static const uint8_t caller_a_bytes[] = {
      0, 0, 'l', 1, 0, 'a', 'F', 0, 0, 'h'
  };
  static const uint8_t caller_b_bytes[] = {
      0, 0, 'l', 1, 0, 'b', 'F', 0, 0, 'h'
  };
  fixture->store = itemstore_create("runtime_verify_bench");
  ASSERT_NOT_NULL(fixture->store);
  fixture->callee_a = runtime_verify_bench_set_code(
      fixture->store, "a", callee_bytes, sizeof(callee_bytes));
  fixture->callee_b = runtime_verify_bench_set_code(
      fixture->store, "b", callee_bytes, sizeof(callee_bytes));
  fixture->caller_a = runtime_verify_bench_set_code(
      fixture->store, "caller.a", caller_a_bytes, sizeof(caller_a_bytes));
  fixture->caller_b = runtime_verify_bench_set_code(
      fixture->store, "caller.b", caller_b_bytes, sizeof(caller_b_bytes));
  fixture->vm = make_vm();
  ASSERT_NOT_NULL(fixture->vm);
  runtime_context_init(&fixture->ctx, fixture->vm);
  fixture->ctx.itemstore = fixture->store;
}

static void runtime_verify_bench_fixture_destroy(
    RuntimeVerifyBenchFixture *fixture) {
  runtime_destroy(&fixture->ctx);
  destroy_vm(fixture->vm);
  itemstore_destroy(fixture->store);
}

static uint64_t runtime_verify_bench_sample(RuntimeVerifyBenchMode mode,
                                            size_t iters,
                                            uint64_t *verifications) {
  static const uint8_t replacement_bytes[] = {0, 0, 'h'};
  RuntimeVerifyBenchFixture fixture = {0};
  runtime_verify_bench_fixture_init(&fixture);
  uint64_t start = now_ns();
  for (size_t i = 0u; i < iters; i++) {
    if (mode == RUNTIME_VERIFY_BENCH_REPLACEMENT) {
      ITEM_t *replacement = runtime_verify_bench_set_code(
          fixture.store, "a", replacement_bytes, sizeof(replacement_bytes));
      ASSERT_TRUE(replacement == fixture.callee_a);
    }
    if (mode == RUNTIME_VERIFY_BENCH_COLD) {
      runtime_verify_cache_clear_for_tests(&fixture.ctx);
    }
    ITEM_t *caller = fixture.caller_a;
    if (mode == RUNTIME_VERIFY_BENCH_ALTERNATING && (i & 1u) != 0u) {
      caller = fixture.caller_b;
    }
    VALUE_t result = interpret(&fixture.ctx, caller);
    ASSERT_EQ_INT(VALUE_nil, result.type);
    value_free(&result);
  }
  uint64_t elapsed = now_ns() - start;
  *verifications = runtime_verify_invocations_for_tests(&fixture.ctx);
  runtime_verify_bench_fixture_destroy(&fixture);
  return elapsed;
}

static void run_runtime_verification_cache_benchmarks(void) {
  const size_t sample_count = 5u;
  const size_t iters = 400u;
  uint64_t repeated_times[5];
  uint64_t alternating_times[5];
  uint64_t replacement_times[5];
  uint64_t cold_times[5];
  for (size_t i = 0u; i < sample_count; i++) {
    uint64_t repeated_verifications = 0u;
    uint64_t alternating_verifications = 0u;
    uint64_t replacement_verifications = 0u;
    uint64_t cold_verifications = 0u;
    repeated_times[i] = runtime_verify_bench_sample(
        RUNTIME_VERIFY_BENCH_REPEATED, iters, &repeated_verifications);
    alternating_times[i] = runtime_verify_bench_sample(
        RUNTIME_VERIFY_BENCH_ALTERNATING, iters, &alternating_verifications);
    replacement_times[i] = runtime_verify_bench_sample(
        RUNTIME_VERIFY_BENCH_REPLACEMENT, iters, &replacement_verifications);
    cold_times[i] = runtime_verify_bench_sample(
        RUNTIME_VERIFY_BENCH_COLD, iters, &cold_verifications);
    ASSERT_EQ_INT(2, repeated_verifications);
    ASSERT_EQ_INT(4, alternating_verifications);
    ASSERT_EQ_INT((int)(iters * 2u), replacement_verifications);
    ASSERT_EQ_INT((int)(iters * 2u), cold_verifications);
  }
  uint64_t repeated_median = median_u64(repeated_times, sample_count);
  uint64_t alternating_median = median_u64(alternating_times, sample_count);
  uint64_t replacement_median = median_u64(replacement_times, sample_count);
  uint64_t cold_median = median_u64(cold_times, sample_count);
  printf("[bench][runtime_verify] five-sample median ns/op repeated=%llu "
         "alternating=%llu replacement=%llu cold_same_path=%llu "
         "cold/repeated_ratio=%.3f\n",
         (unsigned long long)(repeated_median / iters),
         (unsigned long long)(alternating_median / iters),
         (unsigned long long)(replacement_median / iters),
         (unsigned long long)(cold_median / iters),
         per_op_ratio(cold_median, iters, repeated_median, iters));
}

static void run_extended_itemref_and_syscall_benchmarks(void) {
  volatile uintptr_t sink = 0;
  uint64_t create_samples[3];
  uint64_t resolve_samples[3];
  ITEMSTORE_t *store = itemstore_create("itemref_bench");
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
  ITEMSTORE_t *call_store = itemstore_create("sys_call_bench");
  ASSERT_NOT_NULL(call_store);
  const char *target_params[] = {
      "@arg0", "@arg1", "@arg2", "@arg3",
      "@arg4", "@arg5", "@arg6", "@arg7"
  };
  ITEM_t *target = bench_compile_item(call_store, "bench.target",
                                       "return @arg7;", target_params, 8);
  ITEM_t *zero = bench_compile_item(call_store, "bench.zero", "return 17;",
                                    NULL, 0);
  (void)target;
  (void)zero;
  char call_source[256];
  bench_list_literal_source(call_source, sizeof(call_source), 8);
  size_t call_source_length = strlen(call_source);
  ASSERT_TRUE(call_source_length > 0);
  call_source[call_source_length - 1] = '\0';
  char caller_source[384];
  int written = snprintf(caller_source, sizeof(caller_source),
                         "return sys.call{&bench.target, %s};", call_source + 7);
  ASSERT_TRUE(written > 0 && (size_t)written < sizeof(caller_source));
  ITEM_t *caller = bench_compile_item(call_store, "bench.caller",
                                      caller_source, NULL, 0);
  ITEM_t *zero_caller = bench_compile_item(
      call_store, "bench.zero_caller", "return sys.call{&bench.zero, #[]};",
      NULL, 0);
  uint64_t call_samples[3];
  uint64_t zero_samples[3];
  for (size_t i = 0; i < 3; i++) {
    call_samples[i] = bench_syscall(call_store, caller, 7, 80);
    zero_samples[i] = bench_syscall(call_store, zero_caller, 17, 80);
  }
  uint64_t call_median = median_u64(call_samples, 3);
  uint64_t zero_median = median_u64(zero_samples, 3);
  printf("[bench][sys.call] list8 median_ns=%llu ns/invocation=%llu "
         "zero_arg median_ns=%llu ns/invocation=%llu ratio=%.3f\n",
         (unsigned long long)call_median,
         (unsigned long long)(call_median / 80u),
         (unsigned long long)zero_median,
         (unsigned long long)(zero_median / 80u),
         zero_median == 0 ? 0.0 : (double)call_median / (double)zero_median);
  itemstore_destroy(call_store);
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

static VALUE_t bench_make_tracked_string(size_t length, size_t capacity) {
  VALUE_t value = concat_two_strings(
      (VALUE_t){.type = VALUE_str, .s = strdup("0123456789abcdef")},
      (VALUE_t){.type = VALUE_str, .s = strdup("")});
  ASSERT_EQ_INT(VALUE_str, value.type);
  if (capacity == 32u) {
    value = concat_two_strings(value,
                               (VALUE_t){.type = VALUE_str, .s = strdup("x")});
  } else {
    ASSERT_EQ_INT(64, (long long)capacity);
    char suffix[15];
    memset(suffix, 'x', sizeof(suffix) - 1u);
    suffix[sizeof(suffix) - 1u] = '\0';
    value = concat_two_strings(value,
                               (VALUE_t){.type = VALUE_str, .s = strdup(suffix)});
    value = concat_two_strings(value,
                               (VALUE_t){.type = VALUE_str, .s = strdup("x")});
  }
  ASSERT_EQ_INT(VALUE_str, value.type);
  size_t have = strlen(value.s);
  ASSERT_TRUE(have <= length);
  if (have < length) {
    size_t extra = length - have;
    char *suffix = malloc(extra + 1u);
    ASSERT_NOT_NULL(suffix);
    memset(suffix, 'x', extra);
    suffix[extra] = '\0';
    value = concat_two_strings(value, (VALUE_t){.type = VALUE_str, .s = suffix});
  }
  ASSERT_EQ_INT((long long)length, (long long)strlen(value.s));
  ASSERT_EQ_INT((long long)capacity,
                (long long)strbuf_capacity_for_tests(value.s));
  return value;
}

static VALUE_t *bench_make_tracked_population_shape(size_t count, size_t length) {
  VALUE_t *values = calloc(count, sizeof(*values));
  ASSERT_NOT_NULL(values);
  for (size_t i = 0; i < count; i++) {
    values[i] = bench_make_tracked_string(length, 32u);
  }
  return values;
}

static VALUE_t *bench_make_tracked_population(size_t count) {
  return bench_make_tracked_population_shape(count, 17u);
}

static void bench_free_tracked_population(VALUE_t *values, size_t count) {
  for (size_t i = 0; i < count; i++) value_free(&values[i]);
  free(values);
}

static uint64_t bench_string_capacity_lookup(VALUE_t *values, size_t iters,
                                             volatile uintptr_t *sink) {
  uint64_t start = now_ns();
  for (size_t i = 0; i < iters; i++) {
    *sink ^= strbuf_capacity_for_tests(values[0].s);
  }
  return now_ns() - start;
}

static uint64_t bench_string_removal(VALUE_t *values, size_t count,
                                     size_t iters, volatile uintptr_t *sink) {
  ASSERT_TRUE(iters <= count);
  uint64_t start = now_ns();
  for (size_t i = 0; i < iters; i++) {
    strbuf_forget_for_tests(values[i].s);
    *sink ^= (uintptr_t)values[i].s[0];
  }
  uint64_t elapsed = now_ns() - start;
  for (size_t i = 0; i < iters; i++) {
    free(values[i].s);
    values[i] = VALUE_NIL;
  }
  return elapsed;
}

static uint64_t bench_string_concat(VALUE_t *background, size_t population,
                                    size_t iters, int growth,
                                    volatile uintptr_t *sink,
                                    strbuf_probe_t *probe) {
  VALUE_t *right = calloc(iters, sizeof(*right));
  ASSERT_NOT_NULL(right);
  for (size_t i = 0; i < iters; i++) {
    right[i] = (VALUE_t){.type = VALUE_str,
                         .s = strdup(growth ? "0123456789abcdef" : "x")};
    right[i] = concat_two_strings(right[i],
                                  (VALUE_t){.type = VALUE_str, .s = strdup("")});
  }
  ASSERT_EQ_INT((long long)population + (long long)iters,
                (long long)strbuf_tracked_count_for_tests());
  strbuf_probe_reset_for_tests();
  uint64_t start = now_ns();
  for (size_t i = iters; i > 0; i--) {
    size_t index = i - 1u;
    background[index] = concat_two_strings(background[index], right[index]);
    ASSERT_EQ_INT(VALUE_str, background[index].type);
    *sink ^= (uintptr_t)background[index].s[0];
  }
  uint64_t elapsed = now_ns() - start;
  *probe = strbuf_probe_for_tests();
  free(right);
  return elapsed;
}

static uint64_t bench_string_cleanup(VALUE_t *values, size_t count,
                                     volatile uintptr_t *sink) {
  uint64_t start = now_ns();
  for (size_t i = 0; i < count; i++) {
    *sink ^= (uintptr_t)values[i].s[0];
    value_free(&values[i]);
  }
  return now_ns() - start;
}

static uint64_t bench_interpreter_string_workload(ITEMSTORE_t *store,
                                                  ITEM_t *item, size_t iters,
                                                  volatile uintptr_t *sink) {
  VM_t *vm = make_vm();
  ASSERT_NOT_NULL(vm);
  RuntimeContext ctx;
  runtime_context_init(&ctx, vm);
  ctx.itemstore = store;
  uint64_t start = now_ns();
  for (size_t i = 0; i < iters; i++) {
    VALUE_t result = interpret(&ctx, item);
    ASSERT_EQ_INT(VALUE_str, result.type);
    *sink ^= (uintptr_t)result.s[0];
    value_free(&result);
  }
  uint64_t elapsed = now_ns() - start;
  runtime_destroy(&ctx);
  destroy_vm(vm);
  return elapsed;
}

static void print_string_registry_row(const char *operation, size_t population,
                                      uint64_t median, size_t iters,
                                      strbuf_probe_t probe) {
  double probes = (double)(probe.find_nodes + probe.forget_nodes) /
                  (double)iters;
  printf("[bench][string_registry] op=%s live=%zu median_ns=%llu ns/op=%llu "
         "probe_nodes/op=%.1f find_nodes=%zu forget_nodes=%zu\n",
         operation, population, (unsigned long long)median,
         (unsigned long long)(median / iters), probes, probe.find_nodes,
         probe.forget_nodes);
}

static void run_extended_string_registry_benchmarks(void) {
  const size_t populations[] = {1u, 32u, 1024u, 4096u};
  const size_t samples = 5u;
  const size_t iters = 80u;
  const size_t baseline = strbuf_tracked_count_for_tests();
  volatile uintptr_t sink = 0;
  ITEMSTORE_t *store = itemstore_create("string_registry_bench");
  ASSERT_NOT_NULL(store);
  ITEM_t *item = bench_compile_item(store, "bench.string_workload",
                                    "return \"0123456789abcdef\" + \"x\";",
                                    NULL, 0);
  for (size_t p = 0; p < sizeof(populations) / sizeof(populations[0]); p++) {
    size_t population = populations[p];
    uint64_t lookup_samples[samples], removal_samples[samples];
    uint64_t reuse_samples[samples], growth_samples[samples], cleanup_samples[samples];
    uint64_t interpreter_samples[samples];
    strbuf_probe_t lookup_probe = {0}, removal_probe = {0}, reuse_probe = {0};
    strbuf_probe_t growth_probe = {0}, cleanup_probe = {0}, interpreter_probe = {0};
    for (size_t sample = 0; sample < samples; sample++) {
      VALUE_t *values = bench_make_tracked_population(population);
      strbuf_probe_reset_for_tests();
      lookup_samples[sample] = bench_string_capacity_lookup(values, iters, &sink);
      lookup_probe = strbuf_probe_for_tests();
      bench_free_tracked_population(values, population);

      values = bench_make_tracked_population(population);
      strbuf_probe_reset_for_tests();
      removal_samples[sample] = bench_string_removal(values, population,
                                                      population < iters ? population : iters,
                                                      &sink);
      removal_probe = strbuf_probe_for_tests();
      bench_free_tracked_population(values, population);

      size_t concat_iters = population < iters ? population : iters;
      values = bench_make_tracked_population(population);
      reuse_samples[sample] = bench_string_concat(values, population, concat_iters,
                                                  0, &sink, &reuse_probe);
      bench_free_tracked_population(values, population);

      values = bench_make_tracked_population_shape(population, 31u);
      growth_samples[sample] = bench_string_concat(values, population, concat_iters,
                                                   1, &sink, &growth_probe);
      bench_free_tracked_population(values, population);

      values = bench_make_tracked_population(population);
      strbuf_probe_reset_for_tests();
      cleanup_samples[sample] = bench_string_cleanup(values, population, &sink);
      cleanup_probe = strbuf_probe_for_tests();
      free(values);

      values = bench_make_tracked_population(population);
      strbuf_probe_reset_for_tests();
      interpreter_samples[sample] = bench_interpreter_string_workload(store, item, iters, &sink);
      interpreter_probe = strbuf_probe_for_tests();
      bench_free_tracked_population(values, population);
      ASSERT_EQ_INT((long long)baseline,
                    (long long)strbuf_tracked_count_for_tests());
    }
    uint64_t lookup = median_u64(lookup_samples, samples);
    uint64_t removal = median_u64(removal_samples, samples);
    uint64_t reuse = median_u64(reuse_samples, samples);
    uint64_t growth = median_u64(growth_samples, samples);
    uint64_t cleanup = median_u64(cleanup_samples, samples);
    uint64_t interpreter = median_u64(interpreter_samples, samples);
    print_string_registry_row("capacity_lookup", population, lookup, iters, lookup_probe);
    print_string_registry_row("removal", population, removal,
                              population < iters ? population : iters, removal_probe);
    size_t concat_iters = population < iters ? population : iters;
    print_string_registry_row("reuse_concat", population, reuse, concat_iters, reuse_probe);
    print_string_registry_row("growth_concat", population, growth, concat_iters, growth_probe);
    print_string_registry_row("cleanup", population, cleanup, population, cleanup_probe);
    print_string_registry_row("interpreter_concat", population, interpreter, iters, interpreter_probe);
    printf("[bench][string_registry] live=%zu ratios removal/lookup=%.3f "
           "growth/reuse=%.3f interpreter/lookup=%.3f\n", population,
           per_op_ratio(removal, population < iters ? population : iters,
                        lookup, iters),
           per_op_ratio(growth, concat_iters, reuse, concat_iters),
           per_op_ratio(interpreter, iters, lookup, iters));
  }
  itemstore_destroy(store);
  ASSERT_EQ_INT((long long)baseline, (long long)strbuf_tracked_count_for_tests());
  printf("[bench][string_registry] sink=%llu\n", (unsigned long long)sink);
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

  benchmark->store = itemstore_create("interp_bench");
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

typedef struct {
  const char *name;
  const unsigned char *input;
  size_t input_bytes;
} NetworkBenchmarkCase;

static void run_network_benchmark_case(const NetworkBenchmarkCase *test_case) {
  const size_t sample_count = 5;
  uint64_t elapsed_samples[sample_count];
  uint64_t maintenance_samples[sample_count];
  uint64_t allocation_samples[sample_count];
  uint64_t record_samples[sample_count];

  for (size_t sample = 0; sample < sample_count; sample++) {
    ASSERT_TRUE(test_network_reset(1));
    NetworkRuntime *runtime = test_network_runtime();
    network_runtime_test_reset_input_counters();
    ASSERT_TRUE(network_runtime_test_set_line(runtime, 0, NETWORK_TEST_IDLE));
    uint64_t start = now_ns();
    ASSERT_TRUE(network_runtime_test_feed(runtime, 0,
                                          (const char *)test_case->input,
                                          test_case->input_bytes));
    size_t records = 0;
    for (;;) {
      size_t line_index = 0;
      char *input = NULL;
      NetworkEvent event = network_runtime_poll(runtime, &line_index, &input);
      if (event == NETWORK_EVENT_NONE) break;
      if (event == NETWORK_EVENT_DATA) {
        ASSERT_TRUE(line_index == 0);
        ASSERT_NOT_NULL(input);
        free(input);
        records++;
      }
    }
    elapsed_samples[sample] = now_ns() - start;
    maintenance_samples[sample] =
        (uint64_t)network_runtime_test_input_maintenance_bytes();
    allocation_samples[sample] =
        (uint64_t)network_runtime_test_input_buffer_allocations();
    record_samples[sample] = (uint64_t)records;
  }

  printf("[bench][network] case=%s records=%llu input_bytes=%zu "
         "maintenance_bytes=%llu input_buffer_allocations=%llu "
         "median_elapsed_ns=%llu samples=%zu\n",
         test_case->name,
         (unsigned long long)median_u64(record_samples, sample_count),
         test_case->input_bytes,
         (unsigned long long)median_u64(maintenance_samples, sample_count),
         (unsigned long long)median_u64(allocation_samples, sample_count),
         (unsigned long long)median_u64(elapsed_samples, sample_count),
         sample_count);
}

static void run_extended_network_benchmarks(void) {
  const size_t short_batch_16k_bytes = 16384;
  const size_t short_batch_64k_bytes = 65534;
  unsigned char *short_batch_16k =
      (unsigned char *)malloc(short_batch_16k_bytes);
  unsigned char *short_batch_64k =
      (unsigned char *)malloc(short_batch_64k_bytes);
  ASSERT_NOT_NULL(short_batch_16k);
  ASSERT_NOT_NULL(short_batch_64k);
  memset(short_batch_16k, 'x', short_batch_16k_bytes);
  memset(short_batch_64k, 'x', short_batch_64k_bytes);
  for (size_t i = 1; i < short_batch_16k_bytes; i += 2) {
    short_batch_16k[i] = '\n';
  }
  for (size_t i = 1; i < short_batch_64k_bytes; i += 2) {
    short_batch_64k[i] = '\n';
  }

  unsigned char mixed[16384];
  size_t mixed_bytes = 0;
  const char *partial = "partial";
  memcpy(mixed + mixed_bytes, partial, strlen(partial));
  mixed_bytes += strlen(partial);
  const char *complete = " record\n";
  memcpy(mixed + mixed_bytes, complete, strlen(complete));
  mixed_bytes += strlen(complete);
  memset(mixed + mixed_bytes, 'l', 4096);
  mixed_bytes += 4096;
  mixed[mixed_bytes++] = '\n';
  while (mixed_bytes + 3 <= sizeof(mixed)) {
    mixed[mixed_bytes++] = 'm';
    mixed[mixed_bytes++] = '\n';
  }

  const NetworkBenchmarkCase cases[] = {
      {"short-16k", short_batch_16k, short_batch_16k_bytes},
      {"short-64k", short_batch_64k, short_batch_64k_bytes},
      {"mixed-partial-complete-long", mixed, mixed_bytes},
  };
  for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
    run_network_benchmark_case(&cases[i]);
  }
  free(short_batch_64k);
  free(short_batch_16k);
  test_network_clear();
}

void test_runtime_benchmark_optin(void) {
  uint8_t lib_index = 0, call_index = 0;
  uint8_t args = 0;
  ASSERT_TRUE(libcall_lookup_pair("str", "upper", &lib_index, &call_index, &args));
  ASSERT_NOT_NULL(libcall_func_pair(lib_index, call_index));

  const int lookup_iters = 150000;
  const int dispatch_iters = 3000000;
  volatile uintptr_t sink = 0;

  uint64_t t0 = now_ns();
  for (int i = 0; i < lookup_iters; i++) {
    uint8_t li = 0, ci = 0;
    uint8_t ar = 0;
    int ok = libcall_lookup_pair("str", "upper", &li, &ci, &ar);
    sink ^= (uintptr_t)li;
    sink ^= (uintptr_t)ci;
    sink ^= (uintptr_t)ar;
    ASSERT_TRUE(ok);
  }
  uint64_t t1 = now_ns();

  uint64_t t2 = now_ns();
  for (int i = 0; i < dispatch_iters; i++) {
    OP_t fn = libcall_func_pair(lib_index, call_index);
    sink ^= (uintptr_t)fn;
    ASSERT_NOT_NULL(fn);
  }
  uint64_t t3 = now_ns();

  uint64_t lookup_total = t1 - t0;
  uint64_t dispatch_total = t3 - t2;
  uint64_t lookup_per_op = lookup_total / (uint64_t)lookup_iters;
  uint64_t dispatch_per_op = dispatch_total / (uint64_t)dispatch_iters;

  printf("[bench] lookup_pair(str.upper): total=%llu ns iters=%d per_op=%llu ns\n",
         (unsigned long long)lookup_total, lookup_iters, (unsigned long long)lookup_per_op);
  printf("[bench] dispatch_pair(str.upper): total=%llu ns iters=%d per_op=%llu ns\n",
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
    setup_libcall_runtime();
    run_extended_network_benchmarks();
    teardown_libcall_runtime();
    run_extended_list_benchmarks();
    run_extended_string_registry_benchmarks();
    run_extended_itemstore_benchmarks();
    run_extended_itemref_and_syscall_benchmarks();
    run_runtime_verification_cache_benchmarks();
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
