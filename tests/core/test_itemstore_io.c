#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "item.h"
#include "test_assert.h"

static void put_u8(FILE *file, uint8_t value) {
  ASSERT_EQ_INT(1, fwrite(&value, 1, 1, file));
}

static void put_u16_le(FILE *file, uint16_t value) {
  put_u8(file, (uint8_t)value);
  put_u8(file, (uint8_t)(value >> 8));
}

static void put_u32_le(FILE *file, uint32_t value) {
  put_u16_le(file, (uint16_t)value);
  put_u16_le(file, (uint16_t)(value >> 16));
}

static void put_header(FILE *file, uint16_t version) {
  static const char magic[] = "SINITEM";
  ASSERT_EQ_INT(sizeof(magic), fwrite(magic, 1, sizeof(magic), file));
  put_u16_le(file, version);
}

static FILE *new_record(const char *name, uint8_t item_tag) {
  FILE *file = tmpfile();
  ASSERT_NOT_NULL(file);
  size_t length = strlen(name);
  ASSERT_TRUE(length <= UINT16_MAX);
  put_u16_le(file, (uint16_t)length);
  ASSERT_EQ_INT((long long)length, (long long)fwrite(name, 1, length, file));
  put_u8(file, item_tag);
  return file;
}

static void assert_root_record_rejected(FILE *file) {
  rewind(file);
  ASSERT_TRUE(read_item(file, NULL) == NULL);
  ASSERT_EQ_INT(0, fclose(file));
}

void test_itemstore_record_roundtrip(void) {
  ITEM_t *root = make_root_item("root");
  ASSERT_NOT_NULL(root);

  VALUE_t integer = {.type = VALUE_int, .i = -123456789};
  VALUE_t floating = {.type = VALUE_float,
                      .f_bits = UINT64_C(0x8000000000000000)};
  VALUE_t string = {.type = VALUE_str, .s = strdup("itemstore-v1")};
  VALUE_t boolean = {.type = VALUE_bool, .i = 1};
  ASSERT_NOT_NULL(string.s);
  ASSERT_NOT_NULL(insert_item(root, "integer", integer));
  ASSERT_NOT_NULL(insert_item(root, "floating", floating));
  ASSERT_NOT_NULL(insert_item(root, "string", string));
  ASSERT_NOT_NULL(insert_item(root, "boolean", boolean));

  uint8_t *bytecode = malloc(3);
  ASSERT_NOT_NULL(bytecode);
  bytecode[0] = 0;
  bytecode[1] = 0;
  bytecode[2] = 'h';
  ASSERT_NOT_NULL(insert_code_item(root, "code", 3, bytecode));

  FILE *file = tmpfile();
  ASSERT_NOT_NULL(file);
  ASSERT_TRUE(write_item(file, root));
  rewind(file);

  ITEM_t *loaded = read_item(file, NULL);
  ASSERT_NOT_NULL(loaded);
  ASSERT_EQ_INT(0, fclose(file));

  ITEM_t *loaded_integer = find_item(loaded, "integer");
  ITEM_t *loaded_floating = find_item(loaded, "floating");
  ITEM_t *loaded_string = find_item(loaded, "string");
  ITEM_t *loaded_boolean = find_item(loaded, "boolean");
  ITEM_t *loaded_code = find_item(loaded, "code");
  ASSERT_NOT_NULL(loaded_integer);
  ASSERT_NOT_NULL(loaded_floating);
  ASSERT_NOT_NULL(loaded_string);
  ASSERT_NOT_NULL(loaded_boolean);
  ASSERT_NOT_NULL(loaded_code);
  ASSERT_EQ_INT(-123456789, loaded_integer->value.i);
  ASSERT_TRUE(loaded_floating->value.f_bits == UINT64_C(0x8000000000000000));
  ASSERT_TRUE(strcmp("itemstore-v1", loaded_string->value.s) == 0);
  ASSERT_EQ_INT(1, loaded_boolean->value.i);
  ASSERT_EQ_INT(3, loaded_code->bytecode_len);
  ASSERT_EQ_INT('h', loaded_code->bytecode[2]);

  VALUE_t flush = {.type = VALUE_nil, .i = 0};
  ASSERT_NOT_NULL(insert_item(loaded, "cache_flush", flush));
  destroy_item(loaded);
  destroy_item(root);
}

