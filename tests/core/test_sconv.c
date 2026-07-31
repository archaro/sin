#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "error.h"
#include "item_internal.h"
#include "itemref.h"
#include "list.h"
#include "test_assert.h"
#include "test_helpers.h"

static void temp_path(char *path, size_t size, const char *tag) {
  int length = snprintf(path, size, "/tmp/%s-XXXXXX", tag);
  ASSERT_TRUE(length > 0 && (size_t)length < size);
  int fd = mkstemp(path);
  ASSERT_TRUE(fd >= 0);
  ASSERT_EQ_INT(0, close(fd));
  ASSERT_EQ_INT(0, unlink(path));
}

static void put_u8(FILE *f, uint8_t v) { ASSERT_EQ_INT((int)v, fputc(v, f)); }
static void write_u16(FILE *f, uint16_t v) {
  put_u8(f, (uint8_t)(v & 255u));
  put_u8(f, (uint8_t)(v >> 8));
}
static void write_u32(FILE *f, uint32_t v) {
  for (unsigned i = 0; i < 4; i++)
    put_u8(f, (uint8_t)(v >> (8u * i)));
}
static void write_u64(FILE *f, uint64_t v) {
  for (unsigned i = 0; i < 8; i++)
    put_u8(f, (uint8_t)(v >> (8u * i)));
}
static int sync_calls;
static bool fail_sync(FILE *file, const char *path) {
  (void)file;
  (void)path;
  sync_calls++;
  return false;
}
static void write_v1_value(FILE *f, const char *name, int64_t value) {
  put_u8(f, (uint8_t)strlen(name));
  ASSERT_EQ_INT((int)strlen(name), fwrite(name, 1, strlen(name), f));
  put_u8(f, 1);
  put_u8(f, 0);
  write_u64(f, (uint64_t)value);
  write_u32(f, 0);
}

void test_sconv_v1_to_v2_preserves_code(void) {
  char input[64], output[64];
  temp_path(input, sizeof input, "sconv-v1");
  temp_path(output, sizeof output, "sconv-out");
  FILE *f = fopen(input, "wb");
  ASSERT_NOT_NULL(f);
  ASSERT_EQ_INT(8, fwrite("SINITEM\0", 1, 8, f));
  write_u16(f, 1);
  put_u8(f, 4);
  ASSERT_EQ_INT(4, fwrite("root", 1, 4, f));
  put_u8(f, 1);
  put_u8(f, 0);
  write_u64(f, 42);
  write_u32(f, 2);
  write_v1_value(f, "first", 7);
  put_u8(f, 7);
  ASSERT_EQ_INT(7, fwrite("program", 1, 7, f));
  put_u8(f, 2);
  write_u32(f, 3);
  ASSERT_EQ_INT(3, fwrite("\0\0X", 1, 3, f));
  write_u32(f, 0);
  ASSERT_EQ_INT(0, fclose(f));
  ASSERT_EQ_INT(
      ITEMSTORE_CONVERT_SUCCESS,
      itemstore_convert(input, output, ITEMSTORE_DURABLE_FAST, false));
  f = fopen(output, "rb");
  ASSERT_NOT_NULL(f);
  ASSERT_EQ_INT(0, fseek(f, 8, SEEK_SET));
  ASSERT_EQ_INT(2, fgetc(f));
  ASSERT_EQ_INT(0, fgetc(f));
  ASSERT_EQ_INT(0, fclose(f));
  ITEM_t *loaded = load_itemstore_with_options(output, false);
  ASSERT_NOT_NULL(loaded);
  ASSERT_TRUE(strcmp(loaded->name, "root") == 0);
  ASSERT_EQ_INT(VALUE_int, loaded->value.type);
  ASSERT_EQ_INT(42, loaded->value.i);
  ASSERT_EQ_INT(2, (int)item_children_count(loaded->children));
  ITEM_t *first = item_children_at(loaded->children, 0);
  ITEM_t *program = item_children_at(loaded->children, 1);
  ASSERT_NOT_NULL(first);
  ASSERT_NOT_NULL(program);
  ASSERT_TRUE(strcmp(first->name, "first") == 0);
  ASSERT_EQ_INT(VALUE_int, first->value.type);
  ASSERT_EQ_INT(7, first->value.i);
  ASSERT_TRUE(strcmp(program->name, "program") == 0);
  ASSERT_EQ_INT(3, program->bytecode_len);
  ASSERT_TRUE(memcmp(program->bytecode, "\0\0X", 3) == 0);
  destroy_item(loaded);
  ASSERT_EQ_INT(0, unlink(input));
  ASSERT_EQ_INT(0, unlink(output));
}

