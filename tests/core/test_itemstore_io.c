#include <stdint.h>
#include "test_helpers.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <glob.h>
#include <unistd.h>

#include "config.h"
#include "item.h"
#include "item_internal.h"
#include "item_persist_internal.h"
#include "runtime_item_ops.h"
#include "list.h"
#include "itemref.h"
#define get_itemstore_topology_revision() itemstore_topology_revision(itemstore_owner(root))
#define get_itemstore_payload_revision() itemstore_payload_revision(itemstore_owner(root))
#include "string_limits.h"
#include "test_assert.h"
#include "memory.h"

extern CONFIG_t config;

void test_item_value_accessor_rejects_code_items(void) {
  ITEM_t *root = make_root_item("root");
  ASSERT_NOT_NULL(root);
  uint8_t inactive_bytecode = 0xA5;
  root->bytecode = &inactive_bytecode;
  root->bytecode_len = UINT32_MAX;
  ASSERT_TRUE(item_bytecode(root) == NULL);
  ASSERT_EQ_INT(0, item_bytecode_length(root));

  uint8_t *bytecode = malloc(1);
  ASSERT_NOT_NULL(bytecode);
  bytecode[0] = 0;
  ITEM_t *code = make_item("code", root, ITEM_code, VALUE_NIL, bytecode, 1);
  ASSERT_NOT_NULL(code);
  ASSERT_EQ_INT(ITEM_code, item_kind(code));
  code->value = (VALUE_t){.type = VALUE_int, .i = 42};
  ASSERT_TRUE(item_value(code) == NULL);
  ASSERT_TRUE(item_bytecode(code) == bytecode);
  ASSERT_EQ_INT(1, item_bytecode_length(code));
  ASSERT_TRUE(item_value(NULL) == NULL);
  ASSERT_TRUE(item_bytecode(NULL) == NULL);
  ASSERT_EQ_INT(0, item_bytecode_length(NULL));
  destroy_item(root);
}

/* Generated fixtures follow docs/itemstore-format.md. */
enum {
  WIRE_ITEM_VALUE = 1,
  WIRE_ITEM_CODE = 2,
  WIRE_VALUE_INT = 0,
  WIRE_VALUE_FLOAT = 1,
  WIRE_VALUE_STRING = 2,
  WIRE_VALUE_NIL = 3,
  WIRE_VALUE_BOOL = 4,
  WIRE_VALUE_LIST = 5,
  WIRE_VALUE_ITEMREF = 6
};

#define WIRE_VERSION 2u
#define WIRE_MAX_DEPTH ITEM_MAX_DEPTH
#define WIRE_MAX_CHILDREN 250u
#define WIRE_MAX_BYTECODE_LEN (64u * 1024u * 1024u)

void test_get_itemname_root_item(void) {
  ITEM_t *root = make_root_item("root");
  char itemname[MAX_ITEM_NAME] = {0};

  ASSERT_NOT_NULL(root);
  get_itemname(root, itemname);
  ASSERT_TRUE(strcmp(itemname, "root") == 0);

  destroy_item(root);
}

void test_loaded_zero_child_item_can_gain_runtime_child(void) {
  ITEM_t *root = make_root_item("root");
  ASSERT_NOT_NULL(root);

  uint8_t *bytecode = malloc(3);
  ASSERT_NOT_NULL(bytecode);
  bytecode[0] = 0;
  bytecode[1] = 0;
  bytecode[2] = 'h';

  ITEM_t *input = make_loaded_item("input", root, ITEM_code,
                                   (VALUE_t){.type = VALUE_nil, .i = 0},
                                   bytecode, 3, 0);
  ASSERT_NOT_NULL(input);
  ASSERT_EQ_INT(0, item_child_count(input));
  ASSERT_EQ_INT(0, item_children_ordered_capacity(input->children));
  ASSERT_TRUE(item_child_at(input, 0) == NULL);

  ITEM_t *line_item = test_item_set_value(root, "input.line",
                                          (VALUE_t){.type = VALUE_int, .i = 7});
  ASSERT_NOT_NULL(line_item);
  ASSERT_EQ_INT(VALUE_int, line_item->value.type);
  ASSERT_EQ_INT(7, line_item->value.i);
  ASSERT_EQ_INT(1, item_child_count(input));
  ASSERT_TRUE(item_children_ordered_capacity(input->children) >=
              item_child_count(input));
  ASSERT_TRUE(item_child_at(input, 0) == line_item);

  destroy_item(root);
}

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

static void put_string_record_prefix(FILE *file, const char *name,
                                     const char *value,
                                     uint32_t child_count) {
  size_t length = strlen(value);
  ASSERT_TRUE(length <= UINT32_MAX);
  put_record_prefix(file, name, WIRE_ITEM_VALUE);
  put_u8(file, WIRE_VALUE_STRING);
  put_u32_le(file, (uint32_t)length);
  put_bytes(file, value, length);
  put_u32_le(file, child_count);
}

static void put_code_record_prefix(FILE *file, const char *name,
                                   const uint8_t *bytecode,
                                   size_t bytecode_len,
                                   uint32_t child_count) {
  ASSERT_TRUE(bytecode_len <= UINT32_MAX);
  put_record_prefix(file, name, WIRE_ITEM_CODE);
  put_u32_le(file, (uint32_t)bytecode_len);
  put_bytes(file, bytecode, bytecode_len);
  put_u32_le(file, child_count);
}

