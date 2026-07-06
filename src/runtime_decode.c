// Runtime bytecode decoder helpers

// Licensed under the MIT License - see LICENSE file for details.

#include "runtime_decode.h"

#include <stdio.h>
#include <string.h>

void runtime_decoder_init(RuntimeDecoder *decoder, const uint8_t *frame_start, const uint8_t *frame_end) {
  if (!decoder) return;
  decoder->frame_start = frame_start;
  decoder->frame_end = frame_end;
}

bool runtime_decode_status_ok(RuntimeDecodeStatus status) {
  return status.code == RUNTIME_DECODE_OK;
}

static RuntimeDecodeStatus runtime_decode_ok(uint8_t *next) {
  RuntimeDecodeStatus status;
  status.code = RUNTIME_DECODE_OK;
  status.next = next;
  status.detail[0] = '\0';
  return status;
}

static RuntimeDecodeStatus runtime_decode_truncated(size_t bytes, const char *opname) {
  RuntimeDecodeStatus status;
  status.code = RUNTIME_DECODE_TRUNCATED;
  status.next = NULL;
  snprintf(status.detail, sizeof(status.detail), "%s truncated bytecode read (%zu bytes)", opname, bytes);
  return status;
}

RuntimeDecodeStatus require_bytes(const RuntimeDecoder *decoder, uint8_t *nextop, size_t bytes, const char *opname) {
  if (!decoder || !decoder->frame_start || !decoder->frame_end) return runtime_decode_ok(nextop);
  if ((const uint8_t *)nextop > decoder->frame_end || (size_t)(decoder->frame_end - (const uint8_t *)nextop) < bytes) {
    return runtime_decode_truncated(bytes, opname);
  }
  return runtime_decode_ok(nextop);
}

RuntimeDecodeStatus bc_read_u8(const RuntimeDecoder *decoder, uint8_t *nextop, uint8_t *out, const char *opname) {
  RuntimeDecodeStatus status = require_bytes(decoder, nextop, sizeof(*out), opname);
  if (!runtime_decode_status_ok(status)) return status;
  memcpy(out, nextop, sizeof(*out));
  return runtime_decode_ok(nextop + sizeof(*out));
}

RuntimeDecodeStatus bc_read_u16(const RuntimeDecoder *decoder, uint8_t *nextop, uint16_t *out, const char *opname) {
  RuntimeDecodeStatus status = require_bytes(decoder, nextop, sizeof(*out), opname);
  if (!runtime_decode_status_ok(status)) return status;
  memcpy(out, nextop, sizeof(*out));
  return runtime_decode_ok(nextop + sizeof(*out));
}

RuntimeDecodeStatus bc_read_i16(const RuntimeDecoder *decoder, uint8_t *nextop, int16_t *out, const char *opname) {
  RuntimeDecodeStatus status = require_bytes(decoder, nextop, sizeof(*out), opname);
  if (!runtime_decode_status_ok(status)) return status;
  memcpy(out, nextop, sizeof(*out));
  return runtime_decode_ok(nextop + sizeof(*out));
}

RuntimeDecodeStatus bc_read_u64_payload(const RuntimeDecoder *decoder, uint8_t *nextop, uint64_t *out, const char *opname) {
  RuntimeDecodeStatus status = require_bytes(decoder, nextop, sizeof(*out), opname);
  if (!runtime_decode_status_ok(status)) return status;
  memcpy(out, nextop, sizeof(*out));
  return runtime_decode_ok(nextop + sizeof(*out));
}

RuntimeDecodeStatus bc_read_i64(const RuntimeDecoder *decoder, uint8_t *nextop, int64_t *out, const char *opname) {
  uint64_t payload;
  RuntimeDecodeStatus status = bc_read_u64_payload(decoder, nextop, &payload, opname);
  if (!runtime_decode_status_ok(status)) return status;
  memcpy(out, &payload, sizeof(*out));
  return status;
}