void test_sconv_v2_canonical_and_invocation_modes(void) {
  char input[64], output[64], output2[64], output3[64];
  temp_path(input, sizeof input, "sconv-in");
  temp_path(output, sizeof output, "sconv-out");
  temp_path(output2, sizeof output2, "sconv-out2");
  temp_path(output3, sizeof output3, "sconv-out3");
  ITEM_t *root = make_root_item("root");
  ASSERT_NOT_NULL(root);
  ASSERT_NOT_NULL(make_item("target", root, ITEM_value,
                            (VALUE_t){.type = VALUE_int, .i = 9}, NULL, 0));
  VALUE_t elements[2] = {
      {.type = VALUE_int, .i = 4},
      {.type = VALUE_itemref, .itemref = sin_itemref_create("target")}};
  ASSERT_NOT_NULL(elements[1].itemref);
  SIN_LIST_t *list = sin_list_build_owned(elements, 2);
  ASSERT_NOT_NULL(list);
  ASSERT_NOT_NULL(make_item("payload", root, ITEM_value,
                            (VALUE_t){.type = VALUE_list, .list = list}, NULL,
                            0));
  uint8_t *code = malloc(3);
  ASSERT_NOT_NULL(code);
  memcpy(code, "\0\0Z", 3);
  ASSERT_NOT_NULL(make_item("program", root, ITEM_code, VALUE_NIL, code, 3));
  ASSERT_TRUE(save_itemstore_with_options(input, root, ITEMSTORE_DURABLE_FAST));
  destroy_item(root);
  char *missing_argv[] = {"./sconv", "-i", input, NULL};
  TestProcessResult missing = {0};
  ASSERT_EQ_INT(0, test_run_argv_capture(missing_argv, 0, &missing));
  ASSERT_EQ_INT(1, missing.exit_code);
  test_process_result_free(&missing);
  char *mixed_argv[] = {"./sconv", "-i", input, output2, NULL};
  TestProcessResult mixed = {0};
  ASSERT_EQ_INT(0, test_run_argv_capture(mixed_argv, 0, &mixed));
  ASSERT_EQ_INT(1, mixed.exit_code);
  test_process_result_free(&mixed);
  char *bad_d_argv[] = {"./sconv", "-d", "bogus", input, output2, NULL};
  TestProcessResult bad_d = {0};
  ASSERT_EQ_INT(0, test_run_argv_capture(bad_d_argv, 0, &bad_d));
  ASSERT_EQ_INT(1, bad_d.exit_code);
  test_process_result_free(&bad_d);
  ASSERT_EQ_INT(
      ITEMSTORE_CONVERT_SUCCESS,
      itemstore_convert(input, output, ITEMSTORE_DURABLE_FAST, false));
  char *pos_argv[] = {"./sconv", "-q", input, output2, NULL};
  TestProcessResult pos = {0};
  ASSERT_EQ_INT(0, test_run_argv_capture(pos_argv, 0, &pos));
  ASSERT_EQ_INT(0, pos.exit_code);
  ASSERT_EQ_INT(0, (int)pos.stdout_length);
  ASSERT_EQ_INT(0, (int)pos.stderr_length);
  test_process_result_free(&pos);
  char *opt_argv[] = {"./sconv", "-v", "-d",    "fast", "-i",
                      input,     "-o", output3, NULL};
  TestProcessResult opt = {0};
  ASSERT_EQ_INT(0, test_run_argv_capture(opt_argv, 0, &opt));
  ASSERT_EQ_INT(0, opt.exit_code);
  ASSERT_TRUE(strstr(opt.stderr_text,
                     "Conversion options: durability=fast replace=no.") !=
              NULL);
  test_process_result_free(&opt);
  FILE *a = fopen(input, "rb"), *b = fopen(output, "rb");
  ASSERT_NOT_NULL(a);
  ASSERT_NOT_NULL(b);
  int ca, cb;
  do {
    ca = fgetc(a);
    cb = fgetc(b);
    ASSERT_EQ_INT(ca, cb);
  } while (ca != EOF);
  ASSERT_EQ_INT(0, fclose(a));
  ASSERT_EQ_INT(0, fclose(b));
  ASSERT_EQ_INT(0, unlink(input));
  ASSERT_EQ_INT(0, unlink(output));
  ASSERT_EQ_INT(0, unlink(output2));
  ASSERT_EQ_INT(0, unlink(output3));
}

