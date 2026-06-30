// libFuzzer target for sin's runtime-facing object loading path.
//
// Licensed under the MIT License - see LICENSE file for details.

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "bytecode_verify.h"
#include "config.h"
#include "error.h"
#include "item.h"
#include "vm.h"

CONFIG_t config;

static const size_t kMaxFuzzObjectSize = 128 * 1024;
static const size_t kMaxBytecodeProbeSize = 64 * 1024;

static void verify_loaded_code_items(const ITEM_t *item) {
  if (!item) return;

  if (item->type == ITEM_code && item->bytecode_len <= kMaxBytecodeProbeSize) {
    BC_VerifyOptions options = bc_verify_strict_options();
    (void)bc_verify_bytecode(item->bytecode, item->bytecode_len, item->name,
                             &options);
  }

  for (size_t i = 0; i < item->ordered_size; i++) {
    verify_loaded_code_items(item->ordered_array[i]);
  }
}

static void fuzz_load_itemstore_bytes(const uint8_t *data, size_t size) {
  char path[] = "/tmp/sin-object-fuzz-XXXXXX";
  int fd = mkstemp(path);
  if (fd < 0) return;

  FILE *file = fdopen(fd, "wb");
  if (!file) {
    close(fd);
    unlink(path);
    return;
  }

  if (fwrite(data, 1, size, file) != size || fclose(file) != 0) {
    unlink(path);
    return;
  }

  ITEM_t *loaded = load_itemstore(path);
  if (loaded) {
    verify_loaded_code_items(loaded);
    destroy_item(loaded);
  }
  unlink(path);
}

int LLVMFuzzerInitialize(int *argc, char ***argv) {
  (void)argc;
  (void)argv;
  init_errmsg();
  memset(&config, 0, sizeof(config));
  config.strict_validation = true;
  config.itemstore_durability = ITEMSTORE_DURABLE_FAST;
  return 0;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  if (!data || size == 0 || size > kMaxFuzzObjectSize) {
    return 0;
  }

  config.strict_validation = true;
  fuzz_load_itemstore_bytes(data, size);

  if (size <= kMaxBytecodeProbeSize) {
    BC_VerifyOptions options = bc_verify_strict_options();
    (void)bc_verify_bytecode(data, (uint32_t)size, "raw-fuzz-bytecode",
                             &options);
  }

  return 0;
}
