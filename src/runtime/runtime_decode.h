// Runtime bytecode decoder helpers

// Licensed under the MIT License - see LICENSE file for details.

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum RuntimeDecodeCode {
  RUNTIME_DECODE_OK = 0,
  RUNTIME_DECODE_TRUNCATED
} RuntimeDecodeCode;

typedef struct RuntimeDecoder {
  const uint8_t *frame_start;
  const uint8_t *frame_end;
} RuntimeDecoder;

typedef struct RuntimeDecodeStatus {
  RuntimeDecodeCode code;
  uint8_t *next;
  char detail[128];
} RuntimeDecodeStatus;

/*
 * Initialize a non-owning decoder view over the active bytecode frame.
 * The decoder does not allocate, copy, or free bytecode; callers retain
 * ownership of the memory referenced by frame_start and frame_end.
 */
void runtime_decoder_init(RuntimeDecoder *decoder, const uint8_t *frame_start, const uint8_t *frame_end);

/*
 * Ensure that bytes are available at nextop without advancing nextop.
 * Callers must pass a decoder initialized with the active bytecode frame
 * bounds; missing bounds are treated as a truncated decode.
 * Returns RUNTIME_DECODE_TRUNCATED with a stable detail string when the
 * requested range is outside the decoder frame.
 */
RuntimeDecodeStatus require_bytes(const RuntimeDecoder *decoder, uint8_t *nextop, size_t bytes, const char *opname);

/*
 * Read helpers copy the requested payload from nextop into out and return the
 * first unread byte in status.next. On success, nextop advances exactly by the
 * size of the decoded type. On failure, out is left untouched and status.next
 * is NULL; callers should stop interpreting the current frame and surface the
 * returned structured status as an interpreter/runtime error.
 */
RuntimeDecodeStatus bc_read_u8(const RuntimeDecoder *decoder, uint8_t *nextop, uint8_t *out, const char *opname);
RuntimeDecodeStatus bc_read_u16(const RuntimeDecoder *decoder, uint8_t *nextop, uint16_t *out, const char *opname);
RuntimeDecodeStatus bc_read_i16(const RuntimeDecoder *decoder, uint8_t *nextop, int16_t *out, const char *opname);
RuntimeDecodeStatus bc_read_u64_payload(const RuntimeDecoder *decoder, uint8_t *nextop, uint64_t *out, const char *opname);
RuntimeDecodeStatus bc_read_i64(const RuntimeDecoder *decoder, uint8_t *nextop, int64_t *out, const char *opname);

bool runtime_decode_status_ok(RuntimeDecodeStatus status);