void test_sconv_collisions_aliases_and_replace(void) {
  char input[64], output[64], alias[64];
  temp_path(input, sizeof input, "sconv-in");
  temp_path(output, sizeof output, "sconv-out");
  temp_path(alias, sizeof alias, "sconv-alias");
  ITEM_t *root = make_root_item("root");
  ASSERT_NOT_NULL(root);
  ASSERT_TRUE(save_itemstore_with_options(input, root, ITEMSTORE_DURABLE_FAST));
  destroy_item(root);
  ASSERT_EQ_INT(ITEMSTORE_CONVERT_SAME_FILE,
                itemstore_convert(input, input, ITEMSTORE_DURABLE_FAST, false));
  ASSERT_EQ_INT(0, link(input, alias));
  ASSERT_EQ_INT(ITEMSTORE_CONVERT_SAME_FILE,
                itemstore_convert(input, alias, ITEMSTORE_DURABLE_FAST, false));
  ASSERT_EQ_INT(
      ITEMSTORE_CONVERT_SUCCESS,
      itemstore_convert(input, output, ITEMSTORE_DURABLE_FAST, false));
  ASSERT_EQ_INT(
      ITEMSTORE_CONVERT_TARGET_EXISTS,
      itemstore_convert(input, output, ITEMSTORE_DURABLE_FAST, false));
  FILE *sentinel_file = fopen(output, "wb");
  ASSERT_NOT_NULL(sentinel_file);
  ASSERT_EQ_INT(8, fwrite("sentinel", 1, 8, sentinel_file));
  ASSERT_EQ_INT(0, fclose(sentinel_file));
  char *refuse_argv[] = {"./sconv", input, output, NULL};
  TestProcessResult refused = {0};
  ASSERT_EQ_INT(0, test_run_argv_capture(refuse_argv, 0, &refused));
  ASSERT_EQ_INT(1, refused.exit_code);
  test_process_result_free(&refused);
  sentinel_file = fopen(output, "rb");
  ASSERT_NOT_NULL(sentinel_file);
  char sentinel[8];
  ASSERT_EQ_INT(8, fread(sentinel, 1, 8, sentinel_file));
  ASSERT_EQ_INT(0, fclose(sentinel_file));
  ASSERT_TRUE(memcmp(sentinel, "sentinel", 8) == 0);
  char *replace_argv[] = {"./sconv", "--replace", input, output, NULL};
  TestProcessResult replaced = {0};
  ASSERT_EQ_INT(0, test_run_argv_capture(replace_argv, 0, &replaced));
  ASSERT_EQ_INT(0, replaced.exit_code);
  test_process_result_free(&replaced);
  ITEM_t *reloaded = load_itemstore_with_options(output, false);
  ASSERT_NOT_NULL(reloaded);
  destroy_item(reloaded);
  ASSERT_EQ_INT(0, unlink(input));
  ASSERT_EQ_INT(0, unlink(output));
  ASSERT_EQ_INT(0, unlink(alias));
}

