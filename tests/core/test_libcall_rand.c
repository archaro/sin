#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "compiler/compiler_pipeline.h"
#include "config.h"
#include "error.h"
#include "itemref.h"
#include "libcall.h"
#include "libcall_handlers.h"
#include "libcall_rand.h"
#include "list.h"
#include "memory.h"
#include "stack.h"
#include "test_assert.h"
#include "shared/test_libcall_support.h"

extern CONFIG_t config;
static size_t entropy_calls;
static const uint64_t *draws;
static size_t draw_count;
static size_t draw_index;

static bool fixed_entropy(uint64_t seed[4]) {
  entropy_calls++;
  seed[0] = 1; seed[1] = 2; seed[2] = 3; seed[3] = 4;
  return true;
}

static bool failed_entropy(uint64_t seed[4]) {
  (void)seed;
  entropy_calls++;
  return false;
}

static bool zero_entropy(uint64_t seed[4]) {
  entropy_calls++;
  memset(seed, 0, 4 * sizeof(*seed));
  return true;
}

static uint64_t next_draw(void) {
  ASSERT_TRUE(draw_index < draw_count);
  return draws[draw_index++];
}

static void inject(const uint64_t *words, size_t count) {
  draws = words;
  draw_count = count;
  draw_index = 0;
  libcall_rand_test_draw(next_draw);
}

static void setup_rand(void) {
  setup_libcall_runtime();
  entropy_calls = 0;
  libcall_rand_test_reset(fixed_entropy);
}

static void teardown_rand(void) {
  libcall_rand_test_reset(NULL);
  teardown_libcall_runtime();
}

static VALUE_t call_rand(OP_t handler, VALUE_t *args, size_t count) {
  int before = size_stack(config.vm->stack);
  for (size_t i = 0; i < count; ++i) push_stack(config.vm->stack, args[i]);
  uint8_t continuation = 0;
  ASSERT_TRUE(handler(test_ctx(), &continuation, NULL) == &continuation);
  ASSERT_EQ_INT(before + 1, size_stack(config.vm->stack));
  return pop_stack(config.vm->stack);
}

static VALUE_t call_int(int64_t min, int64_t max) {
  VALUE_t args[] = {{VALUE_int, {.i = min}}, {VALUE_int, {.i = max}}};
  return call_rand(lc_rand_int, args, 2);
}

static void assert_error_code(int code) {
  ITEM_t *error = find_item(itemstore_root(config.itemstore_ctx), "error");
  ASSERT_NOT_NULL(error);
  ASSERT_EQ_INT(code, item_value(error)->i);
}

void test_rand_registry_contract(void) {
  const char *names[] = {"int", "float", "chance", "choice"};
  const uint8_t arities[] = {2, 0, 1, 1};
  const OP_t handlers[] = {lc_rand_int, lc_rand_float, lc_rand_chance,
                           lc_rand_choice};
  size_t count = 0;
  while (libcalls[count].libname) count++;
  ASSERT_EQ_INT(86, count);
  for (size_t i = 0; i < 4; ++i) {
    uint8_t lib = 0, call = 0, args = 0;
    ASSERT_TRUE(libcall_lookup_pair("rand", names[i], &lib, &call, &args));
    ASSERT_EQ_INT(7, lib);
    ASSERT_EQ_INT(i, call);
    ASSERT_EQ_INT(arities[i], args);
    ASSERT_TRUE(libcall_func_pair(lib, call) == handlers[i]);
    ASSERT_TRUE(libcall_pair_arg_count(lib, call, &args));
    ASSERT_EQ_INT(arities[i], args);
  }
}

