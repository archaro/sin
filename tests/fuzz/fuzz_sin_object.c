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

static int fuzz_temp_template(char *path, size_t path_size,
                              const char *prefix) {
  const char *root = getenv("SIN_TEST_TMP_ROOT");
  int written;
  if (!root || !root[0]) root = "/tmp";
  written = snprintf(path, path_size, "%s/%s-XXXXXX", root, prefix);
  return written < 0 || (size_t)written >= path_size ? -1 : 0;
}

static void fuzz_load_itemstore_bytes(const uint8_t *data, size_t size) {
  char path[4096];
  if (fuzz_temp_template(path, sizeof path, "sin-object-fuzz") != 0) return;
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
  char input_path[4096], output_path[4096];
  if (fuzz_temp_template(input_path, sizeof input_path,
                         "sin-object-fuzz-convert-in") != 0 ||
      fuzz_temp_template(output_path, sizeof output_path,
                         "sin-object-fuzz-convert-out") != 0) return;
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
    (void)bc_verify_executable_bytecode(data, (uint32_t)size,
                                        "raw-fuzz-bytecode");
  }

  return 0;
}
