#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef enum {
  BC_FORMAT_OK = 0,
  BC_FORMAT_INVALID,
  BC_FORMAT_TRUNCATED,
  BC_FORMAT_UNSUPPORTED_VERSION
} BC_FormatStatus;

#define BC_V1_RESERVED_LOCALS 0x00u
#define BC_V1_RESERVED_PARAMS 0xffu
#define BC_V1_MAGIC_S 0x53u
#define BC_V1_MAGIC_B 0x42u
#define BC_V1_VERSION 1u
#define BC_V1_HEADER_SIZE 8u

typedef struct {
  BC_FormatStatus status;
  uint16_t version;
  uint8_t locals;
  uint8_t params;
  uint32_t instruction_offset;
  const uint8_t *instructions;
  bool legacy;
} BC_FormatHeader;

BC_FormatStatus bc_decode_header(const uint8_t *bytecode, uint32_t length,
                                 BC_FormatHeader *out);
void bc_encode_v1_header(uint8_t header[BC_V1_HEADER_SIZE], uint8_t locals,
                         uint8_t params);
