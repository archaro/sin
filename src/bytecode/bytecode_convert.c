#include "bytecode_convert.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "bytecode_format.h"
#include "bytecode_verify.h"
#include "bytecode_wire.h"
#include "memory.h"

typedef struct {
  uint32_t old_off, new_off;
  bool top;
} OffsetPair;
typedef struct {
  uint32_t old_operand, new_operand;
  int16_t rel;
} Jump;
typedef struct {
  const uint8_t *in;
  const uint8_t *end;
  OffsetPair *pairs;
  size_t pair_count;
  size_t pair_cap;
  Jump *jumps;
  size_t jump_count;
  size_t jump_cap;
  uint32_t out_len;
  bool emit;
  uint8_t *out;
  bool record;
  bool alloc_failed;
} ConvertCtx;

static size_t g_map_offset_probes;

static bool grow(void **p, size_t *cap, size_t n, size_t size) {
  return alloc_grow_array_capacity(p, cap, n, size);
}

static bool token_pair(uint8_t token, uint8_t *lib, uint8_t *call) {
  if (token < 25u) {
    if (lib)
      *lib = 1u;
    if (call)
      *call = token;
    return true;
  }
  if (token < 31u) {
    if (lib)
      *lib = 5u;
    if (call)
      *call = (uint8_t)(token - 25u);
    return true;
  }
  if (token < 38u) {
    if (lib)
      *lib = 3u;
    if (call)
      *call = (uint8_t)(token - 31u);
    return true;
  }
  if (token < 56u) {
    if (lib)
      *lib = 4u;
    if (call)
      *call = (uint8_t)(token - 38u);
    return true;
  }
  if (token < 61u) {
    if (lib)
      *lib = 2u;
    if (call)
      *call = (uint8_t)(token - 56u);
    return true;
  }
  return false;
}

static bool need(ConvertCtx *c, const uint8_t *p, size_t n) {
  return p <= c->end && (size_t)(c->end - p) >= n;
}
static bool add_pair(ConvertCtx *c, uint32_t old_off, uint32_t new_off,
                     bool top) {
  if (!grow((void **)&c->pairs, &c->pair_cap, c->pair_count + 1u,
            sizeof *c->pairs)) {
    c->alloc_failed = true;
    return false;
  }
  c->pairs[c->pair_count++] = (OffsetPair){old_off, new_off, top};
  return true;
}
static bool add_jump(ConvertCtx *c, uint32_t old_operand, uint32_t new_operand,
                     int16_t rel) {
  if (!grow((void **)&c->jumps, &c->jump_cap, c->jump_count + 1u,
            sizeof *c->jumps)) {
    c->alloc_failed = true;
    return false;
  }
  c->jumps[c->jump_count++] = (Jump){old_operand, new_operand, rel};
  return true;
}
static bool emit_bytes(ConvertCtx *c, const uint8_t *p, size_t n) {
  if (n > UINT32_MAX - c->out_len)
    return false;
  if (c->emit)
    memcpy(c->out + c->out_len, p, n);
  c->out_len += (uint32_t)n;
  return true;
}

static bool scan_one(ConvertCtx *c, const uint8_t **pp, bool nested);

static bool scan_item(ConvertCtx *c, const uint8_t **pp) {
  for (;;) {
    if (!need(c, *pp, 1u))
      return false;
    if (**pp == 'E')
      return scan_one(c, pp, true);
    if (**pp != 'L' && **pp != 'D')
      return false;
    if (!scan_one(c, pp, true))
      return false;
  }
}
static bool scan_deref(ConvertCtx *c, const uint8_t **pp) {
  if (!need(c, *pp, 1u))
    return false;
  const uint8_t *start = *pp;
  uint8_t t = *(*pp)++;
  if (!emit_bytes(c, start, 1u))
    return false;
  if (t == 'V') {
    if (!need(c, *pp, 1u))
      return false;
    if (!emit_bytes(c, *pp, 1u))
      return false;
    (*pp)++;
    return true;
  }
  if (t == 'I' || t == 'R')
    return scan_item(c, pp);
  return false;
}

