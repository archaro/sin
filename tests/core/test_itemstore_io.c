#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "config.h"
#include "item.h"
#include "test_assert.h"

extern CONFIG_t config;

/* Generated fixtures follow docs/itemstore-format.md. */
enum {
  WIRE_ITEM_VALUE = 1,
  WIRE_ITEM_CODE = 2,
  WIRE_VALUE_INT = 0,
  WIRE_VALUE_FLOAT = 1,
  WIRE_VALUE_STRING = 2,
  WIRE_VALUE_NIL = 3,
  WIRE_VALUE_BOOL = 4
};

#define WIRE_VERSION 1u
#define WIRE_MAX_DEPTH 8u
#define WIRE_MAX_CHILDREN 250u
#define WIRE_MAX_STRING_LEN (16u * 1024u * 1024u)
#define WIRE_MAX_BYTECODE_LEN (64u * 1024u * 1024u)

static void put_bytes(FILE *file, const void *bytes, size_t length) {
  ASSERT_EQ_INT((long long)length,
                (long long)fwrite(bytes, 1, length, file));
}

static void put_u8(FILE *file, uint8_t value) {
  put_bytes(file, &value, sizeof(value));
}

static void put_u16_le(FILE *file, uint16_t value) {
  uint8_t bytes[] = {(uint8_t)value, (uint8_t)(value >> 8)};
  put_bytes(file, bytes, sizeof(bytes));
}

static void put_u32_le(FILE *file, uint32_t value) {
  uint8_t bytes[] = {
    (uint8_t)value,
    (uint8_t)(value >> 8),
    (uint8_t)(value >> 16),
    (uint8_t)(value >> 24)
  };
  put_bytes(file, bytes, sizeof(bytes));
}

static void put_u64_le(FILE *file, uint64_t value) {
  uint8_t bytes[8];
  for (size_t i = 0; i < sizeof(bytes); i++) {
    bytes[i] = (uint8_t)(value >> (i * 8));
  }
  put_bytes(file, bytes, sizeof(bytes));
}

static void put_header(FILE *file, uint16_t version) {
  static const uint8_t magic[] = {'S', 'I', 'N', 'I', 'T', 'E', 'M', 0};
  put_bytes(file, magic, sizeof(magic));
  put_u16_le(file, version);
}

static void put_record_prefix(FILE *file, const char *name, uint8_t item_tag) {
  size_t length = strlen(name);
  ASSERT_TRUE(length <= UINT8_MAX);
  put_u8(file, (uint8_t)length);
  put_bytes(file, name, length);
  put_u8(file, item_tag);
}

static void put_nil_record_prefix(FILE *file, const char *name,
                                  uint32_t child_count) {
  put_record_prefix(file, name, WIRE_ITEM_VALUE);
  put_u8(file, WIRE_VALUE_NIL);
  put_u32_le(file, child_count);
}

static FILE *new_fixture(char path[]) {
  int fd = mkstemp(path);
  ASSERT_TRUE(fd >= 0);
  FILE *file = fdopen(fd, "wb");
  ASSERT_NOT_NULL(file);
  return file;
}

static FILE *replace_fixture(const char *path) {
  FILE *file = fopen(path, "wb");
  ASSERT_NOT_NULL(file);
  return file;
}

static void assert_fixture_rejected(FILE *file, const char *path) {
  ASSERT_EQ_INT(0, fclose(file));
  ASSERT_TRUE(load_itemstore(path) == NULL);
}

static void put_nested_nil_record(FILE *file, unsigned depth,
                                  unsigned deepest_depth) {
  char name[8];
  if (depth == 0) {
    memcpy(name, "root", 5);
  } else {
    ASSERT_TRUE(snprintf(name, sizeof(name), "n%u", depth) > 0);
  }
  put_nil_record_prefix(file, name, depth < deepest_depth ? 1u : 0u);
  if (depth < deepest_depth) {
    put_nested_nil_record(file, depth + 1, deepest_depth);
  }
}

