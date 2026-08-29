// libFuzzer target for the scomp compiler frontend and bytecode emitter.
//
// Licensed under the MIT License - see LICENSE file for details.

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#include "compiler/compiler_pipeline.h"
#include "config.h"
#include "compiler/emitbc.h"
#include "error.h"
#include "memory.h"

CONFIG_t config;

static const size_t kMaxFuzzSourceSize = 64 * 1024;

int LLVMFuzzerInitialize(int *argc, char ***argv) {
  (void)argc;
  (void)argv;
  init_errmsg();
  return 0;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  OUTPUT_t *out = NULL;
  CompilerDiagnostic diag;
  compiler_diag_init(&diag);

  if (!data || size == 0 || size > kMaxFuzzSourceSize) {
    return 0;
  }

  (void)compile_source_to_bytecode_diag((const char *)data, size, &out, &diag);

  if (out) {
    if (out->bytecode) {
      free(out->bytecode);
    }
    free(out);
  }
  compiler_diag_reset(&diag);

  return 0;
}