void test_rand_generator_vector_and_context_lifetime(void) {
  /* Published xoshiro256** transition applied to seed {1,2,3,4}. Compare
   * complete words through the full-width public integer operation. */
  const uint64_t expected[] = {
      UINT64_C(11520), UINT64_C(0), UINT64_C(1509978240),
      UINT64_C(1215971899390074240), UINT64_C(1216172134540287360)};
  setup_rand();
  for (size_t i = 0; i < sizeof(expected) / sizeof(expected[0]); ++i) {
    RuntimeContext ctx;
    runtime_context_init(&ctx, config.vm);
    ctx.itemstore = config.itemstore_ctx;
    ASSERT_TRUE(runtime_init(&ctx, config.vm));
    ASSERT_TRUE(runtime_init(&ctx, config.vm));
    VALUE_t value = call_int(INT64_MIN, INT64_MAX);
    ASSERT_EQ_INT(VALUE_int, value.type);
    ASSERT_TRUE((uint64_t)value.i - (uint64_t)INT64_MIN == expected[i]);
    runtime_destroy(&ctx);
  }
  ASSERT_EQ_INT(1, entropy_calls);
  teardown_rand();
}

void test_rand_entropy_failures(void) {
  bool (*sources[])(uint64_t seed[4]) = {failed_entropy, zero_entropy};
  setup_rand();
  for (size_t i = 0; i < 2; ++i) {
    libcall_rand_test_reset(sources[i]);
    entropy_calls = 0;
    RuntimeContext ctx;
    runtime_context_init(&ctx, config.vm);
    ctx.itemstore = config.itemstore_ctx;
    ASSERT_TRUE(!runtime_init(&ctx, config.vm));
    ASSERT_TRUE(!ctx.initialized && ctx.libcalls == NULL);
    ASSERT_EQ_INT(1, entropy_calls);
    runtime_destroy(&ctx);
    VALUE_t value = call_rand(lc_rand_float, NULL, 0);
    ASSERT_EQ_INT(VALUE_nil, value.type);
    assert_error_code(ERR_RUNTIME_UNDEFINED);
    value = call_int(-1, 1);
    ASSERT_EQ_INT(VALUE_nil, value.type);
    VALUE_t probability = {VALUE_float, {.f = 0.5}};
    value = call_rand(lc_rand_chance, &probability, 1);
    ASSERT_EQ_INT(VALUE_nil, value.type);
    VALUE_t element = {VALUE_str, {.s = strdup("consumed on seed failure")}};
    ASSERT_NOT_NULL(element.s);
    VALUE_t list = {VALUE_list, {.list = sin_list_build_owned(&element, 1)}};
    ASSERT_NOT_NULL(list.list);
    value = call_rand(lc_rand_choice, &list, 1);
    ASSERT_EQ_INT(VALUE_nil, value.type);
    assert_error_code(ERR_RUNTIME_UNDEFINED);
  }
  /* A later valid initialization succeeds; a rejected seed was not latched. */
  libcall_rand_test_reset(fixed_entropy);
  ASSERT_TRUE(libcall_rand_init());
  teardown_rand();
}

void test_rand_int_boundaries_and_rejection(void) {
  const struct {
    int64_t min, max;
    uint64_t word;
    int64_t expected;
  } cases[] = {
      {7, 7, 0, 7}, {INT64_MIN, INT64_MIN, 0, INT64_MIN},
      {INT64_MAX, INT64_MAX, UINT64_MAX, INT64_MAX},
      {-9, -2, 0, -9}, {-9, -2, 7, -2}, {-4, 3, 0, -4}, {-4, 3, 7, 3},
      {INT64_MIN, INT64_MAX, 0, INT64_MIN},
      {INT64_MIN, INT64_MAX, UINT64_MAX, INT64_MAX},
      {INT64_MIN, INT64_MAX, UINT64_C(1) << 63, 0},
      {INT64_MIN, 0, UINT64_C(1) << 63, 0},
      {0, INT64_MAX, UINT64_MAX, INT64_MAX}};
  setup_rand();
  for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
    inject(&cases[i].word, 1);
    VALUE_t result = call_int(cases[i].min, cases[i].max);
    ASSERT_EQ_INT(VALUE_int, result.type);
    ASSERT_EQ_INT(cases[i].expected, result.i);
  }
  const uint64_t rejection[] = {0, 5, 6, 10}; /* bound 10: threshold 6 */
  inject(rejection, 4);
  ASSERT_EQ_INT(6, call_int(0, 9).i);
  ASSERT_EQ_INT(3, draw_index);
  ASSERT_EQ_INT(0, call_int(0, 9).i);
  ASSERT_EQ_INT(4, draw_index);
  const uint64_t wide[] = {0, (UINT64_C(1) << 63) - 2,
                            (UINT64_C(1) << 63) - 1};
  inject(wide, 3);
  ASSERT_EQ_INT(-1, call_int(INT64_MIN, 0).i);
  ASSERT_EQ_INT(3, draw_index);
  teardown_rand();
}