void test_itemstore_value_and_code_roundtrip(void) {
  char path[] = "/tmp/sin-itemstore-roundtrip-XXXXXX";
  FILE *file = new_fixture(path);
  ASSERT_EQ_INT(0, fclose(file));

  ITEM_t *root = make_root_item("root");
  ASSERT_NOT_NULL(root);
  ASSERT_NOT_NULL(insert_item(root, "nil",
                              (VALUE_t){.type = VALUE_nil, .i = 0}));
  ASSERT_NOT_NULL(insert_item(root, "bool",
                              (VALUE_t){.type = VALUE_bool, .i = 1}));
  ASSERT_NOT_NULL(insert_item(root, "int",
                              (VALUE_t){.type = VALUE_int,
                                        .i = -123456789}));
  ASSERT_NOT_NULL(insert_item(root, "float",
                              (VALUE_t){.type = VALUE_float,
                                        .f_bits = UINT64_C(0x8000000000000000)}));
  ASSERT_NOT_NULL(insert_item(root, "transient",
                              (VALUE_t){.type = VALUE_nil, .i = 0}));
  char *text = strdup("itemstore-v1");
  ASSERT_NOT_NULL(text);
  ASSERT_NOT_NULL(insert_item(root, "string",
                              (VALUE_t){.type = VALUE_str, .s = text}));

  static const uint8_t expected_bytecode[] = {0x00, 0xff, 0x42, 0x00};
  uint8_t *bytecode = malloc(sizeof(expected_bytecode));
  ASSERT_NOT_NULL(bytecode);
  memcpy(bytecode, expected_bytecode, sizeof(expected_bytecode));
  ASSERT_NOT_NULL(insert_code_item(root, "code", sizeof(expected_bytecode),
                                   bytecode));
  ASSERT_EQ_INT(7, root->ordered_size);
  delete_item(root, "transient");

  static const char *expected_order[] = {
    "nil", "bool", "int", "float", "string", "code"
  };
  ASSERT_EQ_INT(sizeof(expected_order) / sizeof(expected_order[0]),
                root->ordered_size);

  ASSERT_TRUE(save_itemstore(path, root));
  ITEM_t *loaded = load_itemstore(path);
  ASSERT_NOT_NULL(loaded);
  ASSERT_EQ_INT(sizeof(expected_order) / sizeof(expected_order[0]),
                loaded->ordered_size);
  for (size_t i = 0; i < sizeof(expected_order) / sizeof(expected_order[0]);
       i++) {
    ITEM_t *ordered = find_item_by_index(loaded, i);
    ASSERT_NOT_NULL(ordered);
    ASSERT_TRUE(strcmp(expected_order[i], ordered->name) == 0);
  }

  ITEM_t *item = find_item(loaded, "nil");
  ASSERT_NOT_NULL(item);
  ASSERT_EQ_INT(ITEM_value, item->type);
  ASSERT_EQ_INT(VALUE_nil, item->value.type);

  item = find_item(loaded, "bool");
  ASSERT_NOT_NULL(item);
  ASSERT_EQ_INT(VALUE_bool, item->value.type);
  ASSERT_EQ_INT(1, item->value.i);

  item = find_item(loaded, "int");
  ASSERT_NOT_NULL(item);
  ASSERT_EQ_INT(VALUE_int, item->value.type);
  ASSERT_EQ_INT(-123456789, item->value.i);

  item = find_item(loaded, "float");
  ASSERT_NOT_NULL(item);
  ASSERT_EQ_INT(VALUE_float, item->value.type);
  ASSERT_TRUE(item->value.f_bits == UINT64_C(0x8000000000000000));

  item = find_item(loaded, "string");
  ASSERT_NOT_NULL(item);
  ASSERT_EQ_INT(VALUE_str, item->value.type);
  ASSERT_TRUE(strcmp("itemstore-v1", item->value.s) == 0);

  item = find_item(loaded, "code");
  ASSERT_NOT_NULL(item);
  ASSERT_EQ_INT(ITEM_code, item->type);
  ASSERT_EQ_INT(sizeof(expected_bytecode), item->bytecode_len);
  ASSERT_TRUE(memcmp(expected_bytecode, item->bytecode,
                     sizeof(expected_bytecode)) == 0);

  destroy_item(loaded);
  destroy_item(root);
  ASSERT_EQ_INT(0, unlink(path));
}