static bool scan_one(ConvertCtx *c, const uint8_t **pp, bool nested) {
  const uint8_t *start = *pp;
  if (!need(c, start, 1u))
    return false;
  uint8_t op = *(*pp)++;
  uint32_t old_off = (uint32_t)(start - c->in);
  if (c->record && !add_pair(c, old_off, c->out_len, !nested))
    return false;
  if (op == 'M') {
    if (!need(c, *pp, 1u) || !token_pair(**pp, NULL, NULL))
      return false;
    uint8_t lib, call;
    token_pair(**pp, &lib, &call);
    (*pp)++;
    if (!emit_bytes(c, start, 1u))
      return false;
    uint8_t pair[2] = {lib, call};
    return emit_bytes(c, pair, 2u);
  }
  size_t n = 0;
  bool emitted = false;
  switch (op) {
  case 'h':
  case 'Q':
  case 'N':
  case 'a':
  case 's':
  case 'm':
  case 'd':
  case '%':
  case 'n':
  case 'o':
  case 'q':
  case 'r':
  case 't':
  case 'u':
  case 'v':
  case 'x':
  case 'y':
  case 'z':
  case 'w':
  case 'E':
  case 'C':
  case '&':
    n = 0;
    break;
  case 'b':
  case 'e':
  case 'c':
  case 'f':
  case 'g':
  case 'V':
    n = 1;
    break;
  case 'F':
    n = 2u;
    break;
  case 'j':
  case 'k':
    if (!need(c, *pp, 2u))
      return false;
    if (c->record && !add_jump(c, (uint32_t)(*pp - c->in), c->out_len + 1u,
                               bc_wire_load_i16(*pp)))
      return false;
    n = 2u;
    break;
  case 'p':
  case 'P':
    n = 8;
    break;
  case 'l':
  case 'B': {
    bool has_params = false;
    if (op == 'B' && need(c, *pp, 1u) && **pp == 'P') {
      has_params = true;
      (*pp)++;
      for (;;) {
        if (!need(c, *pp, 2u))
          return false;
        uint16_t z = bc_wire_load_u16(*pp);
        *pp += 2u;
        if (z == 0u)
          break;
        if (!need(c, *pp, z))
          return false;
        *pp += z;
      }
    }
    if (!need(c, *pp, 2u))
      return false;
    uint16_t z = bc_wire_load_u16(*pp);
    n = (size_t)(*pp - start - 1u) + 2u + z;
    if (op == 'B' && !has_params) {
      if (!emit_bytes(c, start, 1u)) return false;
      uint8_t marker = 'P';
      uint8_t zero[2] = {0, 0};
      if (!emit_bytes(c, &marker, 1u) || !emit_bytes(c, zero, 2u) ||
          !emit_bytes(c, *pp, 2u + z)) return false;
      *pp += 2u + z;
      emitted = true;
      return true;
    }
    *pp = start + 1u;
    break;
  }
  case 'L': {
    if (!need(c, *pp, 1u))
      return false;
    n = 1u + **pp;
    break;
  }
  case '[':
    n = 4u;
    break;
  case 'I':
  case 'R':
    if (!emit_bytes(c, start, 1u))
      return false;
    emitted = true;
    if (!scan_item(c, pp))
      return false;
    n = 0;
    break;
  case 'D':
    if (!emit_bytes(c, start, 1u))
      return false;
    emitted = true;
    if (!scan_deref(c, pp))
      return false;
    n = 0;
    break;
  default:
    return false;
  }
  if (!need(c, *pp, n))
    return false;
  if (!emitted && !emit_bytes(c, start, 1u + n))
    return false;
  *pp += n;
  (void)nested;
  return true;
}

static uint32_t map_offset(const ConvertCtx *c, uint32_t old) {
  size_t lo = 0;
  size_t hi = c->pair_count;
  while (lo < hi) {
    size_t mid = lo + (hi - lo) / 2u;
    g_map_offset_probes++;
    if (c->pairs[mid].old_off < old) {
      lo = mid + 1u;
    } else {
      hi = mid;
    }
  }
  if (lo < c->pair_count) {
    g_map_offset_probes++;
    if (c->pairs[lo].old_off == old && c->pairs[lo].top)
      return c->pairs[lo].new_off;
  }
  return UINT32_MAX;
}