void test_rand_invalid_arguments(void) {
  setup_rand();
  VALUE_t bad_ints[][2] = {
      {{VALUE_int, {.i = 2}}, {VALUE_int, {.i = 1}}},
      {{VALUE_float, {.f = 1.0}}, {VALUE_int, {.i = 2}}},
      {{VALUE_int, {.i = 1}}, {VALUE_bool, {.i = 1}}},
      {{VALUE_str, {.s = strdup("left")}},
       {VALUE_str, {.s = strdup("right")}}}};
  ASSERT_NOT_NULL(bad_ints[3][0].s);
  ASSERT_NOT_NULL(bad_ints[3][1].s);
  for (size_t i = 0; i < 4; ++i) {
    ASSERT_EQ_INT(VALUE_nil, call_rand(lc_rand_int, bad_ints[i], 2).type);
    assert_invalid_args_detail_contains("rand.int");
  }
  VALUE_t bad_chance[] = {VALUE_NIL, VALUE_TRUE, {VALUE_int, {.i = -1}},
      {VALUE_int, {.i = 2}}, {VALUE_float, {.f = -0.01}},
      {VALUE_float, {.f = nextafter(1.0, 2.0)}},
      {VALUE_float, {.f = INFINITY}}, {VALUE_float, {.f = -INFINITY}},
      {VALUE_str, {.s = strdup("owned probability")}}};
  ASSERT_NOT_NULL(bad_chance[8].s);
  for (size_t i = 0; i < sizeof(bad_chance) / sizeof(bad_chance[0]); ++i) {
    ASSERT_EQ_INT(VALUE_nil, call_rand(lc_rand_chance, &bad_chance[i], 1).type);
    assert_invalid_args_detail_contains("rand.chance");
  }
  VALUE_t nan = {VALUE_float, {.f = NAN}};
  ASSERT_EQ_INT(VALUE_nil, call_rand(lc_rand_chance, &nan, 1).type);
  assert_error_code(ERR_RUNTIME_UNDEFINED);
  VALUE_t bad_choice[] = {VALUE_NIL, {VALUE_list, {.list = NULL}},
      {VALUE_str, {.s = strdup("owned nonlist")}}};
  ASSERT_NOT_NULL(bad_choice[2].s);
  for (size_t i = 0; i < 3; ++i) {
    ASSERT_EQ_INT(VALUE_nil, call_rand(lc_rand_choice, &bad_choice[i], 1).type);
    assert_invalid_args_detail_contains("rand.choice");
  }
  ASSERT_EQ_INT(0, entropy_calls);
  teardown_rand();
}

void test_rand_float_and_chance_boundaries(void) {
  const uint64_t words[] = {0, UINT64_MAX, 0, UINT64_MAX,
      (UINT64_C(1) << 63) - 1, UINT64_C(1) << 63};
  setup_rand();
  inject(words, 6);
  VALUE_t result = call_rand(lc_rand_float, NULL, 0);
  ASSERT_EQ_INT(VALUE_float, result.type);
  ASSERT_TRUE(result.f == 0.0);
  result = call_rand(lc_rand_float, NULL, 0);
  ASSERT_TRUE(result.f == 1.0 - 0x1p-53);
  VALUE_t endpoints[] = {{VALUE_int, {.i = 0}}, {VALUE_int, {.i = 1}},
      {VALUE_float, {.f = -0.0}}, {VALUE_float, {.f = 1.0}}};
  for (size_t i = 0; i < 4; ++i) {
    result = call_rand(lc_rand_chance, &endpoints[i], 1);
    ASSERT_EQ_INT(VALUE_bool, result.type);
    ASSERT_EQ_INT(i % 2, result.i);
  }
  ASSERT_EQ_INT(2, draw_index);
  VALUE_t half = {VALUE_float, {.f = 0.5}};
  const int expected[] = {1, 0, 1, 0};
  for (size_t i = 0; i < 4; ++i) {
    result = call_rand(lc_rand_chance, &half, 1);
    ASSERT_EQ_INT(VALUE_bool, result.type);
    ASSERT_EQ_INT(expected[i], result.i);
  }
  teardown_rand();
}