void test_itemstore_nested_depth_roundtrip(void) {
  char path[] = "/tmp/sin-itemstore-depth-XXXXXX";
  FILE *file = new_fixture(path);
  ASSERT_EQ_INT(0, fclose(file));

  ITEM_t *root = make_root_item("root");
  ASSERT_NOT_NULL(root);
  ASSERT_NOT_NULL(insert_item(root, "a.b.c.d.e.f.g.h",
                              (VALUE_t){.type = VALUE_int, .i = 8}));
  ASSERT_TRUE(save_itemstore(path, root));

  ITEM_t *loaded = load_itemstore(path);
  ASSERT_NOT_NULL(loaded);
  ITEM_t *leaf = find_item(loaded, "a.b.c.d.e.f.g.h");
  ASSERT_NOT_NULL(leaf);
  ASSERT_EQ_INT(VALUE_int, leaf->value.type);
  ASSERT_EQ_INT(8, leaf->value.i);

  destroy_item(loaded);
  destroy_item(root);
  ASSERT_EQ_INT(0, unlink(path));
}

void test_itemstore_loads_generated_v1_wire_fixture(void) {
  char path[] = "/tmp/sin-itemstore-wire-XXXXXX";
  FILE *file = new_fixture(path);
  put_header(file, WIRE_VERSION);
  put_nil_record_prefix(file, "root", 6);

  put_nil_record_prefix(file, "nil", 0);

  put_record_prefix(file, "bool", WIRE_ITEM_VALUE);
  put_u8(file, WIRE_VALUE_BOOL);
  put_u8(file, 1);
  put_u32_le(file, 0);

  put_record_prefix(file, "int", WIRE_ITEM_VALUE);
  put_u8(file, WIRE_VALUE_INT);
  put_u64_le(file, UINT64_C(0x0102030405060708));
  put_u32_le(file, 0);

  put_record_prefix(file, "float", WIRE_ITEM_VALUE);
  put_u8(file, WIRE_VALUE_FLOAT);
  put_u64_le(file, UINT64_C(0x3ff8000000000000));
  put_u32_le(file, 0);

  put_record_prefix(file, "string", WIRE_ITEM_VALUE);
  put_u8(file, WIRE_VALUE_STRING);
  put_u32_le(file, 3);
  put_bytes(file, "sin", 3);
  put_u32_le(file, 0);

  static const uint8_t bytecode[] = {0x10, 0x00, 0x7f};
  put_record_prefix(file, "code", WIRE_ITEM_CODE);
  put_u32_le(file, sizeof(bytecode));
  put_bytes(file, bytecode, sizeof(bytecode));
  put_u32_le(file, 0);
  ASSERT_EQ_INT(0, fclose(file));

  ITEM_t *loaded = load_itemstore(path);
  ASSERT_NOT_NULL(loaded);
  ITEM_t *item = find_item(loaded, "nil");
  ASSERT_NOT_NULL(item);
  ASSERT_EQ_INT(VALUE_nil, item->value.type);
  item = find_item(loaded, "bool");
  ASSERT_NOT_NULL(item);
  ASSERT_EQ_INT(1, item->value.i);
  item = find_item(loaded, "int");
  ASSERT_NOT_NULL(item);
  ASSERT_TRUE(item->value.i == INT64_C(0x0102030405060708));
  item = find_item(loaded, "float");
  ASSERT_NOT_NULL(item);
  ASSERT_TRUE(item->value.f_bits == UINT64_C(0x3ff8000000000000));
  item = find_item(loaded, "string");
  ASSERT_NOT_NULL(item);
  ASSERT_TRUE(strcmp(item->value.s, "sin") == 0);
  item = find_item(loaded, "code");
  ASSERT_NOT_NULL(item);
  ASSERT_EQ_INT(sizeof(bytecode), item->bytecode_len);
  ASSERT_TRUE(memcmp(bytecode, item->bytecode, sizeof(bytecode)) == 0);

  destroy_item(loaded);
  ASSERT_EQ_INT(0, unlink(path));
}

void test_load_itemstore_rejects_bad_headers(void) {
  char path[] = "/tmp/sin-itemstore-header-XXXXXX";
  FILE *file = new_fixture(path);

  static const uint8_t bad_magic[] = {'B', 'A', 'D', 'I', 'T', 'E', 'M', 0};
  put_bytes(file, bad_magic, sizeof(bad_magic));
  put_u16_le(file, WIRE_VERSION);
  assert_fixture_rejected(file, path);

  file = replace_fixture(path);
  put_header(file, WIRE_VERSION + 1);
  assert_fixture_rejected(file, path);

  file = replace_fixture(path);
  put_bytes(file, "SINI", 4);
  assert_fixture_rejected(file, path);

  file = replace_fixture(path);
  put_bytes(file, "SINITEM", 8);
  put_u8(file, WIRE_VERSION);
  assert_fixture_rejected(file, path);

  file = replace_fixture(path);
  put_header(file, WIRE_VERSION);
  put_u8(file, 4);
  put_bytes(file, "root", 4);
  put_u8(file, WIRE_ITEM_VALUE);
  put_u8(file, WIRE_VALUE_INT);
  put_u32_le(file, UINT32_C(0x12345678));
  assert_fixture_rejected(file, path);

  ASSERT_EQ_INT(0, unlink(path));
}

