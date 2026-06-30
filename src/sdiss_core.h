#pragma once

#include <stddef.h>
#include <stdint.h>

#include "bytecode_verify.h"

typedef struct {
  int raw;
  int no_header;
} SDissOptions;

typedef void (*SDissWriteFn)(void *ctx, const char *data, size_t len);

typedef struct {
  BC_VerifyStatus status;
  BC_VerifyError diagnostic;
  int instruction_count;
  int unknown_opcode_count;
  int warning_count;
} SDissResult;

SDissResult sdiss_disassemble_bytes(const uint8_t *bytecode,
                                    uint32_t bytecode_len,
                                    const SDissOptions *options,
                                    SDissWriteFn write_fn,
                                    void *write_ctx);
