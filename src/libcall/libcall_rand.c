#include <math.h>
#include <stdint.h>
#include <string.h>
#include <uv.h>

#include "libcall_common.h"
#include "libcall_handlers.h"
#include "libcall_rand.h"
#include "list.h"
#include "stack.h"

static uint64_t state[4];
static bool initialized;
static bool (*test_entropy)(uint64_t seed[4]);
static uint64_t (*test_draw)(void);

void libcall_rand_test_reset(bool (*entropy)(uint64_t seed[4])) {
  memset(state, 0, sizeof(state));
  initialized = false;
  test_entropy = entropy;
  test_draw = NULL;
}

void libcall_rand_test_draw(uint64_t (*draw)(void)) {
  test_draw = draw;
}

bool libcall_rand_init(void) {
  uint64_t seed[4];
  if (initialized) return true;
  bool ok = test_entropy ? test_entropy(seed)
      : uv_random(NULL, NULL, seed, sizeof(seed), 0, NULL) == 0;
  /* All-zero is the absorbing state. Fail rather than weaken the seed. */
  if (!ok || !(seed[0] | seed[1] | seed[2] | seed[3])) return false;
  memcpy(state, seed, sizeof(state));
  initialized = true;
  return true;
}

static uint64_t rotate_left(uint64_t value, unsigned int bits) {
  return (value << bits) | (value >> (64u - bits));
}

static uint64_t random_word(void) {
  if (test_draw) return test_draw();
  /* xoshiro256** by David Blackman and Sebastiano Vigna (public domain):
   * https://prng.di.unimi.it/xoshiro256starstar.c */
  uint64_t result = rotate_left(state[1] * UINT64_C(5), 7) * UINT64_C(9);
  uint64_t t = state[1] << 17;
  state[2] ^= state[0];
  state[3] ^= state[1];
  state[1] ^= state[2];
  state[0] ^= state[3];
  state[2] ^= t;
  state[3] = rotate_left(state[3], 45);
  return result;
}

/* A zero bound denotes the full 2^64 range. Discard the incomplete residue
 * cycle at the bottom, leaving equally many words for every output. */
static uint64_t random_below(uint64_t bound) {
  if (bound == 0) return random_word();
  uint64_t threshold = (UINT64_C(0) - bound) % bound;
  uint64_t word;
  do {
    word = random_word();
  } while (word < threshold);
  return word % bound;
}

static double random_fraction(void) {
  return (double)(random_word() >> 11) * 0x1p-53;
}

static uint8_t *random_undefined(RuntimeContext *ctx, uint8_t *nextop) {
  set_error_item(ctx ? itemstore_root(ctx->itemstore) : NULL,
                 ERR_RUNTIME_UNDEFINED, NULL,
                 ctx ? ctx->current_item : NULL);
  push_stack(ctx->vm->stack, VALUE_NIL);
  return nextop;
}

uint8_t *lc_rand_int(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item) {
  (void)item;
  VALUE_t maximum = pop_stack(ctx->vm->stack);
  VALUE_t minimum = pop_stack(ctx->vm->stack);
  if (minimum.type != VALUE_int || maximum.type != VALUE_int ||
      minimum.i > maximum.i) {
    value_free(&minimum);
    value_free(&maximum);
    return lc_invalid_args_detail_return(ctx, nextop, VALUE_NIL,
        "rand.int expects two integers with min <= max");
  }
  /* Bias signed values into ascending unsigned coordinates. All arithmetic
   * here is defined modulo 2^64, including the full-width range length. */
  uint64_t low = (uint64_t)minimum.i - (uint64_t)INT64_MIN;
  uint64_t high = (uint64_t)maximum.i - (uint64_t)INT64_MIN;
  value_free(&minimum);
  value_free(&maximum);
  if (!libcall_rand_init()) return random_undefined(ctx, nextop);
  uint64_t coordinate = low + random_below(high - low + UINT64_C(1));
  /* Cast only representable unsigned values, avoiding implementation-defined
   * conversion to signed even for INT64_MIN and the full-width range. */
  uint64_t half = UINT64_C(1) << 63;
  int64_t result = coordinate < half
      ? INT64_MIN + (int64_t)coordinate : (int64_t)(coordinate - half);
  push_stack(ctx->vm->stack, (VALUE_t){VALUE_int, {.i = result}});
  return nextop;
}

uint8_t *lc_rand_float(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item) {
  (void)item;
  if (!libcall_rand_init()) return random_undefined(ctx, nextop);
  push_stack(ctx->vm->stack, (VALUE_t){VALUE_float, {.f = random_fraction()}});
  return nextop;
}

uint8_t *lc_rand_chance(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item) {
  (void)item;
  VALUE_t value = pop_stack(ctx->vm->stack);
  if (value.type != VALUE_int && value.type != VALUE_float) {
    value_free(&value);
    return lc_invalid_args_detail_return(ctx, nextop, VALUE_NIL,
        "rand.chance expects an integer or float in [0, 1]");
  }
  double probability = value.type == VALUE_int ? (double)value.i : value.f;
  value_free(&value);
  if (isnan(probability)) return random_undefined(ctx, nextop);
  if (probability < 0.0 || probability > 1.0) {
    return lc_invalid_args_detail_return(ctx, nextop, VALUE_NIL,
        "rand.chance expects an integer or float in [0, 1]");
  }
  if (!libcall_rand_init()) return random_undefined(ctx, nextop);
  bool result = probability == 1.0 ||
      (probability != 0.0 && random_fraction() < probability);
  push_stack(ctx->vm->stack, result ? VALUE_TRUE : VALUE_FALSE);
  return nextop;
}

uint8_t *lc_rand_choice(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item) {
  (void)item;
  VALUE_t list = pop_stack(ctx->vm->stack);
  VALUE_t result = VALUE_NIL;
  if (list.type != VALUE_list || !list.list) {
    value_free(&list);
    return lc_invalid_args_detail_return(ctx, nextop, VALUE_NIL,
        "rand.choice expects a list");
  }
  size_t count = sin_list_count(list.list);
  if (count != 0) {
    if (!libcall_rand_init()) {
      value_free(&list);
      return random_undefined(ctx, nextop);
    }
    size_t index = (size_t)random_below((uint64_t)count);
    (void)value_clone_fallible(sin_list_get(list.list, index), &result);
  }
  value_free(&list);
  push_stack(ctx->vm->stack, result);
  return nextop;
}