void test_load_itemstore_rejects_invalid_wire_tags(void) {
  char path[] = "/tmp/sin-itemstore-tags-XXXXXX";
  FILE *file = new_fixture(path);
  put_header(file, WIRE_VERSION);
  put_record_prefix(file, "root", 0xff);
  assert_fixture_rejected(file, path);

  file = replace_fixture(path);
  put_header(file, WIRE_VERSION);
  put_record_prefix(file, "root", WIRE_ITEM_VALUE);
  put_u8(file, 0xff);
  assert_fixture_rejected(file, path);

  ASSERT_EQ_INT(0, unlink(path));
}

void test_load_itemstore_rejects_structural_corruption(void) {
  char path[] = "/tmp/sin-itemstore-structure-XXXXXX";
  FILE *file = new_fixture(path);
  put_header(file, WIRE_VERSION);
  put_nil_record_prefix(file, "root", 0);
  put_u8(file, 0xaa);
  assert_fixture_rejected(file, path);

  file = replace_fixture(path);
  put_header(file, WIRE_VERSION);
  put_nil_record_prefix(file, "root", 2);
  put_nil_record_prefix(file, "dup", 0);
  put_nil_record_prefix(file, "dup", 0);
  assert_fixture_rejected(file, path);

  file = replace_fixture(path);
  put_header(file, WIRE_VERSION);
  put_nil_record_prefix(file, "root", 1);
  put_nil_record_prefix(file, "bad-name", 0);
  assert_fixture_rejected(file, path);

  file = replace_fixture(path);
  put_header(file, WIRE_VERSION);
  put_nil_record_prefix(file, "root", 1);
  put_nil_record_prefix(file, "branch", 1);
  assert_fixture_rejected(file, path);

  file = replace_fixture(path);
  put_header(file, WIRE_VERSION);
  put_nil_record_prefix(file, "root", 1);
  put_u8(file, 33);
  assert_fixture_rejected(file, path);

  file = replace_fixture(path);
  put_header(file, WIRE_VERSION);
  put_record_prefix(file, "root", WIRE_ITEM_VALUE);
  put_u8(file, WIRE_VALUE_BOOL);
  put_u8(file, 2);
  put_u32_le(file, 0);
  assert_fixture_rejected(file, path);

  ASSERT_EQ_INT(0, unlink(path));
}

void test_load_itemstore_rejects_resource_limit_violations(void) {
  char path[] = "/tmp/sin-itemstore-limits-XXXXXX";
  FILE *file = new_fixture(path);
  put_header(file, WIRE_VERSION);
  put_record_prefix(file, "root", WIRE_ITEM_VALUE);
  put_u8(file, WIRE_VALUE_STRING);
  put_u32_le(file, WIRE_MAX_STRING_LEN + 1u);
  assert_fixture_rejected(file, path);

  file = replace_fixture(path);
  put_header(file, WIRE_VERSION);
  put_record_prefix(file, "root", WIRE_ITEM_CODE);
  put_u32_le(file, WIRE_MAX_BYTECODE_LEN + 1u);
  assert_fixture_rejected(file, path);

  file = replace_fixture(path);
  put_header(file, WIRE_VERSION);
  put_nil_record_prefix(file, "root", WIRE_MAX_CHILDREN + 1u);
  assert_fixture_rejected(file, path);

  file = replace_fixture(path);
  put_header(file, WIRE_VERSION);
  put_nested_nil_record(file, 0, WIRE_MAX_DEPTH + 1u);
  assert_fixture_rejected(file, path);

  ASSERT_EQ_INT(0, unlink(path));
}