void test_sconv_rejects_bad_inputs_and_durability_failure(void) {
  char input[64], output[64];
  temp_path(input, sizeof input, "sconv-bad");
  temp_path(output, sizeof output, "sconv-out");
  FILE *f = fopen(input, "wb");
  ASSERT_NOT_NULL(f);
  ASSERT_EQ_INT(16, fprintf(f, "not an itemstore"));
  ASSERT_EQ_INT(0, fclose(f));
  ASSERT_EQ_INT(
      ITEMSTORE_CONVERT_FAILURE,
      itemstore_convert(input, output, ITEMSTORE_DURABLE_FAST, false));
  f = fopen(input, "wb");
  ASSERT_NOT_NULL(f);
  ASSERT_EQ_INT(8, fwrite("SINITEM\0", 1, 8, f));
  write_u16(f, 99);
  ASSERT_EQ_INT(0, fclose(f));
  ASSERT_EQ_INT(
      ITEMSTORE_CONVERT_FAILURE,
      itemstore_convert(input, output, ITEMSTORE_DURABLE_FAST, false));
  char *unknown_argv[] = {"./sconv", input, output, NULL};
  TestProcessResult unknown = {0};
  ASSERT_EQ_INT(0, test_run_argv_capture(unknown_argv, 0, &unknown));
  ASSERT_EQ_INT(1, unknown.exit_code);
  ASSERT_TRUE(strstr(unknown.stderr_text, "Unsupported itemstore version") !=
              NULL);
  test_process_result_free(&unknown);
  f = fopen(input, "wb");
  ASSERT_NOT_NULL(f);
  ASSERT_EQ_INT(8, fwrite("SINITEM\0", 1, 8, f));
  ASSERT_EQ_INT(0, fclose(f));
  ASSERT_EQ_INT(
      ITEMSTORE_CONVERT_FAILURE,
      itemstore_convert(input, output, ITEMSTORE_DURABLE_FAST, false));
  ITEM_t *root = make_root_item("root");
  ASSERT_NOT_NULL(root);
  ASSERT_TRUE(save_itemstore_with_options(input, root, ITEMSTORE_DURABLE_FAST));
  destroy_item(root);
  f = fopen(output, "wb");
  ASSERT_NOT_NULL(f);
  ASSERT_EQ_INT(8, fwrite("sentinel", 1, 8, f));
  ASSERT_EQ_INT(0, fclose(f));
  char fast_output[64];
  temp_path(fast_output, sizeof fast_output, "sconv-fast");
  sync_calls = 0;
  itemstore_set_sync_hook_for_tests(fail_sync);
  ITEMSTORE_CONVERT_RESULT_e full_result =
      itemstore_convert(input, output, ITEMSTORE_DURABLE_FULL, true);
  int calls_after_full = sync_calls;
  ITEMSTORE_CONVERT_RESULT_e fast_result =
      itemstore_convert(input, fast_output, ITEMSTORE_DURABLE_FAST, true);
  int calls_after_fast = sync_calls;
  itemstore_set_sync_hook_for_tests(NULL);
  ASSERT_EQ_INT(ITEMSTORE_CONVERT_FAILURE, full_result);
  ASSERT_EQ_INT(1, calls_after_full);
  ASSERT_EQ_INT(ITEMSTORE_CONVERT_SUCCESS, fast_result);
  ASSERT_EQ_INT(1, calls_after_fast);
  f = fopen(output, "rb");
  ASSERT_NOT_NULL(f);
  char sentinel[8];
  ASSERT_EQ_INT(8, fread(sentinel, 1, 8, f));
  ASSERT_EQ_INT(0, fclose(f));
  ASSERT_TRUE(memcmp(sentinel, "sentinel", 8) == 0);
  ASSERT_EQ_INT(0, unlink(input));
  ASSERT_EQ_INT(0, unlink(output));
  ASSERT_EQ_INT(0, unlink(fast_output));
}