static void put_value_record_prefix(FILE *file, const char *name, uint8_t tag,
                                    uint32_t child_count) {
  put_record_prefix(file, name, WIRE_ITEM_VALUE);
  put_u8(file, tag);
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

static void build_max_item_path(char path[MAX_ITEM_NAME]) {
  size_t length = 0;
  for (size_t layer = 0; layer < ITEM_MAX_DEPTH; layer++) {
    if (layer > 0) path[length++] = '.';
    memset(path + length, (int)('a' + layer),
           ITEM_MAX_LAYER_NAME_LENGTH);
    length += ITEM_MAX_LAYER_NAME_LENGTH;
  }
  path[length] = '\0';
  ASSERT_EQ_INT(ITEM_MAX_FULL_NAME_LENGTH, length);
}

static int sync_hook_calls;
static int directory_sync_hook_calls;
static int constructor_attempts;
static char constructor_names[4][ITEM_MAX_LAYER_NAME_LENGTH + 1u];
static int source_write_hook_calls;
static int source_close_hook_calls;

static bool fail_item_creation(const char *name) {
  return strcmp(name, "fail") == 0;
}

static bool fail_loaded_item_constructor(const char *name) {
  if (constructor_attempts < (int)(sizeof constructor_names /
                                   sizeof constructor_names[0])) {
    snprintf(constructor_names[constructor_attempts],
             sizeof constructor_names[constructor_attempts], "%s", name);
  }
  constructor_attempts++;
  return strcmp(name, "bad") == 0 || strcmp(name, "bad_code") == 0 ||
         strcmp(name, "bad_ref") == 0;
}

static int fail_source_write(const char *source, FILE *file) {
  (void)source;
  (void)file;
  source_write_hook_calls++;
  return EOF;
}

static int count_source_close(FILE *file) {
  source_close_hook_calls++;
  return fclose(file);
}

static int fail_source_close(FILE *file) {
  int result = fclose(file);
  source_close_hook_calls++;
  return result == 0 ? EOF : result;
}

static bool count_sync(FILE *file, const char *path) {
  (void)file;
  (void)path;
  sync_hook_calls++;
  return true;
}

static bool reject_directory_sync(const char *path) {
  (void)path;
  directory_sync_hook_calls++;
  return false;
}

static uint8_t *copy_bytecode(const uint8_t *bytecode, size_t bytecode_len) {
  uint8_t *copy = malloc(bytecode_len);
  ASSERT_NOT_NULL(copy);
  memcpy(copy, bytecode, bytecode_len);
  return copy;
}

static void assert_child_order(ITEM_t *parent, const char *const *names,
                               size_t count) {
  ASSERT_EQ_INT(count, item_child_count(parent));
  for (size_t i = 0; i < count; i++) {
    ITEM_t *child = item_child_at(parent, i);
    ASSERT_NOT_NULL(child);
    ASSERT_TRUE(strcmp(names[i], child->name) == 0);
  }
}

static void assert_int_item(ITEM_t *root, const char *name, int64_t expected) {
  ITEM_t *item = find_item(root, name);
  ASSERT_NOT_NULL(item);
  ASSERT_EQ_INT(ITEM_value, item_kind(item));
  ASSERT_EQ_INT(VALUE_int, item->value.type);
  ASSERT_EQ_INT(expected, item->value.i);
}

static void assert_code_item(ITEM_t *root, const char *name,
                             const uint8_t *expected, size_t expected_len) {
  ITEM_t *item = find_item(root, name);
  ASSERT_NOT_NULL(item);
  ASSERT_EQ_INT(ITEM_code, item_kind(item));
  ASSERT_EQ_INT(expected_len, item->bytecode_len);
  ASSERT_TRUE(memcmp(expected, item->bytecode, expected_len) == 0);
}

void test_itemstore_value_and_code_roundtrip(void) {
  char path[] = "/tmp/sin-itemstore-roundtrip-XXXXXX";
  FILE *file = new_fixture(path);
  ASSERT_EQ_INT(0, fclose(file));

  ITEM_t *root = make_root_item("root");
  ASSERT_NOT_NULL(root);
  ASSERT_NOT_NULL(test_item_set_value(root, "nil",
                              (VALUE_t){.type = VALUE_nil, .i = 0}));
  ASSERT_NOT_NULL(test_item_set_value(root, "bool",
                              (VALUE_t){.type = VALUE_bool, .i = 1}));
  ASSERT_NOT_NULL(test_item_set_value(root, "int",
                              (VALUE_t){.type = VALUE_int,
                                        .i = -123456789}));
  ASSERT_NOT_NULL(test_item_set_value(root, "float",
                              (VALUE_t){.type = VALUE_float,
                                        .f_bits = UINT64_C(0x8000000000000000)}));
  ASSERT_NOT_NULL(test_item_set_value(root, "transient",
                              (VALUE_t){.type = VALUE_nil, .i = 0}));
  char *text = strdup("itemstore-v1");
  ASSERT_NOT_NULL(text);
  ASSERT_NOT_NULL(test_item_set_value(root, "string",
                              (VALUE_t){.type = VALUE_str, .s = text}));

  static const uint8_t expected_bytecode[] = {0x00, 0x00, 'h'};
  uint8_t *bytecode = malloc(sizeof(expected_bytecode));
  ASSERT_NOT_NULL(bytecode);
  memcpy(bytecode, expected_bytecode, sizeof(expected_bytecode));
  ASSERT_NOT_NULL(test_item_set_code(root, "code", sizeof(expected_bytecode),
                                   bytecode));
  ASSERT_EQ_INT(7, item_child_count(root));
  test_item_delete(root, "transient");

  static const char *expected_order[] = {
    "nil", "bool", "int", "float", "string", "code"
  };
  ASSERT_EQ_INT(sizeof(expected_order) / sizeof(expected_order[0]),
                item_child_count(root));

  ASSERT_TRUE(save_itemstore(path, root));
  ITEM_t *loaded = load_itemstore(path);
  ASSERT_NOT_NULL(loaded);
  ASSERT_EQ_INT(sizeof(expected_order) / sizeof(expected_order[0]),
                item_child_count(loaded));
  for (size_t i = 0; i < sizeof(expected_order) / sizeof(expected_order[0]);
       i++) {
    ITEM_t *ordered = item_child_at(loaded, i);
    ASSERT_NOT_NULL(ordered);
    ASSERT_TRUE(strcmp(expected_order[i], ordered->name) == 0);
  }

  ITEM_t *item = find_item(loaded, "nil");
  ASSERT_NOT_NULL(item);
  ASSERT_EQ_INT(ITEM_value, item_kind(item));
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
  ASSERT_EQ_INT(ITEM_code, item_kind(item));
  ASSERT_EQ_INT(sizeof(expected_bytecode), item->bytecode_len);
  ASSERT_TRUE(memcmp(expected_bytecode, item->bytecode,
                     sizeof(expected_bytecode)) == 0);

  destroy_item(loaded);
  destroy_item(root);
  ASSERT_EQ_INT(0, unlink(path));
}

void test_itemstore_v2_lists_and_itemrefs_roundtrip(void) {
  char path[] = "/tmp/sin-itemstore-v2-XXXXXX";
  int fd = mkstemp(path);
  ASSERT_TRUE(fd >= 0);
  ASSERT_EQ_INT(0, close(fd));
  ITEM_t *root = make_root_item("root");
  ASSERT_NOT_NULL(root);
  VALUE_t nested_elems[1] = {{VALUE_int, {.i = 7}}};
  SIN_LIST_t *nested = sin_list_build_owned(nested_elems, 1);
  ASSERT_NOT_NULL(nested);
  VALUE_t outer_elems[2] = {{VALUE_list, {.list = nested}},
                            {VALUE_list, {.list = sin_list_retain(nested)}}};
  SIN_LIST_t *outer = sin_list_build_owned(outer_elems, 2);
  ASSERT_NOT_NULL(outer);
  ASSERT_NOT_NULL(test_item_set_value(root, "list",
                                      (VALUE_t){VALUE_list, {.list = outer}}));
  SIN_ITEMREF_t *ref = sin_itemref_create("foo.bar");
  ASSERT_NOT_NULL(ref);
  ASSERT_NOT_NULL(test_item_set_value(
      root, "ref", (VALUE_t){VALUE_itemref, {.itemref = ref}}));
  ASSERT_TRUE(save_itemstore(path, root));
  FILE *wire = fopen(path, "rb");
  ASSERT_NOT_NULL(wire);
  uint8_t header[10];
  ASSERT_EQ_INT(10, fread(header, 1, sizeof header, wire));
  static const uint8_t expected_header[10] = {'S', 'I', 'N', 'I', 'T',
                                              'E', 'M', 0,   2,   0};
  ASSERT_TRUE(memcmp(header, expected_header, sizeof header) == 0);
  ASSERT_EQ_INT(0, fclose(wire));
  destroy_item(root);
  root = load_itemstore(path);
  ASSERT_NOT_NULL(root);
  ITEM_t *li = find_item(root, "list");
  ASSERT_NOT_NULL(li);
  ASSERT_EQ_INT(VALUE_list, li->value.type);
  ASSERT_EQ_INT(2, sin_list_count(li->value.list));
  ASSERT_EQ_INT(VALUE_list, sin_list_get(li->value.list, 0)->type);
  ASSERT_EQ_INT(VALUE_list, sin_list_get(li->value.list, 1)->type);
  ASSERT_EQ_INT(1, sin_list_count(sin_list_get(li->value.list, 0)->list));
  ASSERT_EQ_INT(7, sin_list_get(sin_list_get(li->value.list, 0)->list, 0)->i);
  ASSERT_EQ_INT(7, sin_list_get(sin_list_get(li->value.list, 1)->list, 0)->i);
  ASSERT_TRUE(sin_list_equal(sin_list_get(li->value.list, 0)->list,
                             sin_list_get(li->value.list, 1)->list));
  ITEM_t *ri = find_item(root, "ref");
  ASSERT_NOT_NULL(ri);
  ASSERT_EQ_INT(VALUE_itemref, ri->value.type);
  ASSERT_TRUE(strcmp("foo.bar", sin_itemref_path(ri->value.itemref)) == 0);
  destroy_item(root);
  ASSERT_EQ_INT(0, unlink(path));
}

void test_itemstore_v2_all_values_fixture(void) {
  char path[] = "/tmp/sin-itemstore-v2-values-XXXXXX";
  FILE *file = new_fixture(path);
  static const uint8_t code[] = {0x01, 0x02, 0x03};
  put_header(file, 2);
  put_nil_record_prefix(file, "root", 7);
  put_value_record_prefix(file, "nil", WIRE_VALUE_NIL, 0);
  put_record_prefix(file, "bool", WIRE_ITEM_VALUE);
  put_u8(file, WIRE_VALUE_BOOL);
  put_u8(file, 1);
  put_u32_le(file, 0);
  put_record_prefix(file, "int", WIRE_ITEM_VALUE);
  put_u8(file, WIRE_VALUE_INT);
  put_u64_le(file, 42);
  put_u32_le(file, 0);
  put_record_prefix(file, "float", WIRE_ITEM_VALUE);
  put_u8(file, WIRE_VALUE_FLOAT);
  put_u64_le(file, UINT64_C(0x3ff0000000000000));
  put_u32_le(file, 0);
  put_string_record_prefix(file, "str", "hello", 0);
  put_code_record_prefix(file, "code", code, sizeof code, 0);
  put_record_prefix(file, "list", WIRE_ITEM_VALUE);
  put_u8(file, 5);
  put_u32_le(file, 2);
  put_u8(file, WIRE_VALUE_INT);
  put_u64_le(file, 7);
  put_u8(file, WIRE_VALUE_LIST);
  put_u32_le(file, 1);
  put_u8(file, WIRE_VALUE_ITEMREF);
  put_u16_le(file, 7);
  put_bytes(file, "foo.bar", 7);
  put_u32_le(file, 0);
  ASSERT_EQ_INT(0, fclose(file));
  ITEM_t *root = load_itemstore(path);
  ASSERT_NOT_NULL(root);
  ASSERT_EQ_INT(VALUE_nil, find_item(root, "nil")->value.type);
  ASSERT_EQ_INT(1, find_item(root, "bool")->value.i);
  ASSERT_EQ_INT(42, find_item(root, "int")->value.i);
  ASSERT_EQ_INT(VALUE_float, find_item(root, "float")->value.type);
  ASSERT_TRUE(find_item(root, "float")->value.f_bits ==
              UINT64_C(0x3ff0000000000000));
  ASSERT_TRUE(strcmp(find_item(root, "str")->value.s, "hello") == 0);
  ASSERT_EQ_INT(ITEM_code, find_item(root, "code")->type);
  ASSERT_EQ_INT(sizeof code, find_item(root, "code")->bytecode_len);
  ASSERT_TRUE(memcmp(code, find_item(root, "code")->bytecode, sizeof code) ==
              0);
  ASSERT_EQ_INT(2, sin_list_count(find_item(root, "list")->value.list));
  ASSERT_EQ_INT(VALUE_int,
                sin_list_get(find_item(root, "list")->value.list, 0)->type);
  ASSERT_EQ_INT(7, sin_list_get(find_item(root, "list")->value.list, 0)->i);
  ASSERT_EQ_INT(VALUE_list,
                sin_list_get(find_item(root, "list")->value.list, 1)->type);
  ASSERT_EQ_INT(
      1, sin_list_count(
             sin_list_get(find_item(root, "list")->value.list, 1)->list));
  ASSERT_EQ_INT(
      VALUE_itemref,
      sin_list_get(sin_list_get(find_item(root, "list")->value.list, 1)->list,
                   0)
          ->type);
  ASSERT_TRUE(
      strcmp(
          "foo.bar",
          sin_itemref_path(
              sin_list_get(
                  sin_list_get(find_item(root, "list")->value.list, 1)->list, 0)
          ->itemref)) == 0);
  char roundtrip_path[] = "/tmp/sin-itemstore-v2-values-roundtrip-XXXXXX";
  FILE *roundtrip_file = new_fixture(roundtrip_path);
  ASSERT_EQ_INT(0, fclose(roundtrip_file));
  ASSERT_TRUE(save_itemstore(roundtrip_path, root));
  FILE *original_file = fopen(path, "rb");
  roundtrip_file = fopen(roundtrip_path, "rb");
  ASSERT_NOT_NULL(original_file);
  ASSERT_NOT_NULL(roundtrip_file);
  ASSERT_EQ_INT(0, fseek(original_file, 0, SEEK_END));
  ASSERT_EQ_INT(0, fseek(roundtrip_file, 0, SEEK_END));
  long original_size = ftell(original_file);
  long roundtrip_size = ftell(roundtrip_file);
  ASSERT_TRUE(original_size >= 0 && roundtrip_size == original_size);
  ASSERT_EQ_INT(0, fseek(original_file, 0, SEEK_SET));
  ASSERT_EQ_INT(0, fseek(roundtrip_file, 0, SEEK_SET));
  uint8_t *original_bytes = malloc((size_t)original_size);
  uint8_t *roundtrip_bytes = malloc((size_t)roundtrip_size);
  ASSERT_NOT_NULL(original_bytes);
  ASSERT_NOT_NULL(roundtrip_bytes);
  ASSERT_EQ_INT(original_size,
                (long)fread(original_bytes, 1, (size_t)original_size,
                             original_file));
  ASSERT_EQ_INT(roundtrip_size,
                (long)fread(roundtrip_bytes, 1, (size_t)roundtrip_size,
                             roundtrip_file));
  ASSERT_TRUE(memcmp(original_bytes, roundtrip_bytes,
                     (size_t)original_size) == 0);
  free(original_bytes);
  free(roundtrip_bytes);
  ASSERT_EQ_INT(0, fclose(original_file));
  ASSERT_EQ_INT(0, fclose(roundtrip_file));
  destroy_item(root);
  ASSERT_EQ_INT(0, unlink(path));
  ASSERT_EQ_INT(0, unlink(roundtrip_path));
}

void test_itemstore_v2_malformed_value_table(void) {
  enum malformed_case {
    BAD_UNKNOWN,
    BAD_REF_ZERO,
    BAD_REF_LEADING,
    BAD_REF_TRAILING,
    BAD_REF_EMPTY,
    BAD_REF_CHAR,
    BAD_REF_DEPTH,
    BAD_REF_NUL,
    BAD_REF_TRUNC_LEN,
    BAD_REF_TRUNC_PATH,
    BAD_LIST_COUNT,
    BAD_LIST_TAG,
    BAD_LIST_INT,
    BAD_LIST_NESTING
  };
  static const enum malformed_case cases[] = {
      BAD_UNKNOWN,       BAD_REF_ZERO,       BAD_REF_LEADING, BAD_REF_TRAILING,
      BAD_REF_EMPTY,     BAD_REF_CHAR,       BAD_REF_DEPTH,   BAD_REF_NUL,
      BAD_REF_TRUNC_LEN, BAD_REF_TRUNC_PATH, BAD_LIST_COUNT,  BAD_LIST_TAG,
      BAD_LIST_INT,      BAD_LIST_NESTING};
  for (size_t i = 0; i < sizeof cases / sizeof cases[0]; i++) {
    char path[] = "/tmp/sin-itemstore-v2-bad-XXXXXX";
    FILE *file = new_fixture(path);
    put_header(file, 2);
    put_record_prefix(file, "root", WIRE_ITEM_VALUE);
    if (cases[i] == BAD_UNKNOWN) {
      put_u8(file, 9);
      put_u32_le(file, 0);
    } else if (cases[i] >= BAD_REF_ZERO && cases[i] <= BAD_REF_TRUNC_PATH) {
      put_u8(file, WIRE_VALUE_ITEMREF);
      if (cases[i] == BAD_REF_TRUNC_LEN) {
        put_u8(file, 1);
      } else if (cases[i] == BAD_REF_TRUNC_PATH) {
        put_u16_le(file, 7);
        put_bytes(file, "foo", 3);
      } else if (cases[i] == BAD_REF_ZERO) {
        put_u16_le(file, 0);
      } else if (cases[i] == BAD_REF_LEADING) {
        put_u16_le(file, 4);
        put_bytes(file, ".foo", 4);
      } else if (cases[i] == BAD_REF_TRAILING) {
        put_u16_le(file, 4);
        put_bytes(file, "foo.", 4);
      } else if (cases[i] == BAD_REF_EMPTY) {
        put_u16_le(file, 8);
        put_bytes(file, "foo..bar", 8);
      } else if (cases[i] == BAD_REF_CHAR) {
        put_u16_le(file, 5);
        put_bytes(file, "foo-$", 5);
      } else if (cases[i] == BAD_REF_DEPTH) {
        put_u16_le(file, 17);
        put_bytes(file, "a.b.c.d.e.f.g.h.i", 17);
      } else {
        put_u16_le(file, 3);
        put_bytes(file, "a\0b", 3);
      }
    } else {
      put_u8(file, WIRE_VALUE_LIST);
      if (cases[i] == BAD_LIST_COUNT) {
        put_u8(file, 1);
      } else if (cases[i] == BAD_LIST_TAG) {
        put_u32_le(file, 1);
      } else if (cases[i] == BAD_LIST_INT) {
        put_u32_le(file, 1);
        put_u8(file, WIRE_VALUE_INT);
        put_u32_le(file, 1);
      } else {
        put_u32_le(file, 1);
        for (size_t depth = 0; depth < 65; depth++) {
          put_u8(file, WIRE_VALUE_LIST);
          put_u32_le(file, 1);
        }
        put_u8(file, WIRE_VALUE_NIL);
      }
    }
    if (cases[i] != BAD_REF_TRUNC_LEN && cases[i] != BAD_REF_TRUNC_PATH &&
        cases[i] != BAD_LIST_COUNT && cases[i] != BAD_LIST_TAG &&
        cases[i] != BAD_LIST_INT && cases[i] != BAD_LIST_NESTING) {
      put_u32_le(file, 0);
    }
    ASSERT_EQ_INT(0, fclose(file));
    ASSERT_TRUE(load_itemstore(path) == NULL);
    ASSERT_EQ_INT(0, unlink(path));
  }
  {
    char path[] = "/tmp/sin-itemstore-v2-list-count-XXXXXX";
    FILE *file = new_fixture(path);
    put_header(file, 2);
    put_record_prefix(file, "root", WIRE_ITEM_VALUE);
    put_u8(file, WIRE_VALUE_LIST);
    put_u32_le(file, SIN_LIST_MAX_ELEMENTS + 1u);
    ASSERT_EQ_INT(0, fclose(file));
    ASSERT_TRUE(load_itemstore(path) == NULL);
    ASSERT_EQ_INT(0, unlink(path));
  }
  char path[] = "/tmp/sin-itemstore-v2-trailing-XXXXXX";
  FILE *file = new_fixture(path);
  put_header(file, 2);
  put_nil_record_prefix(file, "root", 0);
  put_u8(file, 0xff);
  ASSERT_EQ_INT(0, fclose(file));
  ASSERT_TRUE(load_itemstore(path) == NULL);
  ASSERT_EQ_INT(0, unlink(path));
}

void test_itemstore_v2_budget_and_malformed_save(void) {
  char path[] = "/tmp/sin-itemstore-v2-budget-XXXXXX";
  FILE *file = new_fixture(path);
  put_header(file, 2);
  put_record_prefix(file, "root", WIRE_ITEM_VALUE);
  put_u8(file, 5);
  put_u32_le(file, 2);
  put_u8(file, WIRE_VALUE_NIL);
  put_u8(file, WIRE_VALUE_NIL);
  put_u32_le(file, 0);
  ASSERT_EQ_INT(0, fclose(file));
  file = fopen(path, "rb");
  ASSERT_NOT_NULL(file);
  uint8_t header[10];
  ASSERT_EQ_INT(10, fread(header, 1, 10, file));
  ITEMSTORE_READ_CTX_t ctx = itemstore_read_context(path, 0);
  ctx.aggregate_budget = 1;
  ASSERT_TRUE(itemstore_read_record_for_version(2, file, NULL, &ctx) == NULL);
  ASSERT_EQ_INT(0, fclose(file));
  ASSERT_EQ_INT(0, unlink(path));
  char badpath[] = "/tmp/sin-itemstore-v2-save-XXXXXX";
  file = new_fixture(badpath);
  static const uint8_t sentinel[] = {'k', 'e', 'e', 'p'};
  put_bytes(file, sentinel, sizeof sentinel);
  ASSERT_EQ_INT(0, fclose(file));
  ITEM_t *root = make_root_item("root");
  ASSERT_NOT_NULL(root);
  ASSERT_NOT_NULL(
      test_item_set_value(root, "bad", (VALUE_t){VALUE_list, {.list = NULL}}));
  ASSERT_TRUE(!save_itemstore(badpath, root));
  destroy_item(root);
  file = fopen(badpath, "rb");
  ASSERT_NOT_NULL(file);
  uint8_t actual[sizeof sentinel];
  ASSERT_EQ_INT(sizeof actual, fread(actual, 1, sizeof actual, file));
  ASSERT_TRUE(memcmp(actual, sentinel, sizeof actual) == 0);
  ASSERT_EQ_INT(0, fclose(file));
  ASSERT_EQ_INT(0, unlink(badpath));

  char aggregate_path[] = "/tmp/sin-itemstore-v2-aggregate-XXXXXX";
  file = new_fixture(aggregate_path);
  static const uint8_t aggregate_sentinel[] = {'a', 'g', 'g'};
  put_bytes(file, aggregate_sentinel, sizeof aggregate_sentinel);
  ASSERT_EQ_INT(0, fclose(file));
  root = make_root_item("root");
  ASSERT_NOT_NULL(root);
  const size_t repeated_count = 600000u;
  VALUE_t *elements = calloc(repeated_count, sizeof *elements);
  ASSERT_NOT_NULL(elements);
  for (size_t i = 0; i < repeated_count; ++i) {
    elements[i] = (VALUE_t){VALUE_nil, {.i = 0}};
  }
  SIN_LIST_t *repeated = sin_list_build_owned(elements, repeated_count);
  free(elements);
  ASSERT_NOT_NULL(repeated);
  ASSERT_NOT_NULL(test_item_set_value(
      root, "first", (VALUE_t){VALUE_list, {.list = sin_list_retain(repeated)}}));
  ASSERT_NOT_NULL(test_item_set_value(
      root, "second", (VALUE_t){VALUE_list, {.list = sin_list_retain(repeated)}}));
  sin_list_release(repeated);
  ASSERT_TRUE(!save_itemstore(aggregate_path, root));
  destroy_item(root);
  file = fopen(aggregate_path, "rb");
  ASSERT_NOT_NULL(file);
  uint8_t aggregate_actual[sizeof aggregate_sentinel];
  ASSERT_EQ_INT(sizeof aggregate_actual,
                fread(aggregate_actual, 1, sizeof aggregate_actual, file));
  ASSERT_TRUE(memcmp(aggregate_actual, aggregate_sentinel,
                     sizeof aggregate_sentinel) == 0);
  ASSERT_EQ_INT(0, fclose(file));
  ASSERT_EQ_INT(0, unlink(aggregate_path));

  badpath[0] = '\0';
  snprintf(badpath, sizeof badpath, "/tmp/sin-itemstore-v2-ref-XXXXXX");
  file = new_fixture(badpath);
  put_bytes(file, sentinel, sizeof sentinel);
  ASSERT_EQ_INT(0, fclose(file));
  root = make_root_item("root");
  ASSERT_NOT_NULL(root);
  ASSERT_NOT_NULL(test_item_set_value(
      root, "bad", (VALUE_t){VALUE_itemref, {.itemref = NULL}}));
  ASSERT_TRUE(!save_itemstore(badpath, root));
  destroy_item(root);
  file = fopen(badpath, "rb");
  ASSERT_NOT_NULL(file);
  ASSERT_EQ_INT(sizeof actual, fread(actual, 1, sizeof actual, file));
  ASSERT_TRUE(memcmp(actual, sentinel, sizeof actual) == 0);
  ASSERT_EQ_INT(0, fclose(file));
  ASSERT_EQ_INT(0, unlink(badpath));

  /* Exercise each v2-owned allocation failure directly; hooks are reset
     immediately so later assertions cannot inherit the failure budget. */
  uint8_t header_bytes[10];
  ITEMSTORE_READ_CTX_t alloc_ctx;
  {
    char alloc_path[] = "/tmp/sin-itemstore-v2-alloc-XXXXXX";
    file = new_fixture(alloc_path);
    put_header(file, 2);
    put_record_prefix(file, "root", WIRE_ITEM_VALUE);
    put_u8(file, WIRE_VALUE_ITEMREF);
    put_u16_le(file, 3);
    put_bytes(file, "a.b", 3);
    put_u32_le(file, 0);
    ASSERT_EQ_INT(0, fclose(file));
    file = fopen(alloc_path, "rb");
    ASSERT_NOT_NULL(file);
    ASSERT_EQ_INT(10, fread(header_bytes, 1, sizeof header_bytes, file));
    alloc_ctx = itemstore_read_context(alloc_path, 0);
    alloc_test_fail_after(0);
    ASSERT_TRUE(itemstore_read_record_for_version(2, file, NULL, &alloc_ctx) ==
                NULL);
    alloc_test_fail_after(-1);
    ASSERT_EQ_INT(0, fclose(file));
    ASSERT_EQ_INT(0, unlink(alloc_path));
  }
  {
    char alloc_path[] = "/tmp/sin-itemstore-v2-list-alloc-XXXXXX";
    file = new_fixture(alloc_path);
    put_header(file, 2);
    put_record_prefix(file, "root", WIRE_ITEM_VALUE);
    put_u8(file, WIRE_VALUE_LIST);
    put_u32_le(file, 1);
    put_u8(file, WIRE_VALUE_NIL);
    put_u32_le(file, 0);
    ASSERT_EQ_INT(0, fclose(file));
    file = fopen(alloc_path, "rb");
    ASSERT_NOT_NULL(file);
    ASSERT_EQ_INT(10, fread(header_bytes, 1, sizeof header_bytes, file));
    alloc_ctx = itemstore_read_context(alloc_path, 0);
    alloc_test_fail_after(0);
    ASSERT_TRUE(itemstore_read_record_for_version(2, file, NULL, &alloc_ctx) ==
                NULL);
    alloc_test_fail_after(-1);
    ASSERT_EQ_INT(0, fclose(file));
    ASSERT_EQ_INT(0, unlink(alloc_path));
  }
  {
    char alloc_path[] = "/tmp/sin-itemstore-v2-partial-XXXXXX";
    file = new_fixture(alloc_path);
    put_header(file, 2);
    put_record_prefix(file, "root", WIRE_ITEM_VALUE);
    put_u8(file, WIRE_VALUE_LIST);
    put_u32_le(file, 2);
    put_u8(file, WIRE_VALUE_STRING);
    put_u32_le(file, 1);
    put_u8(file, 'x');
    put_u8(file, WIRE_VALUE_STRING);
    put_u32_le(file, 1);
    put_u8(file, 'y');
    put_u32_le(file, 0);
    ASSERT_EQ_INT(0, fclose(file));
    file = fopen(alloc_path, "rb");
    ASSERT_NOT_NULL(file);
    ASSERT_EQ_INT(10, fread(header_bytes, 1, sizeof header_bytes, file));
    alloc_ctx = itemstore_read_context(alloc_path, 0);
    alloc_test_fail_after(3);
    ASSERT_TRUE(itemstore_read_record_for_version(2, file, NULL, &alloc_ctx) ==
                NULL);
    alloc_test_fail_after(-1);
    ASSERT_EQ_INT(0, fclose(file));
    ASSERT_EQ_INT(0, unlink(alloc_path));
  }

  {
    char constructor_path[] =
        "/tmp/sin-itemstore-list-ref-constructor-failure-XXXXXX";
    FILE *constructor_file = new_fixture(constructor_path);
    put_header(constructor_file, WIRE_VERSION);
    put_nil_record_prefix(constructor_file, "root", 2);
    put_record_prefix(constructor_file, "list", WIRE_ITEM_VALUE);
    put_u8(constructor_file, WIRE_VALUE_LIST);
    put_u32_le(constructor_file, 1);
    put_u8(constructor_file, WIRE_VALUE_STRING);
    put_u32_le(constructor_file, 3);
    put_bytes(constructor_file, "one", 3);
    put_u32_le(constructor_file, 0);
    put_record_prefix(constructor_file, "bad_ref", WIRE_ITEM_VALUE);
    put_u8(constructor_file, WIRE_VALUE_ITEMREF);
    put_u16_le(constructor_file, 3);
    put_bytes(constructor_file, "a.b", 3);
    put_u32_le(constructor_file, 0);
    ASSERT_EQ_INT(0, fclose(constructor_file));
    constructor_attempts = 0;
    itemstore_set_load_constructor_failure_hook_for_tests(
        fail_loaded_item_constructor);
    ASSERT_TRUE(load_itemstore(constructor_path) == NULL);
    itemstore_set_load_constructor_failure_hook_for_tests(NULL);
    ASSERT_EQ_INT(3, constructor_attempts);
    ASSERT_EQ_INT(0, unlink(constructor_path));
  }
}

void test_loaded_itemstore_mutation_roundtrip(void) {
  char path[] = "/tmp/sin-itemstore-mutation-XXXXXX";
  FILE *file = new_fixture(path);
  ASSERT_EQ_INT(0, fclose(file));

  static const uint8_t program_v1[] = {0, 0, 'h'};
  static const uint8_t program_v2[] = {
    0, 0, 'p', 9, 0, 0, 0, 0, 0, 0, 0, 'h'
  };
  static const uint8_t branch_code[] = {0, 0, 'b', 1, 'h'};
  static const uint8_t inserted_code[] = {
    0, 0, 'l', 1, 0, 'x', 'h'
  };

  ITEM_t *root = make_root_item("root");
  ASSERT_NOT_NULL(root);
  ASSERT_NOT_NULL(test_item_set_value(root, "alpha",
                              (VALUE_t){.type = VALUE_int, .i = 1}));
  ASSERT_NOT_NULL(test_item_set_value(root, "branch.leaf",
                              (VALUE_t){.type = VALUE_int, .i = 2}));
  ASSERT_NOT_NULL(test_item_set_value(root, "empty_parent",
                              (VALUE_t){.type = VALUE_nil, .i = 0}));
  ASSERT_NOT_NULL(test_item_set_code(root, "program", sizeof(program_v1),
                                   copy_bytecode(program_v1,
                                                 sizeof(program_v1))));
  ASSERT_NOT_NULL(test_item_set_code(root, "branch.code",
                                   sizeof(branch_code),
                                   copy_bytecode(branch_code,
                                                 sizeof(branch_code))));
  ASSERT_TRUE(save_itemstore(path, root));
  destroy_item(root);

  ITEM_t *loaded = load_itemstore(path);
  ASSERT_NOT_NULL(loaded);
  static const char *loaded_root_order[] = {
    "alpha", "branch", "empty_parent", "program"
  };
  assert_child_order(loaded, loaded_root_order,
                     sizeof(loaded_root_order) / sizeof(loaded_root_order[0]));
  static const char *loaded_branch_order[] = {"leaf", "code"};
  ITEM_t *branch = find_item(loaded, "branch");
  ASSERT_NOT_NULL(branch);
  assert_child_order(branch, loaded_branch_order,
                     sizeof(loaded_branch_order) /
                         sizeof(loaded_branch_order[0]));
  ITEM_t *empty_parent = find_item(loaded, "empty_parent");
  ASSERT_NOT_NULL(empty_parent);
  ASSERT_EQ_INT(0, item_child_count(empty_parent));
  ASSERT_EQ_INT(0, item_children_ordered_capacity(empty_parent->children));
  assert_int_item(loaded, "alpha", 1);
  assert_int_item(loaded, "branch.leaf", 2);
  assert_code_item(loaded, "program", program_v1, sizeof(program_v1));
  assert_code_item(loaded, "branch.code", branch_code, sizeof(branch_code));

  ASSERT_NOT_NULL(test_item_set_value(loaded, "new_root",
                              (VALUE_t){.type = VALUE_int, .i = 10}));
  ASSERT_NOT_NULL(test_item_set_value(loaded, "empty_parent.child",
                              (VALUE_t){.type = VALUE_int, .i = 11}));
  ASSERT_NOT_NULL(test_item_set_code(loaded, "new_code", sizeof(inserted_code),
                                   copy_bytecode(inserted_code,
                                                 sizeof(inserted_code))));
  ASSERT_NOT_NULL(test_item_set_code(loaded, "program", sizeof(program_v2),
                                   copy_bytecode(program_v2,
                                                 sizeof(program_v2))));
  test_item_delete(loaded, "alpha");
  test_item_delete(loaded, "branch.leaf");
  ASSERT_TRUE(find_item(loaded, "alpha") == NULL);
  ASSERT_TRUE(find_item(loaded, "branch.leaf") == NULL);
  ASSERT_NOT_NULL(test_item_set_value(loaded, "alpha",
                              (VALUE_t){.type = VALUE_int, .i = 20}));
  ASSERT_NOT_NULL(test_item_set_code(loaded, "branch.leaf",
                                   sizeof(inserted_code),
                                   copy_bytecode(inserted_code,
                                                 sizeof(inserted_code))));

  static const char *mutated_root_order[] = {
    "branch", "empty_parent", "program", "new_root", "new_code", "alpha"
  };
  assert_child_order(loaded, mutated_root_order,
                     sizeof(mutated_root_order) /
                         sizeof(mutated_root_order[0]));
  static const char *mutated_branch_order[] = {"code", "leaf"};
  assert_child_order(branch, mutated_branch_order,
                     sizeof(mutated_branch_order) /
                         sizeof(mutated_branch_order[0]));
  static const char *empty_parent_order[] = {"child"};
  assert_child_order(empty_parent, empty_parent_order,
                     sizeof(empty_parent_order) /
                         sizeof(empty_parent_order[0]));
  assert_int_item(loaded, "alpha", 20);
  assert_int_item(loaded, "new_root", 10);
  assert_int_item(loaded, "empty_parent.child", 11);
  assert_code_item(loaded, "program", program_v2, sizeof(program_v2));
  assert_code_item(loaded, "branch.leaf", inserted_code,
                   sizeof(inserted_code));
  assert_code_item(loaded, "new_code", inserted_code, sizeof(inserted_code));

  ASSERT_TRUE(save_itemstore(path, loaded));
  destroy_item(loaded);

  ITEM_t *reloaded = load_itemstore(path);
  ASSERT_NOT_NULL(reloaded);
  assert_child_order(reloaded, mutated_root_order,
                     sizeof(mutated_root_order) /
                         sizeof(mutated_root_order[0]));
  ITEM_t *reloaded_branch = find_item(reloaded, "branch");
  ASSERT_NOT_NULL(reloaded_branch);
  assert_child_order(reloaded_branch, mutated_branch_order,
                     sizeof(mutated_branch_order) /
                         sizeof(mutated_branch_order[0]));
  ITEM_t *reloaded_empty_parent = find_item(reloaded, "empty_parent");
  ASSERT_NOT_NULL(reloaded_empty_parent);
  assert_child_order(reloaded_empty_parent, empty_parent_order,
                     sizeof(empty_parent_order) /
                         sizeof(empty_parent_order[0]));
  assert_int_item(reloaded, "alpha", 20);
  assert_int_item(reloaded, "new_root", 10);
  assert_int_item(reloaded, "empty_parent.child", 11);
  assert_code_item(reloaded, "program", program_v2, sizeof(program_v2));
  assert_code_item(reloaded, "branch.code", branch_code,
                   sizeof(branch_code));
  assert_code_item(reloaded, "branch.leaf", inserted_code,
                   sizeof(inserted_code));
  assert_code_item(reloaded, "new_code", inserted_code,
                   sizeof(inserted_code));

  destroy_item(reloaded);
  ASSERT_EQ_INT(0, unlink(path));
}

void test_item_set_code_rejects_inuse_replacement(void) {
  ITEM_t *root = make_root_item("root");
  ASSERT_NOT_NULL(root);

  uint8_t *initial = malloc(1);
  ASSERT_NOT_NULL(initial);
  initial[0] = 'h';
  ITEM_MUTATION_RESULT_t created = item_set_code(root, "code", 1, initial);
  ASSERT_EQ_INT(ITEM_MUTATION_CREATED, created.status);
  ASSERT_TRUE(item_mutation_succeeded(created));
  ITEM_t *code = created.item;
  ASSERT_NOT_NULL(code);
  item_enter_use(code);
  item_enter_use(code);

  uint8_t *replacement = malloc(2);
  ASSERT_NOT_NULL(replacement);
  replacement[0] = 0;
  replacement[1] = 'h';
  ITEM_MUTATION_RESULT_t rejected = item_set_code(root, "code", 2, replacement);
  ASSERT_EQ_INT(ITEM_MUTATION_IN_USE, rejected.status);
  ASSERT_TRUE(rejected.item == NULL);
  ASSERT_TRUE(!item_mutation_succeeded(rejected));
  ASSERT_EQ_INT(ITEM_code, item_kind(code));
  ASSERT_EQ_INT(1, code->bytecode_len);
  ASSERT_TRUE(code->bytecode == initial);
  free(replacement);

  item_leave_use(code);
  ASSERT_TRUE(item_is_in_use(code));
  uint8_t *still_rejected = malloc(2);
  ASSERT_NOT_NULL(still_rejected);
  still_rejected[0] = 0;
  still_rejected[1] = 'h';
  rejected = item_set_code(root, "code", 2, still_rejected);
  ASSERT_EQ_INT(ITEM_MUTATION_IN_USE, rejected.status);
  ASSERT_TRUE(rejected.item == NULL);
  ASSERT_EQ_INT(1, code->bytecode_len);
  free(still_rejected);

  item_leave_use(code);
  ASSERT_TRUE(!item_is_in_use(code));
  uint8_t *accepted = malloc(2);
  ASSERT_NOT_NULL(accepted);
  accepted[0] = 0;
  accepted[1] = 'h';
  ITEM_MUTATION_RESULT_t replaced = item_set_code(root, "code", 2, accepted);
  ASSERT_EQ_INT(ITEM_MUTATION_REPLACED, replaced.status);
  ASSERT_TRUE(item_mutation_succeeded(replaced));
  ASSERT_TRUE(replaced.item == code);
  ASSERT_EQ_INT(2, code->bytecode_len);
  created = item_set_value(root, "value", (VALUE_t){.type = VALUE_nil, .i = 0});
  ASSERT_EQ_INT(ITEM_MUTATION_CREATED, created.status);
  ASSERT_TRUE(created.item != NULL);
  ITEM_t *value_item = find_item(root, "value");
  ASSERT_NOT_NULL(value_item);
  item_enter_use(value_item);

  uint8_t *new_code = malloc(1);
  ASSERT_NOT_NULL(new_code);
  new_code[0] = 'h';
  rejected = item_set_code(root, "value", 1, new_code);
  ASSERT_EQ_INT(ITEM_MUTATION_IN_USE, rejected.status);
  ASSERT_TRUE(rejected.item == NULL);
  ASSERT_EQ_INT(ITEM_value, value_item->type);
  ASSERT_EQ_INT(VALUE_nil, value_item->value.type);
  free(new_code);

  item_leave_use(value_item);
  destroy_item(root);
}

void test_item_execution_pins_are_balanced_and_zero_safe(void) {
  ITEM_t *root = make_root_item("root");
  ASSERT_NOT_NULL(root);
  ITEM_t *item = test_item_set_value(root, "target",
                             (VALUE_t){.type = VALUE_nil, .i = 0});
  ASSERT_NOT_NULL(item);
  ASSERT_TRUE(!item_is_in_use(item));
  item_enter_use(item);
  item_enter_use(item);
  ASSERT_TRUE(item_is_in_use(item));
  item_leave_use(item);
  ASSERT_TRUE(item_is_in_use(item));
  item_leave_use(item);
  ASSERT_TRUE(!item_is_in_use(item));
  item_leave_use(item);
  ASSERT_TRUE(!item_is_in_use(item));
  destroy_item(root);
}

void test_item_delete_rejects_pinned_descendant(void) {
  ITEMSTORE_t *store = itemstore_create("root");
  ASSERT_NOT_NULL(store);
  ITEM_t *root = itemstore_root(store);
  ASSERT_NOT_NULL(root);
  ITEM_MUTATION_RESULT_t created = item_set_value(
      root, "branch.leaf", (VALUE_t){.type = VALUE_int, .i = 7});
  ASSERT_EQ_INT(ITEM_MUTATION_CREATED, created.status);
  ASSERT_TRUE(created.item != NULL);
  ITEM_t *branch = find_item(root, "branch");
  ITEM_t *leaf = find_item(root, "branch.leaf");
  ASSERT_NOT_NULL(branch);
  ASSERT_NOT_NULL(leaf);
  ASSERT_TRUE(find_item_cached(root, "branch.leaf", NULL) == leaf);
  uint64_t generation = itemstore_topology_revision(store);
  uint64_t payload_revision = itemstore_payload_revision(store);
  uint64_t hits = itemstore_cache_hits(store);
  uint64_t misses = itemstore_cache_misses(store);
  item_enter_use(leaf);
  ITEM_MUTATION_RESULT_t rejected = item_delete(root, "branch");
  ASSERT_EQ_INT(ITEM_MUTATION_IN_USE, rejected.status);
  ASSERT_TRUE(rejected.item == NULL);
  ASSERT_TRUE(find_item(root, "branch") == branch);
  ASSERT_TRUE(find_item(root, "branch.leaf") == leaf);
  ASSERT_EQ_INT(generation, itemstore_topology_revision(store));
  ASSERT_EQ_INT(payload_revision, itemstore_payload_revision(store));
  ASSERT_TRUE(find_item_cached(root, "branch.leaf", NULL) == leaf);
  ASSERT_TRUE(itemstore_cache_hits(store) > hits);
  ASSERT_EQ_INT(misses, itemstore_cache_misses(store));
  ASSERT_EQ_INT(7, find_item(root, "branch.leaf")->value.i);
  item_leave_use(leaf);
  ITEM_MUTATION_RESULT_t deleted = item_delete(root, "branch");
  ASSERT_EQ_INT(ITEM_MUTATION_DELETED, deleted.status);
  ASSERT_TRUE(item_mutation_succeeded(deleted));
  ASSERT_TRUE(deleted.item == NULL);
  ASSERT_TRUE(find_item(root, "branch") == NULL);
  ASSERT_EQ_INT(generation + 1, itemstore_topology_revision(store));
  ASSERT_EQ_INT(payload_revision, itemstore_payload_revision(store));
  ASSERT_TRUE(find_item_cached(root, "branch.leaf", NULL) == NULL);
  ITEM_MUTATION_RESULT_t missing = item_delete(root, "missing");
  ASSERT_EQ_INT(ITEM_MUTATION_NOT_FOUND, missing.status);
  ASSERT_TRUE(missing.item == NULL);
  ITEM_MUTATION_RESULT_t malformed = item_delete(root, "branch..bad");
  ASSERT_EQ_INT(ITEM_MUTATION_INVALID_NAME, malformed.status);
  ASSERT_TRUE(malformed.item == NULL);
  itemstore_destroy(store);
}

void test_itemstore_payload_replacement_contracts(void) {
  ITEM_t *root = make_root_item("root");
  ASSERT_NOT_NULL(root);

  ITEM_MUTATION_RESULT_t mutation = item_set_value(NULL, "x", VALUE_NIL);
  ASSERT_EQ_INT(ITEM_MUTATION_INVALID_ARGUMENT, mutation.status);
  ASSERT_TRUE(mutation.item == NULL);
  ASSERT_TRUE(!item_mutation_succeeded(mutation));

  char *old_value = strdup("old value");
  ASSERT_NOT_NULL(old_value);
  mutation = item_set_value(
      root, "insert_target", (VALUE_t){.type = VALUE_str, .s = old_value});
  ASSERT_EQ_INT(ITEM_MUTATION_CREATED, mutation.status);
  ASSERT_TRUE(mutation.item != NULL);

  uint8_t *first_code = copy_bytecode((const uint8_t *)"h", 1);
  uint64_t generation = get_itemstore_payload_revision();
  mutation = item_set_code(root, "insert_target", 1, first_code);
  ASSERT_EQ_INT(ITEM_MUTATION_REPLACED, mutation.status);
  ITEM_t *insert_target = mutation.item;
  ASSERT_NOT_NULL(insert_target);
  ASSERT_EQ_INT(generation + 1, get_itemstore_payload_revision());
  ASSERT_EQ_INT(ITEM_code, insert_target->type);
  ASSERT_EQ_INT(1, insert_target->bytecode_len);
  ASSERT_EQ_INT(VALUE_nil, insert_target->value.type);

  char *inserted_value = strdup("new value");
  ASSERT_NOT_NULL(inserted_value);
  generation = get_itemstore_payload_revision();
  mutation = item_set_value(root, "insert_target",
                            (VALUE_t){.type = VALUE_str, .s = inserted_value});
  ASSERT_EQ_INT(ITEM_MUTATION_REPLACED, mutation.status);
  ASSERT_TRUE(mutation.item == insert_target);
  ASSERT_EQ_INT(generation + 1, get_itemstore_payload_revision());
  ASSERT_EQ_INT(ITEM_value, insert_target->type);
  ASSERT_EQ_INT(VALUE_str, insert_target->value.type);
  ASSERT_TRUE(strcmp(insert_target->value.s, "new value") == 0);
  ASSERT_TRUE(insert_target->bytecode == NULL);
  ASSERT_EQ_INT(0, insert_target->bytecode_len);

  char *same_value = insert_target->value.s;
  generation = get_itemstore_payload_revision();
  mutation = item_set_value(root, "insert_target",
                            (VALUE_t){.type = VALUE_str, .s = same_value});
  ASSERT_EQ_INT(ITEM_MUTATION_REPLACED, mutation.status);
  ASSERT_TRUE(mutation.item == insert_target);
  ASSERT_EQ_INT(generation + 1, get_itemstore_payload_revision());
  ASSERT_TRUE(insert_target->value.s == same_value);

  // A code pointer is not a string, so this cross-kind alias must be
  // rejected before any string inspection or payload replacement.
  uint8_t *value_as_code = (uint8_t *)same_value;
  generation = get_itemstore_payload_revision();
  uint64_t topology_before_reject = get_itemstore_topology_revision();
  mutation = item_set_code(root, "insert_target", 1, value_as_code);
  ASSERT_EQ_INT(ITEM_MUTATION_INVALID_PAYLOAD, mutation.status);
  ASSERT_TRUE(mutation.item == NULL);
  ASSERT_EQ_INT(generation, get_itemstore_payload_revision());
  ASSERT_EQ_INT(topology_before_reject, get_itemstore_topology_revision());
  ASSERT_EQ_INT(ITEM_value, insert_target->type);
  ASSERT_TRUE(insert_target->value.s == same_value);

  uint8_t *set_code = copy_bytecode((const uint8_t *)"h", 1);
  mutation = item_set_code(root, "set_target", 1, set_code);
  ASSERT_EQ_INT(ITEM_MUTATION_CREATED, mutation.status);
  ITEM_t *set_target = mutation.item;
  ASSERT_NOT_NULL(set_target);
  char *set_value = strdup("set value");
  ASSERT_NOT_NULL(set_value);
  generation = get_itemstore_payload_revision();
  uint64_t topology_before_set = get_itemstore_topology_revision();
  mutation = item_set_value(root, "set_target",
                            (VALUE_t){.type = VALUE_str, .s = set_value});
  ASSERT_EQ_INT(ITEM_MUTATION_REPLACED, mutation.status);
  ASSERT_TRUE(mutation.item == set_target);
  ASSERT_EQ_INT(generation + 1, get_itemstore_payload_revision());
  ASSERT_EQ_INT(topology_before_set, get_itemstore_topology_revision());
  ASSERT_EQ_INT(ITEM_value, set_target->type);
  ASSERT_EQ_INT(VALUE_str, set_target->value.type);
  ASSERT_TRUE(strcmp(set_target->value.s, "set value") == 0);
  ASSERT_TRUE(set_target->bytecode == NULL);
  ASSERT_EQ_INT(0, set_target->bytecode_len);

  mutation = item_set_code(root, "alias_code", 1,
                           copy_bytecode((const uint8_t *)"h", 1));
  ASSERT_EQ_INT(ITEM_MUTATION_CREATED, mutation.status);
  ITEM_t *alias_code = mutation.item;
  ASSERT_NOT_NULL(alias_code);
  uint8_t *same_code = alias_code->bytecode;
  generation = get_itemstore_payload_revision();
  mutation = item_set_code(root, "alias_code", 1, same_code);
  ASSERT_EQ_INT(ITEM_MUTATION_REPLACED, mutation.status);
  ASSERT_TRUE(mutation.item == alias_code);
  ASSERT_EQ_INT(generation + 1, get_itemstore_payload_revision());
  ASSERT_TRUE(alias_code->bytecode == same_code);

  // The bytecode pointer is owned by alias_code and must not be interpreted
  // as a C string by either replacement API.
  char *code_as_value = (char *)same_code;
  generation = get_itemstore_payload_revision();
  topology_before_reject = get_itemstore_topology_revision();
  mutation = item_set_value(root, "alias_code",
                            (VALUE_t){.type = VALUE_str, .s = code_as_value});
  ASSERT_EQ_INT(ITEM_MUTATION_INVALID_PAYLOAD, mutation.status);
  ASSERT_TRUE(mutation.item == NULL);
  ASSERT_EQ_INT(generation, get_itemstore_payload_revision());
  ASSERT_EQ_INT(topology_before_reject, get_itemstore_topology_revision());
  ASSERT_EQ_INT(ITEM_code, alias_code->type);
  ASSERT_TRUE(alias_code->bytecode == same_code);
  generation = get_itemstore_payload_revision();
  mutation = item_set_value(root, "alias_code",
                            (VALUE_t){.type = VALUE_str, .s = code_as_value});
  ASSERT_EQ_INT(ITEM_MUTATION_INVALID_PAYLOAD, mutation.status);
  ASSERT_TRUE(mutation.item == NULL);
  ASSERT_EQ_INT(generation, get_itemstore_payload_revision());
  ASSERT_EQ_INT(ITEM_code, alias_code->type);
  ASSERT_TRUE(alias_code->bytecode == same_code);

  mutation = item_set_code(root, "inuse", 1,
                           copy_bytecode((const uint8_t *)"h", 1));
  ASSERT_EQ_INT(ITEM_MUTATION_CREATED, mutation.status);
  ITEM_t *inuse = mutation.item;
  ASSERT_NOT_NULL(inuse);
  item_enter_use(inuse);
  uint8_t *rejected_code = copy_bytecode((const uint8_t *)"hh", 2);
  generation = get_itemstore_payload_revision();
  topology_before_reject = get_itemstore_topology_revision();
  mutation = item_set_code(root, "inuse", 2, rejected_code);
  ASSERT_EQ_INT(ITEM_MUTATION_IN_USE, mutation.status);
  ASSERT_TRUE(mutation.item == NULL);
  ASSERT_EQ_INT(generation, get_itemstore_payload_revision());
  ASSERT_EQ_INT(topology_before_reject, get_itemstore_topology_revision());
  ASSERT_EQ_INT(1, inuse->bytecode_len);
  free(rejected_code);

  char *rejected_value = strdup("rejected value");
  ASSERT_NOT_NULL(rejected_value);
  generation = get_itemstore_payload_revision();
  mutation = item_set_value(root, "inuse",
                            (VALUE_t){.type = VALUE_str, .s = rejected_value});
  ASSERT_EQ_INT(ITEM_MUTATION_IN_USE, mutation.status);
  ASSERT_TRUE(mutation.item == NULL);
  ASSERT_EQ_INT(generation, get_itemstore_payload_revision());
  ASSERT_EQ_INT(ITEM_code, inuse->type);
  ASSERT_EQ_INT(1, inuse->bytecode_len);
  value_free(&(VALUE_t){.type = VALUE_str, .s = rejected_value});
  item_leave_use(inuse);

  char *assigned_value = strdup("assigned value");
  ASSERT_NOT_NULL(assigned_value);
  mutation = item_set_value(root, "assign_target",
                            (VALUE_t){.type = VALUE_str, .s = assigned_value});
  ASSERT_EQ_INT(ITEM_MUTATION_CREATED, mutation.status);
  ITEM_t *assign_target = mutation.item;
  ASSERT_NOT_NULL(assign_target);
  item_enter_use(assign_target);
  VALUE_t assigned_name = {.type = VALUE_str, .s = strdup("assign_target")};
  ASSERT_NOT_NULL(assigned_name.s);
  assignitem(root, &assigned_name,
             (VALUE_t){.type = VALUE_str, .s = assigned_value});
  ASSERT_TRUE(assign_target->value.s == assigned_value);
  ASSERT_TRUE(strcmp(assign_target->value.s, "assigned value") == 0);
  item_leave_use(assign_target);

  char *invalid = strdup("invalid");
  ASSERT_NOT_NULL(invalid);
  generation = get_itemstore_payload_revision();
  mutation = item_set_value(root, "invalid..name",
                            (VALUE_t){.type = VALUE_str, .s = invalid});
  ASSERT_EQ_INT(ITEM_MUTATION_INVALID_NAME, mutation.status);
  ASSERT_TRUE(mutation.item == NULL);
  ASSERT_EQ_INT(generation, get_itemstore_payload_revision());
  value_free(&(VALUE_t){.type = VALUE_str, .s = invalid});

  char *invalid_set = strdup("invalid set");
  ASSERT_NOT_NULL(invalid_set);
  generation = get_itemstore_payload_revision();
  mutation = item_set_value(root, "invalid..set",
                            (VALUE_t){.type = VALUE_str, .s = invalid_set});
  ASSERT_EQ_INT(ITEM_MUTATION_INVALID_NAME, mutation.status);
  ASSERT_TRUE(mutation.item == NULL);
  ASSERT_EQ_INT(generation, get_itemstore_payload_revision());
  value_free(&(VALUE_t){.type = VALUE_str, .s = invalid_set});

  char *oversized = malloc(SIN_MAX_STRING_BYTES + 2);
  ASSERT_NOT_NULL(oversized);
  memset(oversized, 'x', SIN_MAX_STRING_BYTES + 1);
  oversized[SIN_MAX_STRING_BYTES + 1] = '\0';
  generation = get_itemstore_payload_revision();
  mutation = item_set_value(root, "oversized",
                            (VALUE_t){.type = VALUE_str, .s = oversized});
  ASSERT_EQ_INT(ITEM_MUTATION_INVALID_PAYLOAD, mutation.status);
  ASSERT_TRUE(mutation.item == NULL);
  ASSERT_EQ_INT(generation, get_itemstore_payload_revision());
  value_free(&(VALUE_t){.type = VALUE_str, .s = oversized});

  char *oversized_set = malloc(SIN_MAX_STRING_BYTES + 2);
  ASSERT_NOT_NULL(oversized_set);
  memset(oversized_set, 'y', SIN_MAX_STRING_BYTES + 1);
  oversized_set[SIN_MAX_STRING_BYTES + 1] = '\0';
  generation = get_itemstore_payload_revision();
  mutation = item_set_value(root, "oversized_set",
                            (VALUE_t){.type = VALUE_str, .s = oversized_set});
  ASSERT_EQ_INT(ITEM_MUTATION_INVALID_PAYLOAD, mutation.status);
  ASSERT_TRUE(mutation.item == NULL);
  ASSERT_EQ_INT(generation, get_itemstore_payload_revision());
  value_free(&(VALUE_t){.type = VALUE_str, .s = oversized_set});

  destroy_item(root);
}

void test_itemstore_path_creation_rolls_back_on_failure(void) {
  ITEM_t *root = make_root_item("root");
  ASSERT_NOT_NULL(root);
  itemstore_set_item_creation_failure_hook_for_tests(fail_item_creation);

  char *insert_value = strdup("retained");
  ASSERT_NOT_NULL(insert_value);
  uint64_t generation = get_itemstore_payload_revision();
  uint64_t topology = get_itemstore_topology_revision();
  ITEM_MUTATION_RESULT_t mutation = item_set_value(
      root, "transaction.fail.leaf",
      (VALUE_t){.type = VALUE_str, .s = insert_value});
  ASSERT_EQ_INT(ITEM_MUTATION_ALLOCATION_FAILED, mutation.status);
  ASSERT_TRUE(mutation.item == NULL);
  ASSERT_TRUE(find_item(root, "transaction") == NULL);
  ASSERT_EQ_INT(generation, get_itemstore_payload_revision());
  ASSERT_EQ_INT(topology, get_itemstore_topology_revision());
  value_free(&(VALUE_t){.type = VALUE_str, .s = insert_value});

  char *set_value = strdup("consumed");
  ASSERT_NOT_NULL(set_value);
  generation = get_itemstore_payload_revision();
  topology = get_itemstore_topology_revision();
  mutation = item_set_value(root, "transaction.fail.leaf",
                            (VALUE_t){.type = VALUE_str, .s = set_value});
  ASSERT_EQ_INT(ITEM_MUTATION_ALLOCATION_FAILED, mutation.status);
  ASSERT_TRUE(mutation.item == NULL);
  ASSERT_TRUE(find_item(root, "transaction") == NULL);
  ASSERT_EQ_INT(generation, get_itemstore_payload_revision());
  ASSERT_EQ_INT(topology, get_itemstore_topology_revision());
  value_free(&(VALUE_t){.type = VALUE_str, .s = set_value});

  itemstore_set_item_creation_failure_hook_for_tests(NULL);
  destroy_item(root);
}

void test_itemstore_nested_depth_roundtrip(void) {
  char path[] = "/tmp/sin-itemstore-depth-XXXXXX";
  FILE *file = new_fixture(path);
  ASSERT_EQ_INT(0, fclose(file));

  ITEM_t *root = make_root_item("root");
  ASSERT_NOT_NULL(root);
  ASSERT_NOT_NULL(test_item_set_value(root, "a.b.c.d.e.f.g.h",
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

void test_itemstore_item_name_contract_boundaries_roundtrip(void) {
  char path[] = "/tmp/sin-itemstore-name-boundaries-XXXXXX";
  FILE *file = new_fixture(path);
  ASSERT_EQ_INT(0, fclose(file));

  ITEM_t *root = make_root_item("root/name");
  ASSERT_NOT_NULL(root);
  ASSERT_NOT_NULL(test_item_set_value(root, "a",
                              (VALUE_t){.type = VALUE_int, .i = 1}));

  char max_layer[ITEM_MAX_LAYER_NAME_LENGTH + 1u];
  memset(max_layer, 'b', ITEM_MAX_LAYER_NAME_LENGTH);
  max_layer[ITEM_MAX_LAYER_NAME_LENGTH] = '\0';
  ASSERT_NOT_NULL(test_item_set_value(root, max_layer,
                              (VALUE_t){.type = VALUE_int, .i = 32}));

  char max_path[MAX_ITEM_NAME];
  build_max_item_path(max_path);
  ITEM_t *deepest = test_item_set_value(root, max_path,
                                (VALUE_t){.type = VALUE_int, .i = 263});
  ASSERT_NOT_NULL(deepest);
  char assembled[MAX_ITEM_NAME] = {0};
  get_itemname(deepest, assembled);
  ASSERT_TRUE(strcmp(max_path, assembled) == 0);

  uint8_t *bytecode = malloc(3);
  ASSERT_NOT_NULL(bytecode);
  bytecode[0] = 0;
  bytecode[1] = 0;
  bytecode[2] = 'h';
  ASSERT_NOT_NULL(test_item_set_code(root, "code_boundary_123456789012345678",
                                   3, bytecode));

  ASSERT_TRUE(save_itemstore(path, root));
  ITEM_t *loaded = load_itemstore(path);
  ASSERT_NOT_NULL(loaded);
  ASSERT_EQ_INT(1, find_item(loaded, "a")->value.i);
  ASSERT_EQ_INT(32, find_item(loaded, max_layer)->value.i);
  ASSERT_EQ_INT(263, find_item(loaded, max_path)->value.i);
  ASSERT_EQ_INT(ITEM_code,
                find_item(loaded, "code_boundary_123456789012345678")->type);

  destroy_item(loaded);
  destroy_item(root);
  ASSERT_EQ_INT(0, unlink(path));
}

void test_itemstore_item_name_rejection_is_atomic(void) {
  ITEM_t *root = make_root_item("root");
  ASSERT_NOT_NULL(root);
  char non_ascii[] = {(char)0xc3, (char)0xa9, '\0'};
  const char *invalid_names[] = {
    "bad-name", "bad/name", "bad space", non_ascii
  };

  for (size_t i = 0; i < sizeof invalid_names / sizeof invalid_names[0];
       i++) {
    uint64_t topology = get_itemstore_topology_revision();
    uint64_t payload = get_itemstore_payload_revision();
    ASSERT_TRUE(test_item_set_value(root, invalid_names[i],
                            (VALUE_t){.type = VALUE_int, .i = 1}) == NULL);
    ASSERT_EQ_INT(topology, get_itemstore_topology_revision());
    ASSERT_EQ_INT(payload, get_itemstore_payload_revision());
  }

  uint64_t topology = get_itemstore_topology_revision();
  uint64_t payload = get_itemstore_payload_revision();
  ASSERT_TRUE(test_item_set_value(root, "a.b.c.d.e.f.g.h.i",
                          (VALUE_t){.type = VALUE_int, .i = 1}) == NULL);
  ASSERT_EQ_INT(topology, get_itemstore_topology_revision());
  ASSERT_EQ_INT(payload, get_itemstore_payload_revision());
  ASSERT_TRUE(find_item(root, "a") == NULL);

  char max_path[MAX_ITEM_NAME];
  build_max_item_path(max_path);
  char too_long[MAX_ITEM_NAME + 2u];
  ASSERT_TRUE(snprintf(too_long, sizeof too_long, "%s.x", max_path)
              < (int)sizeof too_long);
  topology = get_itemstore_topology_revision();
  payload = get_itemstore_payload_revision();
  test_item_set_value(root, too_long, (VALUE_t){.type = VALUE_int, .i = 2});
  ASSERT_EQ_INT(topology, get_itemstore_topology_revision());
  ASSERT_EQ_INT(payload, get_itemstore_payload_revision());
  ASSERT_TRUE(find_item(root, "a") == NULL);

  destroy_item(root);
}

void test_itemstore_item_name_relative_depth_contract(void) {
  ITEM_t *root = make_root_item("root");
  ASSERT_NOT_NULL(root);
  ITEM_t *parent = test_item_set_value(root, "a.b.c.d.e.f.g",
                               (VALUE_t){.type = VALUE_nil, .i = 0});
  ASSERT_NOT_NULL(parent);
  ASSERT_NOT_NULL(test_item_set_value(parent, "h",
                              (VALUE_t){.type = VALUE_int, .i = 8}));
  ITEM_t *deep_parent = find_item(parent, "h");
  ASSERT_NOT_NULL(deep_parent);

  uint64_t topology = get_itemstore_topology_revision();
  uint64_t payload = get_itemstore_payload_revision();
  ASSERT_TRUE(test_item_set_value(deep_parent, "i",
                          (VALUE_t){.type = VALUE_int, .i = 9}) == NULL);
  ASSERT_EQ_INT(topology, get_itemstore_topology_revision());
  ASSERT_EQ_INT(payload, get_itemstore_payload_revision());
  ASSERT_TRUE(find_item(deep_parent, "i") == NULL);
  ASSERT_TRUE(find_item(root, "a.b.c.d.e.f.g.i") == NULL);

  test_item_delete(parent, "h");
  ASSERT_TRUE(find_item(parent, "h") == NULL);
  destroy_item(root);
}

void test_save_itemstore_rejects_manually_invalid_item_names(void) {
  char path[] = "/tmp/sin-itemstore-invalid-tree-XXXXXX";
  FILE *file = new_fixture(path);
  ASSERT_EQ_INT(0, fclose(file));

  ITEM_t *root = make_root_item("root");
  ASSERT_NOT_NULL(root);
  ASSERT_NOT_NULL(make_item("bad-name", root, ITEM_value,
                            VALUE_NIL, NULL, 0));
  ASSERT_TRUE(!save_itemstore(path, root));
  destroy_item(root);

  root = make_root_item("root");
  ASSERT_NOT_NULL(root);
  ITEM_t *current = root;
  for (size_t depth = 0; depth <= ITEM_MAX_DEPTH; depth++) {
    current = make_item("x", current, ITEM_value, VALUE_NIL, NULL, 0);
    ASSERT_NOT_NULL(current);
  }
  ASSERT_TRUE(!save_itemstore(path, root));
  destroy_item(root);
  ASSERT_EQ_INT(0, unlink(path));
}

void test_load_itemstore_handles_constructor_failure_with_children(void) {
  {
    char path[] = "/tmp/sin-itemstore-string-constructor-failure-XXXXXX";
    FILE *file = new_fixture(path);
    put_header(file, WIRE_VERSION);
    put_nil_record_prefix(file, "root", 2);
    put_nil_record_prefix(file, "first", 0);
    put_string_record_prefix(file, "bad", "payload", 1);
    put_nil_record_prefix(file, "nested", 0);
    ASSERT_EQ_INT(0, fclose(file));

    constructor_attempts = 0;
    memset(constructor_names, 0, sizeof constructor_names);
    itemstore_set_load_constructor_failure_hook_for_tests(
        fail_loaded_item_constructor);
    bool loaded = load_itemstore(path) != NULL;
    itemstore_set_load_constructor_failure_hook_for_tests(NULL);

    ASSERT_TRUE(!loaded);
    ASSERT_EQ_INT(3, constructor_attempts);
    ASSERT_TRUE(strcmp(constructor_names[0], "root") == 0);
    ASSERT_TRUE(strcmp(constructor_names[1], "first") == 0);
    ASSERT_TRUE(strcmp(constructor_names[2], "bad") == 0);
    ASSERT_EQ_INT(0, unlink(path));
  }

  {
    char path[] = "/tmp/sin-itemstore-code-constructor-failure-XXXXXX";
    static const uint8_t bytecode[] = {0x01, 0x02, 0x03};
    FILE *file = new_fixture(path);
    put_header(file, WIRE_VERSION);
    put_nil_record_prefix(file, "root", 1);
    put_code_record_prefix(file, "bad_code", bytecode, sizeof bytecode, 1);
    put_nil_record_prefix(file, "nested", 0);
    ASSERT_EQ_INT(0, fclose(file));

    constructor_attempts = 0;
    memset(constructor_names, 0, sizeof constructor_names);
    itemstore_set_load_constructor_failure_hook_for_tests(
        fail_loaded_item_constructor);
    bool loaded = load_itemstore_with_options(path, false) != NULL;
    itemstore_set_load_constructor_failure_hook_for_tests(NULL);

    ASSERT_TRUE(!loaded);
    ASSERT_EQ_INT(2, constructor_attempts);
    ASSERT_TRUE(strcmp(constructor_names[0], "root") == 0);
    ASSERT_TRUE(strcmp(constructor_names[1], "bad_code") == 0);
    ASSERT_EQ_INT(0, unlink(path));
  }
}

void test_itemstore_loads_generated_v1_wire_fixture(void) {
  char path[] = "/tmp/sin-itemstore-wire-XXXXXX";
  FILE *file = new_fixture(path);
  put_header(file, 1);
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

  static const uint8_t bytecode[] = {0x00, 0x00, 'h'};
  put_record_prefix(file, "code", WIRE_ITEM_CODE);
  put_u32_le(file, sizeof(bytecode));
  put_bytes(file, bytecode, sizeof(bytecode));
  put_u32_le(file, 0);
  ASSERT_EQ_INT(0, fclose(file));

  ITEM_t *loaded = load_itemstore(path);
  ASSERT_TRUE(loaded == NULL);
  file = fopen(path, "rb"); ASSERT_NOT_NULL(file);
  uint8_t header[10]; ASSERT_EQ_INT(10, fread(header, 1, sizeof header, file));
  ITEMSTORE_READ_CTX_t ctx = itemstore_read_context(path, 0);
  loaded = itemstore_read_record_for_version(1, file, NULL, &ctx);
  ASSERT_NOT_NULL(loaded);
  ASSERT_EQ_INT(0, fclose(file));
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

  detach_item_and_destroy(loaded);
  ASSERT_EQ_INT(0, unlink(path));
}


static void put_code_record(FILE *file, const char *name, const uint8_t *bytecode,
                            uint32_t bytecode_len, uint32_t child_count) {
  put_record_prefix(file, name, WIRE_ITEM_CODE);
  put_u32_le(file, bytecode_len);
  if (bytecode_len > 0) put_bytes(file, bytecode, bytecode_len);
  put_u32_le(file, child_count);
}

static void assert_malformed_code_rejected(const uint8_t *bytecode,
                                           uint32_t bytecode_len) {
  char path[] = "/tmp/sin-itemstore-bad-code-XXXXXX";
  FILE *file = new_fixture(path);
  put_header(file, WIRE_VERSION);
  put_nil_record_prefix(file, "root", 1);
  put_code_record(file, "badcode", bytecode, bytecode_len, 0);
  assert_fixture_rejected(file, path);
  ASSERT_EQ_INT(0, unlink(path));
}

void test_load_itemstore_rejects_malformed_code_bytecode(void) {
  bool previous_strict_validation = config.strict_validation;
  config.strict_validation = true;

  const uint8_t missing_header[] = {'h'};
  assert_malformed_code_rejected(missing_header, sizeof(missing_header));

  const uint8_t missing_halt[] = {0, 0, 'b', 1};
  assert_malformed_code_rejected(missing_halt, sizeof(missing_halt));

  const uint8_t truncated_string_operand[] = {0, 0, 'l', 3, 0, 'a', 'b'};
  assert_malformed_code_rejected(truncated_string_operand,
                                 sizeof(truncated_string_operand));

  const uint8_t invalid_local_index[] = {0, 0, 'e', 0, 'h'};
  assert_malformed_code_rejected(invalid_local_index,
                                 sizeof(invalid_local_index));

  const uint8_t invalid_jump_target[] = {0, 0, 'j', 0x04, 0x00, 'h'};
  assert_malformed_code_rejected(invalid_jump_target,
                                 sizeof(invalid_jump_target));

  const uint8_t malformed_nested_item_expression[] = {
    0, 0, 'I', 'L', 4, 'x', 'E', 'h'
  };
  assert_malformed_code_rejected(malformed_nested_item_expression,
                                 sizeof(malformed_nested_item_expression));

  config.strict_validation = previous_strict_validation;
}

void test_load_itemstore_allows_malformed_code_when_strict_validation_disabled(
    void) {
  bool previous_strict_validation = config.strict_validation;
  config.strict_validation = false;

  char path[] = "/tmp/sin-itemstore-bad-code-disabled-XXXXXX";
  FILE *file = new_fixture(path);
  const uint8_t missing_halt[] = {0, 0, 'b', 1};
  put_header(file, WIRE_VERSION);
  put_nil_record_prefix(file, "root", 1);
  put_code_record(file, "badcode", missing_halt, sizeof(missing_halt), 0);
  ASSERT_EQ_INT(0, fclose(file));

  ITEM_t *loaded = load_itemstore(path);
  ASSERT_NOT_NULL(loaded);
  ITEM_t *badcode = find_item(loaded, "badcode");
  ASSERT_NOT_NULL(badcode);
  ASSERT_EQ_INT(ITEM_code, badcode->type);
  ASSERT_EQ_INT(sizeof(missing_halt), badcode->bytecode_len);

  destroy_item(loaded);
  ASSERT_EQ_INT(0, unlink(path));
  config.strict_validation = previous_strict_validation;
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
  put_u8(file, ITEM_MAX_LAYER_NAME_LENGTH + 1u);
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
  put_u32_le(file, (uint32_t)SIN_MAX_STRING_BYTES + 1u);
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
  ASSERT_NOT_NULL(test_item_set_value(original, "sentinel",
                              (VALUE_t){.type = VALUE_int, .i = 42}));
  ASSERT_TRUE(save_itemstore(path, original));
  destroy_item(original);

  ITEM_t *invalid = make_root_item("root");
  ASSERT_NOT_NULL(invalid);
  ASSERT_NOT_NULL(test_item_set_code(invalid, "invalid_code", 1, NULL));
  ASSERT_TRUE(!save_itemstore(path, invalid));
  destroy_item(invalid);

  ITEM_t *too_many_children = make_root_item("root");
  ASSERT_NOT_NULL(too_many_children);
  for (int i = 0; i < 251; i++) {
    char name[16];
    ASSERT_TRUE(snprintf(name, sizeof(name), "child_%03d", i) > 0);
    ASSERT_NOT_NULL(test_item_set_value(
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

void test_save_itemsource_reports_write_and_close_failure(void) {
  char srcroot[] = "/tmp/sin-itemsource-save-XXXXXX";
  ASSERT_NOT_NULL(mkdtemp(srcroot));
  ITEM_t *root = make_item("root", NULL, ITEM_code, VALUE_NIL, NULL, 0);
  ASSERT_NOT_NULL(root);
  char source[] = "persisted source\n";

  ASSERT_TRUE(save_itemsource_in_srcroot(root, source, srcroot));
  char *filename = get_itemfilename_in_srcroot(root, srcroot);
  ASSERT_NOT_NULL(filename);
  FILE *file = fopen(filename, "rb");
  ASSERT_NOT_NULL(file);
  char contents[32] = {0};
  ASSERT_EQ_INT(strlen(source), fread(contents, 1, sizeof contents - 1, file));
  ASSERT_TRUE(strcmp(source, contents) == 0);
  ASSERT_EQ_INT(0, fclose(file));

  source_write_hook_calls = 0;
  source_close_hook_calls = 0;
  itemstore_set_source_io_hooks_for_tests(fail_source_write,
                                          count_source_close);
  bool write_succeeded = save_itemsource_in_srcroot(root, source, srcroot);
  itemstore_set_source_io_hooks_for_tests(NULL, NULL);
  ASSERT_TRUE(!write_succeeded);
  ASSERT_EQ_INT(1, source_write_hook_calls);
  ASSERT_EQ_INT(1, source_close_hook_calls);

  source_close_hook_calls = 0;
  itemstore_set_source_io_hooks_for_tests(NULL, fail_source_close);
  bool close_succeeded = save_itemsource_in_srcroot(root, source, srcroot);
  itemstore_set_source_io_hooks_for_tests(NULL, NULL);
  ASSERT_TRUE(!close_succeeded);
  ASSERT_EQ_INT(1, source_close_hook_calls);

  ASSERT_EQ_INT(0, unlink(filename));
  char itemdir[sizeof srcroot + sizeof "/root"];
  ASSERT_TRUE(snprintf(itemdir, sizeof itemdir, "%s/root", srcroot) > 0);
  ASSERT_EQ_INT(0, rmdir(itemdir));
  free(filename);
  detach_item_and_destroy(root);
  ASSERT_EQ_INT(0, rmdir(srcroot));
}

void test_itemsource_paths_are_validated_and_contained(void) {
  char srcroot[] = "/tmp/sin-itemsource-path-XXXXXX";
  char outside[] = "/tmp/sin-itemsource-out-XXXXXX";
  char missing_srcroot[] = "/tmp/sin-itemsource-missing-XXXXXX";
  ASSERT_NOT_NULL(mkdtemp(srcroot));
  ASSERT_NOT_NULL(mkdtemp(outside));
  ASSERT_NOT_NULL(mkdtemp(missing_srcroot));
  ASSERT_EQ_INT(0, rmdir(missing_srcroot));

  ITEM_t *root = make_item("root", NULL, ITEM_code, VALUE_NIL, NULL, 0);
  ASSERT_NOT_NULL(root);
  ITEM_t *child = make_item("child", root, ITEM_code, VALUE_NIL, NULL, 0);
  ASSERT_NOT_NULL(child);
  ITEM_t *value_item = make_item("value", root, ITEM_value, VALUE_NIL, NULL, 0);
  ASSERT_NOT_NULL(value_item);
  char detail[128];
  char *root_file = get_itemfilename_in_srcroot(root, srcroot);
  char *child_file = get_itemfilename_in_srcroot(child, srcroot);
  ASSERT_NOT_NULL(root_file);
  ASSERT_NOT_NULL(child_file);
  ASSERT_TRUE(strstr(root_file, "/root/source.sin") != NULL);
  ASSERT_TRUE(strstr(child_file, "/child/source.sin") != NULL);
  free(root_file);
  free(child_file);

  ASSERT_TRUE(!save_itemsource_in_srcroot(value_item, "rejected\n", srcroot));
  char value_dir[sizeof srcroot + sizeof "/root/value"];
  ASSERT_TRUE(snprintf(value_dir, sizeof value_dir, "%s/root/value", srcroot)
              > 0);
  ASSERT_TRUE(access(value_dir, F_OK) != 0);
  ASSERT_TRUE(read_itemsource_in_srcroot(value_item, srcroot, detail,
                                         sizeof detail) == NULL);
  ASSERT_TRUE(strcmp("source item is not a code item", detail) == 0);

  ITEM_t *punctuation_root = make_item("context-root", NULL, ITEM_code,
                                       VALUE_NIL, NULL, 0);
  ASSERT_NOT_NULL(punctuation_root);
  char *punctuation_file = get_itemfilename_in_srcroot(punctuation_root,
                                                        srcroot);
  ASSERT_NOT_NULL(punctuation_file);
  char expected_punctuation_file[sizeof srcroot
                                 + sizeof "/context-root/source.sin"];
  ASSERT_TRUE(snprintf(expected_punctuation_file,
                       sizeof expected_punctuation_file,
                       "%s/context-root/source.sin", srcroot) > 0);
  ASSERT_TRUE(strcmp(expected_punctuation_file, punctuation_file) == 0);
  ASSERT_TRUE(save_itemsource_in_srcroot(punctuation_root,
                                         "punctuation source\n", srcroot));
  char *punctuation_source = read_itemsource_in_srcroot(
      punctuation_root, srcroot, detail, sizeof detail);
  ASSERT_NOT_NULL(punctuation_source);
  ASSERT_TRUE(strcmp("punctuation source\n", punctuation_source) == 0);
  free(punctuation_source);
  ASSERT_EQ_INT(0, unlink(punctuation_file));
  char punctuation_dir[sizeof srcroot + sizeof "/context-root"];
  ASSERT_TRUE(snprintf(punctuation_dir, sizeof punctuation_dir,
                       "%s/context-root", srcroot) > 0);
  ASSERT_EQ_INT(0, rmdir(punctuation_dir));
  free(punctuation_file);
  detach_item_and_destroy(punctuation_root);

  ASSERT_TRUE(get_itemfilename_in_srcroot(NULL, srcroot) == NULL);
  ASSERT_TRUE(get_itemfilename_in_srcroot(root, NULL) == NULL);
  ASSERT_TRUE(!save_itemsource_in_srcroot(NULL, "x", srcroot));
  ASSERT_TRUE(!save_itemsource_in_srcroot(root, NULL, srcroot));
  ASSERT_TRUE(!save_itemsource_in_srcroot(root, "x", NULL));
  ASSERT_TRUE(read_itemsource_in_srcroot(NULL, srcroot, detail,
                                         sizeof detail) == NULL);
  ASSERT_TRUE(read_itemsource_in_srcroot(root, NULL, detail,
                                         sizeof detail) == NULL);

  const char *bad_names[] = {"", "bad/name", "bad\\name", ".", "..",
                             "bad.name", ".bad", "bad."};
  for (size_t i = 0; i < sizeof bad_names / sizeof bad_names[0]; i++) {
    ITEM_t *bad = make_root_item(bad_names[i]);
    ASSERT_NOT_NULL(bad);
    ASSERT_TRUE(get_itemfilename_in_srcroot(bad, srcroot) == NULL);
    ASSERT_TRUE(!save_itemsource_in_srcroot(bad, "x", srcroot));
    destroy_item(bad);
  }

  CONFIG_t saved = config;
  config.srcroot = srcroot;
  ASSERT_TRUE(save_itemsource(root, "configured\n"));
  config = saved;

  char linkpath[sizeof srcroot + sizeof "/child"];
  ASSERT_TRUE(snprintf(linkpath, sizeof linkpath, "%s/child", srcroot) > 0);
  ASSERT_EQ_INT(0, symlink(outside, linkpath));
  ASSERT_TRUE(!save_itemsource_in_srcroot(child, "escape", srcroot));
  ASSERT_TRUE(read_itemsource_in_srcroot(child, srcroot, detail,
                                         sizeof detail) == NULL);
  ASSERT_EQ_INT(0, unlink(linkpath));
  char escaped_source[sizeof outside + sizeof "/source.sin"];
  ASSERT_TRUE(snprintf(escaped_source, sizeof escaped_source,
                       "%s/source.sin", outside) > 0);
  ASSERT_TRUE(access(escaped_source, F_OK) != 0);

  char child_dir[sizeof srcroot + sizeof "/child"];
  ASSERT_TRUE(snprintf(child_dir, sizeof child_dir, "%s/child", srcroot) > 0);
  ASSERT_EQ_INT(0, mkdir(child_dir, 0700));
  char child_source[sizeof child_dir + sizeof "/source.sin"];
  ASSERT_TRUE(snprintf(child_source, sizeof child_source, "%s/source.sin",
                       child_dir) > 0);
  char outside_source[sizeof outside + sizeof "/outside.sin"];
  ASSERT_TRUE(snprintf(outside_source, sizeof outside_source, "%s/outside.sin",
                       outside) > 0);
  FILE *outside_file = fopen(outside_source, "wb");
  ASSERT_NOT_NULL(outside_file);
  ASSERT_TRUE(fputs("outside sentinel\n", outside_file) >= 0);
  ASSERT_EQ_INT(0, fclose(outside_file));
  ASSERT_EQ_INT(0, symlink(outside_source, child_source));
  ASSERT_TRUE(!save_itemsource_in_srcroot(child, "overwrite\n", srcroot));
  ASSERT_TRUE(read_itemsource_in_srcroot(child, srcroot, detail,
                                         sizeof detail) == NULL);
  outside_file = fopen(outside_source, "rb");
  ASSERT_NOT_NULL(outside_file);
  char outside_contents[32] = {0};
  ASSERT_EQ_INT(strlen("outside sentinel\n"),
                fread(outside_contents, 1, sizeof outside_contents - 1,
                      outside_file));
  ASSERT_TRUE(strcmp("outside sentinel\n", outside_contents) == 0);
  ASSERT_EQ_INT(0, fclose(outside_file));

  ASSERT_TRUE(save_itemsource_in_srcroot(root, "created root\n",
                                         missing_srcroot));
  char missing_file[sizeof missing_srcroot + sizeof "/root/source.sin"];
  ASSERT_TRUE(snprintf(missing_file, sizeof missing_file,
                       "%s/root/source.sin", missing_srcroot) > 0);
  FILE *missing_source = fopen(missing_file, "rb");
  ASSERT_NOT_NULL(missing_source);
  char missing_contents[32] = {0};
  ASSERT_EQ_INT(strlen("created root\n"),
                fread(missing_contents, 1, sizeof missing_contents - 1,
                      missing_source));
  ASSERT_TRUE(strcmp("created root\n", missing_contents) == 0);
  ASSERT_EQ_INT(0, fclose(missing_source));

  char slash_item_dir[] = "/tmp/sin_itemsource_slash_XXXXXX";
  ASSERT_NOT_NULL(mkdtemp(slash_item_dir));
  ASSERT_EQ_INT(0, rmdir(slash_item_dir));
  const char *slash_leaf_name = strrchr(slash_item_dir, '/');
  ASSERT_NOT_NULL(slash_leaf_name);
  slash_leaf_name++;
  ITEM_t *slash_root = make_item("unused", NULL, ITEM_code, VALUE_NIL, NULL,
                                 0);
  ASSERT_NOT_NULL(slash_root);
  ITEM_t *slash_tmp = make_item("tmp", slash_root, ITEM_code, VALUE_NIL,
                                NULL, 0);
  ASSERT_NOT_NULL(slash_tmp);
  ITEM_t *slash_leaf = make_item(slash_leaf_name, slash_tmp, ITEM_code,
                                 VALUE_NIL, NULL, 0);
  ASSERT_NOT_NULL(slash_leaf);
  ASSERT_TRUE(save_itemsource_in_srcroot(slash_leaf, "slash root\n", "/"));
  char slash_file[sizeof slash_item_dir + sizeof "/source.sin"];
  ASSERT_TRUE(snprintf(slash_file, sizeof slash_file, "%s/source.sin",
                       slash_item_dir) > 0);
  FILE *slash_source = fopen(slash_file, "rb");
  ASSERT_NOT_NULL(slash_source);
  char slash_contents[32] = {0};
  ASSERT_EQ_INT(strlen("slash root\n"),
                fread(slash_contents, 1, sizeof slash_contents - 1,
                      slash_source));
  ASSERT_TRUE(strcmp("slash root\n", slash_contents) == 0);
  ASSERT_EQ_INT(0, fclose(slash_source));
  detach_item_and_destroy(slash_root);
  ASSERT_EQ_INT(0, unlink(slash_file));
  ASSERT_EQ_INT(0, rmdir(slash_item_dir));

  detach_item_and_destroy(root);
  ASSERT_EQ_INT(0, unlink(missing_file));
  char missing_dir[sizeof missing_srcroot + sizeof "/root"];
  ASSERT_TRUE(snprintf(missing_dir, sizeof missing_dir, "%s/root",
                       missing_srcroot) > 0);
  ASSERT_EQ_INT(0, rmdir(missing_dir));
  ASSERT_EQ_INT(0, rmdir(missing_srcroot));
  ASSERT_EQ_INT(0, unlink(child_source));
  ASSERT_EQ_INT(0, rmdir(child_dir));
  ASSERT_EQ_INT(0, unlink(outside_source));
  char saved_file[sizeof srcroot + sizeof "/root/source.sin"];
  ASSERT_TRUE(snprintf(saved_file, sizeof saved_file, "%s/root/source.sin",
                       srcroot) > 0);
  ASSERT_EQ_INT(0, unlink(saved_file));
  char saved_dir[sizeof srcroot + sizeof "/root"];
  ASSERT_TRUE(snprintf(saved_dir, sizeof saved_dir, "%s/root", srcroot) > 0);
  ASSERT_EQ_INT(0, rmdir(saved_dir));
  ASSERT_EQ_INT(0, rmdir(srcroot));
  ASSERT_EQ_INT(0, rmdir(outside));
}

void test_itemstore_durability_modes(void) {
  ASSERT_TRUE(itemstore_durability_requires_sync(ITEMSTORE_DURABLE_FULL));
  ASSERT_TRUE(!itemstore_durability_requires_sync(ITEMSTORE_DURABLE_FAST));

  char path[] = "/tmp/sin-itemstore-durability-XXXXXX";
  FILE *file = new_fixture(path);
  ASSERT_EQ_INT(0, fclose(file));
  ITEM_t *root = make_root_item("root");
  ASSERT_NOT_NULL(root);
  ASSERT_NOT_NULL(test_item_set_value(root, "persisted",
                              (VALUE_t){.type = VALUE_int, .i = 1}));

  // This deliberately targets the old predictable PID-shaped name. It
  // verifies that an unrelated pre-existing temporary file is preserved; it
  // does not force an mkstemp collision.
  char unrelated_temp_path[sizeof path + 32];
  ASSERT_TRUE(snprintf(unrelated_temp_path, sizeof unrelated_temp_path,
                       "%s.tmp.%ld",
                       path, (long)getpid()) > 0);
  int unrelated_temp_fd = open(unrelated_temp_path,
                               O_WRONLY | O_CREAT | O_EXCL, 0600);
  ASSERT_TRUE(unrelated_temp_fd >= 0);
  FILE *unrelated_temp_file = fdopen(unrelated_temp_fd, "wb");
  ASSERT_NOT_NULL(unrelated_temp_file);
  ASSERT_TRUE(fputs("pre-existing temporary data", unrelated_temp_file) >= 0);
  ASSERT_EQ_INT(0, fclose(unrelated_temp_file));

  sync_hook_calls = 0;
  directory_sync_hook_calls = 0;
  itemstore_set_sync_hook_for_tests(count_sync);
  itemstore_set_directory_sync_hook_for_tests(reject_directory_sync);

  config.itemstore_durability = ITEMSTORE_DURABLE_FAST;
  ASSERT_TRUE(save_itemstore(path, root));
  ASSERT_EQ_INT(0, sync_hook_calls);
  ASSERT_EQ_INT(0, directory_sync_hook_calls);

  unrelated_temp_file = fopen(unrelated_temp_path, "rb");
  ASSERT_NOT_NULL(unrelated_temp_file);
  char unrelated_temp_contents[sizeof "pre-existing temporary data"] = {0};
  ASSERT_EQ_INT(strlen("pre-existing temporary data"),
                fread(unrelated_temp_contents, 1,
                      sizeof unrelated_temp_contents - 1, unrelated_temp_file));
  ASSERT_TRUE(strcmp("pre-existing temporary data", unrelated_temp_contents)
              == 0);
  ASSERT_EQ_INT(0, fclose(unrelated_temp_file));
  ASSERT_EQ_INT(0, unlink(unrelated_temp_path));

  ASSERT_NOT_NULL(test_item_set_value(root, "not_persisted",
                              (VALUE_t){.type = VALUE_int, .i = 2}));

  config.itemstore_durability = ITEMSTORE_DURABLE_FULL;
  ASSERT_TRUE(!save_itemstore(path, root));
  ASSERT_EQ_INT(1, sync_hook_calls);
  ASSERT_EQ_INT(1, directory_sync_hook_calls);
  itemstore_set_sync_hook_for_tests(NULL);
  itemstore_set_directory_sync_hook_for_tests(NULL);

  ITEM_t *loaded = load_itemstore(path);
  ASSERT_NOT_NULL(loaded);
  ASSERT_NOT_NULL(find_item(loaded, "persisted"));
  ASSERT_NOT_NULL(find_item(loaded, "not_persisted"));
  destroy_item(loaded);

  char temp_pattern[sizeof path + 8];
  ASSERT_TRUE(snprintf(temp_pattern, sizeof temp_pattern, "%s.tmp.*", path)
              > 0);
  glob_t temp_matches = {0};
  int glob_result = glob(temp_pattern, 0, NULL, &temp_matches);
  ASSERT_TRUE(glob_result == 0 || glob_result == GLOB_NOMATCH);
  ASSERT_EQ_INT(0, temp_matches.gl_pathc);
  globfree(&temp_matches);

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
  ASSERT_EQ_INT(CHILD_COUNT, item_child_count(loaded));
  ASSERT_EQ_INT(CHILD_COUNT, item_children_ordered_capacity(loaded->children));
  ASSERT_EQ_INT(267, item_children_bucket_count(loaded->children));
  for (int i = 0; i < CHILD_COUNT; i++) {
    ITEM_t *child = item_child_at(loaded, (size_t)i);
    ASSERT_NOT_NULL(child);
    char expected[16];
    ASSERT_TRUE(snprintf(expected, sizeof(expected), "child_%03d", i) > 0);
    ASSERT_TRUE(strcmp(expected, child->name) == 0);
  }

  destroy_item(loaded);
  ASSERT_EQ_INT(0, unlink(path));
}
