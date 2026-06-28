#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "config.h"
#include "item.h"
#include "test_assert.h"

extern CONFIG_t config;

static void put_bytes(FILE *file, const void *bytes, size_t length) {
  ASSERT_EQ_INT((long long)length, (long long)fwrite(bytes, 1, length, file));
}

static void put_u8(FILE *file, uint8_t value) { put_bytes(file, &value, sizeof(value)); }

static void put_u16_le(FILE *file, uint16_t value) {
  uint8_t bytes[] = {(uint8_t)value, (uint8_t)(value >> 8)};
  put_bytes(file, bytes, sizeof(bytes));
}

static void put_u32_le(FILE *file, uint32_t value) {
  uint8_t bytes[] = {(uint8_t)value, (uint8_t)(value >> 8),
                     (uint8_t)(value >> 16), (uint8_t)(value >> 24)};
  put_bytes(file, bytes, sizeof(bytes));
}

static void put_header(FILE *file) {
  static const uint8_t magic[] = {'S', 'I', 'N', 'I', 'T', 'E', 'M', 0};
  put_bytes(file, magic, sizeof(magic));
  put_u16_le(file, 1);
}

static void put_record_prefix(FILE *file, const char *name, uint8_t item_tag) {
  size_t length = strlen(name);
  ASSERT_TRUE(length <= UINT8_MAX);
  put_u8(file, (uint8_t)length);
  put_bytes(file, name, length);
  put_u8(file, item_tag);
}

void test_itemstore_verifier_rejects_malformed_code_item_bytecode(void) {
  bool previous_strict_validation = config.strict_validation;
  config.strict_validation = true;

  char path[] = "/tmp/sin-itemstore-verifier-XXXXXX";
  int fd = mkstemp(path);
  ASSERT_TRUE(fd >= 0);
  FILE *file = fdopen(fd, "wb");
  ASSERT_NOT_NULL(file);

  const uint8_t malformed[] = {0, 0, 'l', 3, 0, 'a'};
  put_header(file);
  put_record_prefix(file, "root", 1);
  put_u8(file, 3); /* VALUE_nil */
  put_u32_le(file, 1);
  put_record_prefix(file, "badcode", 2);
  put_u32_le(file, (uint32_t)sizeof(malformed));
  put_bytes(file, malformed, sizeof(malformed));
  put_u32_le(file, 0);
  ASSERT_EQ_INT(0, fclose(file));

  ITEM_t *loaded = load_itemstore(path);
  ASSERT_TRUE(loaded == NULL);

  ASSERT_EQ_INT(0, unlink(path));
  config.strict_validation = previous_strict_validation;
}
