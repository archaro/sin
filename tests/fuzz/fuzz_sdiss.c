// libFuzzer target for the sdiss bytecode verification and disassembly path.
//
// Licensed under the MIT License - see LICENSE file for details.

#include <stddef.h>
#include <stdint.h>

#include "config.h"
#include "error.h"
#include "sdiss_core.h"

CONFIG_t config;

static const size_t kMaxFuzzBytecodeSize = 64 * 1024;

static void discard_output(void *ctx, const char *data, size_t len) {
  (void)ctx;
  (void)data;
  (void)len;
}

int LLVMFuzzerInitialize(int *argc, char ***argv) {
  (void)argc;
  (void)argv;
  init_errmsg();
  return 0;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  if (!data || size == 0 || size > kMaxFuzzBytecodeSize) {
    return 0;
  }

  SDissOptions options = {0};
  (void)sdiss_disassemble_bytes(data, (uint32_t)size, &options, discard_output, NULL);
  return 0;
}