void test_itemstore_read_rejects_corrupt_records(void) {
  /* Name lengths above the item model's 32-byte layer limit. */
  FILE *file = tmpfile();
  ASSERT_NOT_NULL(file);
  put_u16_le(file, 33);
  assert_root_record_rejected(file);

  /* Unknown item and value wire tags. */
  file = new_record("root", 0xff);
  assert_root_record_rejected(file);

  file = new_record("root", 1);
  put_u8(file, 0xff);
  assert_root_record_rejected(file);

  /* String and bytecode lengths above the configured 16 MiB limits. */
  file = new_record("root", 1);
  put_u8(file, 4);
  put_u32_le(file, (16u * 1024u * 1024u) + 1u);
  assert_root_record_rejected(file);

  file = new_record("root", 2);
  put_u32_le(file, (16u * 1024u * 1024u) + 1u);
  assert_root_record_rejected(file);

  /* Child counts above the in-memory ordered-array limit. */
  file = new_record("root", 1);
  put_u8(file, 1);
  put_u32_le(file, 251);
  assert_root_record_rejected(file);

  /* Duplicate siblings are rejected before the second item is inserted. */
  file = new_record("root", 1);
  put_u8(file, 1);
  put_u32_le(file, 2);
  put_u16_le(file, 3);
  ASSERT_EQ_INT(3, fwrite("dup", 1, 3, file));
  put_u8(file, 1);
  put_u8(file, 1);
  put_u32_le(file, 0);
  put_u16_le(file, 3);
  ASSERT_EQ_INT(3, fwrite("dup", 1, 3, file));
  assert_root_record_rejected(file);

  /* Invalid non-root layer names. */
  ITEM_t *parent = make_root_item("root");
  ASSERT_NOT_NULL(parent);
  file = new_record("bad-name", 1);
  put_u8(file, 1);
  put_u32_le(file, 0);
  rewind(file);
  ASSERT_TRUE(read_item(file, parent) == NULL);
  ASSERT_EQ_INT(0, fclose(file));
  destroy_item(parent);

  /* A child below eight existing layers would exceed the nesting limit. */
  parent = make_root_item("root");
  ASSERT_NOT_NULL(parent);
  ITEM_t *deep = insert_item(parent, "a.b.c.d.e.f.g.h",
                             (VALUE_t){.type = VALUE_nil, .i = 0});
  ASSERT_NOT_NULL(deep);
  file = tmpfile();
  ASSERT_NOT_NULL(file);
  ASSERT_TRUE(read_item(file, deep) == NULL);
  ASSERT_EQ_INT(0, fclose(file));
  destroy_item(parent);
}

void test_load_itemstore_rejects_incomplete_or_trailing_data(void) {
  char path[] = "/tmp/sin-itemstore-test-XXXXXX";
  int fd = mkstemp(path);
  ASSERT_TRUE(fd >= 0);
  FILE *file = fdopen(fd, "wb");
  ASSERT_NOT_NULL(file);

  ITEM_t *root = make_root_item("root");
  ASSERT_NOT_NULL(root);
  put_header(file, 1);
  ASSERT_TRUE(write_item(file, root));
  put_u8(file, 0xaa);
  ASSERT_EQ_INT(0, fclose(file));
  ASSERT_TRUE(load_itemstore(path) == NULL);

  file = fopen(path, "wb");
  ASSERT_NOT_NULL(file);
  ASSERT_EQ_INT(4, fwrite("SINI", 1, 4, file));
  ASSERT_EQ_INT(0, fclose(file));
  ASSERT_TRUE(load_itemstore(path) == NULL);

  file = fopen(path, "wb");
  ASSERT_NOT_NULL(file);
  ASSERT_EQ_INT(8, fwrite("SINITEM", 1, 8, file));
  put_u8(file, 1);
  ASSERT_EQ_INT(0, fclose(file));
  ASSERT_TRUE(load_itemstore(path) == NULL);

  file = fopen(path, "wb");
  ASSERT_NOT_NULL(file);
  ASSERT_EQ_INT(8, fwrite("BADMAGIC", 1, 8, file));
  put_u16_le(file, 1);
  ASSERT_EQ_INT(0, fclose(file));
  ASSERT_TRUE(load_itemstore(path) == NULL);

  file = fopen(path, "wb");
  ASSERT_NOT_NULL(file);
  put_header(file, 2);
  ASSERT_EQ_INT(0, fclose(file));
  ASSERT_TRUE(load_itemstore(path) == NULL);

  save_itemstore(path, root);
  ITEM_t *loaded = load_itemstore(path);
  ASSERT_NOT_NULL(loaded);
  destroy_item(loaded);
  destroy_item(root);
  ASSERT_EQ_INT(0, unlink(path));
}

void test_save_itemstore_preserves_existing_file_on_failure(void) {
  static const char original[] = "existing-itemstore";
  char path[] = "/tmp/sin-itemstore-save-test-XXXXXX";
  int fd = mkstemp(path);
  ASSERT_TRUE(fd >= 0);
  FILE *file = fdopen(fd, "wb");
  ASSERT_NOT_NULL(file);
  ASSERT_EQ_INT(sizeof(original), fwrite(original, 1, sizeof(original), file));
  ASSERT_EQ_INT(0, fclose(file));

  ITEM_t *root = make_root_item("root");
  ASSERT_NOT_NULL(root);
  VALUE_t invalid_bool = {.type = VALUE_bool, .i = 2};
  ASSERT_NOT_NULL(insert_item(root, "invalid_bool", invalid_bool));
  save_itemstore(path, root);

  file = fopen(path, "rb");
  ASSERT_NOT_NULL(file);
  char actual[sizeof(original)];
  ASSERT_EQ_INT(sizeof(actual), fread(actual, 1, sizeof(actual), file));
  ASSERT_TRUE(memcmp(original, actual, sizeof(original)) == 0);
  ASSERT_EQ_INT(EOF, fgetc(file));
  ASSERT_EQ_INT(0, fclose(file));

  destroy_item(root);
  ASSERT_EQ_INT(0, unlink(path));
}