void test_save_itemstore_preserves_existing_file_on_failure(void) {
  char path[] = "/tmp/sin-itemstore-save-test-XXXXXX";
  FILE *file = new_fixture(path);
  ASSERT_EQ_INT(0, fclose(file));

  ITEM_t *original = make_root_item("root");
  ASSERT_NOT_NULL(original);
  ASSERT_NOT_NULL(insert_item(original, "sentinel",
                              (VALUE_t){.type = VALUE_int, .i = 42}));
  ASSERT_TRUE(save_itemstore(path, original));
  destroy_item(original);

  ITEM_t *invalid = make_root_item("root");
  ASSERT_NOT_NULL(invalid);
  ASSERT_NOT_NULL(insert_code_item(invalid, "invalid_code", 1, NULL));
  ASSERT_TRUE(!save_itemstore(path, invalid));
  destroy_item(invalid);

  ITEM_t *too_many_children = make_root_item("root");
  ASSERT_NOT_NULL(too_many_children);
  for (int i = 0; i < 251; i++) {
    char name[16];
    ASSERT_TRUE(snprintf(name, sizeof(name), "child_%03d", i) > 0);
    ASSERT_NOT_NULL(insert_item(
        too_many_children, name, (VALUE_t){.type = VALUE_nil, .i = 0}));
  }
  ASSERT_TRUE(!save_itemstore(path, too_many_children));
  destroy_item(too_many_children);

  ITEM_t *loaded = load_itemstore(path);
  ASSERT_NOT_NULL(loaded);
  ITEM_t *sentinel = find_item(loaded, "sentinel");
  ASSERT_NOT_NULL(sentinel);
  ASSERT_EQ_INT(VALUE_int, sentinel->value.type);
  ASSERT_EQ_INT(42, sentinel->value.i);
  ASSERT_TRUE(find_item(loaded, "invalid_code") == NULL);

  destroy_item(loaded);
  ASSERT_EQ_INT(0, unlink(path));
}

void test_itemstore_durability_modes(void) {
  ASSERT_TRUE(itemstore_durability_requires_sync(ITEMSTORE_DURABLE_FULL));
  ASSERT_TRUE(!itemstore_durability_requires_sync(ITEMSTORE_DURABLE_FAST));

  char path[] = "/tmp/sin-itemstore-durability-XXXXXX";
  FILE *file = new_fixture(path);
  ASSERT_EQ_INT(0, fclose(file));
  ITEM_t *root = make_root_item("root");
  ASSERT_NOT_NULL(root);

  config.itemstore_durability = ITEMSTORE_DURABLE_FAST;
  ASSERT_TRUE(save_itemstore(path, root));
  ITEM_t *loaded = load_itemstore(path);
  ASSERT_NOT_NULL(loaded);
  destroy_item(loaded);

  config.itemstore_durability = ITEMSTORE_DURABLE_FULL;
  ASSERT_TRUE(save_itemstore(path, root));
  loaded = load_itemstore(path);
  ASSERT_NOT_NULL(loaded);
  destroy_item(loaded);

  destroy_item(root);
  ASSERT_EQ_INT(0, unlink(path));
}

void test_itemstore_large_load_presizes_child_storage(void) {
  enum { CHILD_COUNT = 200 };
  char path[] = "/tmp/sin-itemstore-large-load-XXXXXX";
  FILE *file = new_fixture(path);
  put_header(file, WIRE_VERSION);
  put_nil_record_prefix(file, "root", CHILD_COUNT);
  for (int i = 0; i < CHILD_COUNT; i++) {
    char name[16];
    ASSERT_TRUE(snprintf(name, sizeof(name), "child_%03d", i) > 0);
    put_nil_record_prefix(file, name, 0);
  }
  ASSERT_EQ_INT(0, fclose(file));

  ITEM_t *loaded = load_itemstore(path);
  ASSERT_NOT_NULL(loaded);
  ASSERT_EQ_INT(CHILD_COUNT, loaded->children->entry_count);
  ASSERT_EQ_INT(CHILD_COUNT, loaded->ordered_size);
  ASSERT_EQ_INT(CHILD_COUNT, loaded->ordered_capacity);
  ASSERT_EQ_INT(267, loaded->children->size);
  for (int i = 0; i < CHILD_COUNT; i++) {
    ITEM_t *child = find_item_by_index(loaded, (size_t)i);
    ASSERT_NOT_NULL(child);
    char expected[16];
    ASSERT_TRUE(snprintf(expected, sizeof(expected), "child_%03d", i) > 0);
    ASSERT_TRUE(strcmp(expected, child->name) == 0);
  }

  destroy_item(loaded);
  ASSERT_EQ_INT(0, unlink(path));
}