static VALUE_t singleton_choice(VALUE_t element) {
  VALUE_t list = {VALUE_list, {.list = sin_list_build_owned(&element, 1)}};
  ASSERT_NOT_NULL(list.list);
  return call_rand(lc_rand_choice, &list, 1);
}

void test_rand_choice_ownership_and_selection(void) {
  setup_rand();
  VALUE_t result = singleton_choice(VALUE_NIL);
  ASSERT_EQ_INT(VALUE_nil, result.type);
  VALUE_t string = {VALUE_str, {.s = strdup("owned selection")}};
  ASSERT_NOT_NULL(string.s);
  result = singleton_choice(string);
  ASSERT_EQ_INT(VALUE_str, result.type);
  ASSERT_TRUE(strcmp(result.s, "owned selection") == 0);
  value_free(&result);
  VALUE_t nested_values[] = {{VALUE_str, {.s = strdup("nested")}}};
  ASSERT_NOT_NULL(nested_values[0].s);
  VALUE_t nested = {VALUE_list, {.list = sin_list_build_owned(nested_values, 1)}};
  ASSERT_NOT_NULL(nested.list);
  result = singleton_choice(nested);
  ASSERT_EQ_INT(VALUE_list, result.type);
  ASSERT_TRUE(strcmp(sin_list_get(result.list, 0)->s, "nested") == 0);
  value_free(&result);
  VALUE_t ref = {VALUE_itemref, {.itemref = sin_itemref_create("root.child")}};
  ASSERT_NOT_NULL(ref.itemref);
  result = singleton_choice(ref);
  ASSERT_EQ_INT(VALUE_itemref, result.type);
  ASSERT_TRUE(strcmp(sin_itemref_path(result.itemref), "root.child") == 0);
  value_free(&result);
  const uint64_t words[] = {0, 3, 4, 5}; /* three elements: reject zero */
  inject(words, 4);
  for (int64_t i = 0; i < 3; ++i) {
    VALUE_t values[] = {{VALUE_int, {.i = 10}}, {VALUE_int, {.i = 20}},
                        {VALUE_int, {.i = 30}}};
    VALUE_t list = {VALUE_list, {.list = sin_list_build_owned(values, 3)}};
    ASSERT_NOT_NULL(list.list);
    result = call_rand(lc_rand_choice, &list, 1);
    ASSERT_EQ_INT(VALUE_int, result.type);
    ASSERT_EQ_INT((i + 1) * 10, result.i);
  }
  ASSERT_EQ_INT(4, draw_index);
  teardown_rand();
}