void bc_convert_test_reset_lookup_probes(void) { g_map_offset_probes = 0; }

size_t bc_convert_test_lookup_probes(void) { return g_map_offset_probes; }

BC_ConvertResult bc_convert_latest(const uint8_t *input, uint32_t length) {
  BC_ConvertResult r = {BC_CONVERT_INVALID, NULL, 0};
  BC_FormatHeader h;
  BC_FormatStatus fs = bc_decode_header(input, length, &h);
  if (fs == BC_FORMAT_UNSUPPORTED_VERSION) {
    r.status = BC_CONVERT_UNSUPPORTED_VERSION;
    return r;
  }
  if (fs == BC_FORMAT_TRUNCATED) {
    r.status = BC_CONVERT_TRUNCATED;
    return r;
  }
  if (fs != BC_FORMAT_OK)
    return r;
  if (!h.legacy) {
    BC_VerifyResult v = bc_verify_executable_bytecode(input, length,
                                                      "conversion");
    if (v.status != BC_VERIFY_OK)
      return r;
    r.data = alloc_malloc(length);
    if (!r.data && length) {
      r.status = BC_CONVERT_ALLOCATION_FAILURE;
      return r;
    }
    memcpy(r.data, input, length);
    r.length = length;
    r.status = BC_CONVERT_SUCCESS;
    return r;
  }
  ConvertCtx c = {.in = input, .end = input + length, .record = true};
  c.out_len = BC_V1_HEADER_SIZE;
  const uint8_t *p = h.instructions;
  while (p < c.end) {
    if (!scan_one(&c, &p, false))
      goto done;
  }
  if (p != c.end)
    goto done;
  r.data = alloc_malloc(c.out_len);
  if (!r.data) {
    r.status = BC_CONVERT_ALLOCATION_FAILURE;
    goto done;
  }
  c.emit = true;
  c.record = false;
  c.out = r.data;
  c.out_len = BC_V1_HEADER_SIZE;
  bc_encode_v1_header(r.data, h.locals, h.params);
  p = h.instructions;
  while (p < c.end) {
    if (!scan_one(&c, &p, false)) {
      free(r.data);
      r.data = NULL;
      goto done;
    }
  }
  for (size_t i = 0; i < c.jump_count; i++) {
    int64_t target_calc =
        (int64_t)c.jumps[i].old_operand + (int64_t)c.jumps[i].rel;
    if (target_calc < 0 || target_calc > UINT32_MAX) {
      free(r.data);
      r.data = NULL;
      goto done;
    }
    uint32_t target_old = (uint32_t)target_calc;
    uint32_t target_new = map_offset(&c, target_old);
    if (target_new == UINT32_MAX) {
      free(r.data);
      r.data = NULL;
      goto done;
    }
    int64_t rel = (int64_t)target_new - (int64_t)c.jumps[i].new_operand;
    if (rel < INT16_MIN || rel > INT16_MAX) {
      free(r.data);
      r.data = NULL;
      goto done;
    }
    bc_wire_store_i16(r.data + c.jumps[i].new_operand, (int16_t)rel);
  }
  r.length = c.out_len;
  r.status = BC_CONVERT_SUCCESS;
  {
    BC_VerifyResult v = bc_verify_executable_bytecode(
        r.data, r.length, "converted legacy");
    if (v.status != BC_VERIFY_OK) {
      free(r.data);
      r.data = NULL;
      r.length = 0;
      r.status = BC_CONVERT_INVALID;
    }
  }
done:
  if (c.alloc_failed) {
    free(r.data);
    r.data = NULL;
    r.length = 0;
    r.status = BC_CONVERT_ALLOCATION_FAILURE;
  }
  free(c.pairs);
  free(c.jumps);
  return r;
}

void bc_convert_result_free(BC_ConvertResult *result) {
  if (result) {
    free(result->data);
    result->data = NULL;
    result->length = 0;
  }
}
