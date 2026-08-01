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
#include "item_internal.h"
#include "vm.h"

CONFIG_t config;

static const size_t kMaxFuzzObjectSize = 128 * 1024;
static const size_t kMaxBytecodeProbeSize = 64 * 1024;

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

  bool write_ok = fwrite(data, 1, size, file) == size;
  bool close_ok = fclose(file) == 0;
  if (!write_ok || !close_ok) {
    unlink(path);
    return;
  }

  ITEMSTORE_t *store = itemstore_load(path);
  itemstore_destroy(store);
  unlink(path);
}

static void fuzz_convert_itemstore_bytes(const uint8_t *data, size_t size) {
  char input_path[] = "/tmp/sin-object-fuzz-convert-in-XXXXXX";
  char output_path[] = "/tmp/sin-object-fuzz-convert-out-XXXXXX";
  int input_fd = mkstemp(input_path);
  int output_fd = mkstemp(output_path);
  if (input_fd < 0 || output_fd < 0) {
    if (input_fd >= 0) close(input_fd);
    if (output_fd >= 0) close(output_fd);
    unlink(input_path);
    unlink(output_path);
    return;
  }
  if (close(output_fd) != 0) {
    close(input_fd);
    unlink(input_path);
    unlink(output_path);
    return;
  }
  FILE *file = fdopen(input_fd, "wb");
  if (file != NULL) {
    bool write_ok = fwrite(data, 1, size, file) == size;
    bool close_ok = fclose(file) == 0;
    if (write_ok && close_ok) {
      (void)itemstore_convert(input_path, output_path, ITEMSTORE_DURABLE_FAST,
                              true);
    }
  } else {
    close(input_fd);
  }
  unlink(input_path);
  unlink(output_path);
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
  fuzz_convert_itemstore_bytes(data, size);

  if (size <= kMaxBytecodeProbeSize) {
    BC_VerifyOptions options = bc_verify_strict_options();
    (void)bc_verify_bytecode(data, (uint32_t)size, "raw-fuzz-bytecode",
                             &options);
  }

  return 0;
}