void test_rand_preserves_errors_and_clone_failure(void) {
  setup_rand();
  set_error_item(itemstore_root(config.itemstore_ctx), ERR_NETWORK_ERROR,
                 "prior diagnostic", NULL);
  (void)call_int(1, 1);
  assert_error_code(ERR_NETWORK_ERROR);
  (void)call_rand(lc_rand_float, NULL, 0);
  assert_error_code(ERR_NETWORK_ERROR);
  VALUE_t probability = {VALUE_int, {.i = 1}};
  (void)call_rand(lc_rand_chance, &probability, 1);
  assert_error_code(ERR_NETWORK_ERROR);
  VALUE_t empty = {VALUE_list, {.list = sin_list_build_owned(NULL, 0)}};
  ASSERT_NOT_NULL(empty.list);
  ASSERT_EQ_INT(VALUE_nil, call_rand(lc_rand_choice, &empty, 1).type);
  assert_error_code(ERR_NETWORK_ERROR);
  ASSERT_EQ_INT(VALUE_nil, singleton_choice(VALUE_NIL).type);
  assert_error_code(ERR_NETWORK_ERROR);
  VALUE_t element = {VALUE_str, {.s = strdup("clone must fail")}};
  ASSERT_NOT_NULL(element.s);
  VALUE_t list = {VALUE_list, {.list = sin_list_build_owned(&element, 1)}};
  ASSERT_NOT_NULL(list.list);
  push_stack(config.vm->stack, list);
  RuntimeContext *ctx = test_ctx();
  alloc_test_fail_after(0);
  (void)lc_rand_choice(ctx, NULL, NULL);
  alloc_test_fail_after(-1);
  VALUE_t result = pop_stack(config.vm->stack);
  ASSERT_EQ_INT(VALUE_nil, result.type);
  ASSERT_EQ_INT(0, size_stack(config.vm->stack));
  assert_error_code(ERR_NETWORK_ERROR);
  ITEM_t *message = find_item(itemstore_root(config.itemstore_ctx), "error.msg");
  ASSERT_NOT_NULL(message);
  ASSERT_TRUE(strstr(item_value(message)->s, "prior diagnostic") != NULL);
  teardown_rand();
}

void test_rand_source_integration_and_arity(void) {
  setup_rand();
  VALUE_t source = {VALUE_str, {.s = strdup(
      "result.a = rand.int{-4, -4}; result.b = rand.float + 1.0; "
      "result.c = rand.chance{1}; result.d = rand.choice{#[\"chosen\"]};")}};
  ASSERT_NOT_NULL(source.s);
  VALUE_t result = call_rand(lc_sys_compile, &source, 1);
  ASSERT_EQ_INT(VALUE_bool, result.type);
  ASSERT_EQ_INT(1, result.i);
  ITEM_t *root = itemstore_root(config.itemstore_ctx);
  ITEM_t *a = find_item(root, "result.a"), *b = find_item(root, "result.b");
  ITEM_t *c = find_item(root, "result.c"), *d = find_item(root, "result.d");
  ASSERT_NOT_NULL(a); ASSERT_NOT_NULL(b); ASSERT_NOT_NULL(c); ASSERT_NOT_NULL(d);
  ASSERT_EQ_INT(VALUE_int, item_value(a)->type);
  ASSERT_EQ_INT(-4, item_value(a)->i);
  ASSERT_EQ_INT(VALUE_float, item_value(b)->type);
  ASSERT_TRUE(item_value(b)->f >= 1.0 && item_value(b)->f < 2.0);
  ASSERT_EQ_INT(VALUE_bool, item_value(c)->type);
  ASSERT_EQ_INT(1, item_value(c)->i);
  ASSERT_EQ_INT(VALUE_str, item_value(d)->type);
  ASSERT_TRUE(strcmp(item_value(d)->s, "chosen") == 0);
  const char *invalid[] = {"rand.int{1};", "rand.int{1, 2, 3};",
      "rand.float{1};", "rand.chance;", "rand.chance{0, 1};",
      "rand.choice;", "rand.choice{#[1], #[2]};"};
  for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
    OUTPUT_t *out = NULL;
    CompilerDiagnostic diag;
    compiler_diag_init(&diag);
    ASSERT_TRUE(compile_source_to_bytecode_diag(invalid[i], strlen(invalid[i]),
                                               &out, &diag) != 0);
    ASSERT_TRUE(out == NULL);
    ASSERT_EQ_INT(DIAG_PHASE_LOWER, diag.phase);
    ASSERT_NOT_NULL(diag.message);
    ASSERT_TRUE(strstr(diag.message, "invalid libcall argument count") != NULL);
    compiler_diag_reset(&diag);
  }
  teardown_rand();
}
