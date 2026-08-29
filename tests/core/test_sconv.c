#include <glob.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "bytecode_verify.h"
#include "error.h"
#include "item_internal.h"
#include "item_persist_internal.h"
#include "itemref.h"
#include "list.h"
#include "test_assert.h"
#include "test_helpers.h"

static void temp_path(char *path, size_t size, const char *tag) {
  ASSERT_EQ_INT(0, test_temp_template(path, size, tag));
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

void test_sconv_v1_to_v2_migrates_legacy_code(void) {
  char input[4096], output[4096];
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
  write_u32(f, 6);
  ASSERT_EQ_INT(6, fwrite("\1\0b\1Qh", 1, 6, f));
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
  ASSERT_EQ_INT(12, program->bytecode_len);
  ASSERT_TRUE(memcmp(program->bytecode, "\0\xffSB\1\0\1\0b\1Qh", 12) == 0);
  destroy_item(loaded);
  ASSERT_EQ_INT(0, unlink(input));
  ASSERT_EQ_INT(0, unlink(output));
}

void test_sconv_conversion_work_budget_is_atomic(void) {
  char input[4096], output[4096];
  temp_path(input, sizeof input, "sconv-budget");
  temp_path(output, sizeof output, "sconv-budget-out");
  ASSERT_EQ_INT(512u * 1024u * 1024u, ITEMSTORE_MAX_CONVERSION_BYTES);
  FILE *f = fopen(input, "wb");
  ASSERT_NOT_NULL(f);
  ASSERT_EQ_INT(8, fwrite("SINITEM\0", 1, 8, f));
  write_u16(f, 1);
  put_u8(f, 4);
  ASSERT_EQ_INT(4, fwrite("root", 1, 4, f));
  put_u8(f, 1);
  put_u8(f, 0);
  write_u64(f, 42);
  write_u32(f, 0);
  ASSERT_EQ_INT(0, fclose(f));
  f = fopen(output, "wb");
  ASSERT_NOT_NULL(f);
  ASSERT_EQ_INT(8, fwrite("sentinel", 1, 8, f));
  ASSERT_EQ_INT(0, fclose(f));
  ASSERT_EQ_INT(ITEMSTORE_CONVERT_FAILURE,
                itemstore_convert_with_limits(input, output,
                                              ITEMSTORE_DURABLE_FAST, true,
                                              ITEMSTORE_MAX_RECORDS,
                                              ITEMSTORE_MAX_DECODE_BYTES, 63));
  f = fopen(output, "rb");
  ASSERT_NOT_NULL(f);
  char sentinel[8];
  ASSERT_EQ_INT(sizeof sentinel, fread(sentinel, 1, sizeof sentinel, f));
  ASSERT_TRUE(memcmp(sentinel, "sentinel", sizeof sentinel) == 0);
  ASSERT_EQ_INT(0, fclose(f));
  ASSERT_EQ_INT(0, unlink(input));
  ASSERT_EQ_INT(0, unlink(output));
}

void test_sconv_decode_budget_failures_are_atomic(void) {
  char input[4096], output[4096];
  temp_path(input, sizeof input, "sconv-decode-budget-v1");
  temp_path(output, sizeof output, "sconv-decode-budget-out");
  FILE *f = fopen(input, "wb");
  ASSERT_NOT_NULL(f);
  ASSERT_EQ_INT(8, fwrite("SINITEM\0", 1, 8, f));
  write_u16(f, 1);
  put_u8(f, 4);
  ASSERT_EQ_INT(4, fwrite("root", 1, 4, f));
  put_u8(f, 1);
  put_u8(f, 0);
  write_u64(f, 42);
  write_u32(f, 0);
  ASSERT_EQ_INT(0, fclose(f));
  f = fopen(output, "wb");
  ASSERT_NOT_NULL(f);
  ASSERT_EQ_INT(8, fwrite("sentinel", 1, 8, f));
  ASSERT_EQ_INT(0, fclose(f));
  ASSERT_EQ_INT(ITEMSTORE_CONVERT_FAILURE,
                itemstore_convert_with_limits(
                    input, output, ITEMSTORE_DURABLE_FAST, true,
                    ITEMSTORE_MAX_RECORDS, 0,
                    ITEMSTORE_MAX_CONVERSION_BYTES));
  ASSERT_EQ_INT(ITEMSTORE_CONVERT_FAILURE,
                itemstore_convert_with_limits(
                    input, output, ITEMSTORE_DURABLE_FAST, true, 0,
                    ITEMSTORE_MAX_DECODE_BYTES,
                    ITEMSTORE_MAX_CONVERSION_BYTES));
  f = fopen(output, "rb");
  ASSERT_NOT_NULL(f);
  char sentinel[8];
  ASSERT_EQ_INT(sizeof sentinel, fread(sentinel, 1, sizeof sentinel, f));
  ASSERT_TRUE(memcmp(sentinel, "sentinel", sizeof sentinel) == 0);
  ASSERT_EQ_INT(0, fclose(f));
  ASSERT_EQ_INT(0, unlink(input));
  ASSERT_EQ_INT(0, unlink(output));

  temp_path(input, sizeof input, "sconv-decode-budget-v2");
  temp_path(output, sizeof output, "sconv-decode-budget-v2-out");
  ITEM_t *root = make_root_item("root");
  ASSERT_NOT_NULL(root);
  ASSERT_TRUE(save_itemstore_with_options(input, root,
                                          ITEMSTORE_DURABLE_FAST));
  destroy_item(root);
  f = fopen(output, "wb");
  ASSERT_NOT_NULL(f);
  ASSERT_EQ_INT(8, fwrite("sentinel", 1, 8, f));
  ASSERT_EQ_INT(0, fclose(f));
  ASSERT_EQ_INT(ITEMSTORE_CONVERT_FAILURE,
                itemstore_convert_with_limits(
                    input, output, ITEMSTORE_DURABLE_FAST, true,
                    ITEMSTORE_MAX_RECORDS, 0,
                    ITEMSTORE_MAX_CONVERSION_BYTES));
  ASSERT_EQ_INT(ITEMSTORE_CONVERT_FAILURE,
                itemstore_convert_with_limits(
                    input, output, ITEMSTORE_DURABLE_FAST, true, 0,
                    ITEMSTORE_MAX_DECODE_BYTES,
                    ITEMSTORE_MAX_CONVERSION_BYTES));
  f = fopen(output, "rb");
  ASSERT_NOT_NULL(f);
  ASSERT_EQ_INT(sizeof sentinel, fread(sentinel, 1, sizeof sentinel, f));
  ASSERT_TRUE(memcmp(sentinel, "sentinel", sizeof sentinel) == 0);
  ASSERT_EQ_INT(0, fclose(f));
  ASSERT_EQ_INT(0, unlink(input));
  ASSERT_EQ_INT(0, unlink(output));
}

void test_sconv_v1_embedded_nul_warns_with_full_path(void) {
  char input[4096], output[4096];
  temp_path(input, sizeof input, "sconv-nul");
  temp_path(output, sizeof output, "sconv-nul-out");
  FILE *f = fopen(input, "wb");
  ASSERT_NOT_NULL(f);
  ASSERT_EQ_INT(8, fwrite("SINITEM\0", 1, 8, f));
  write_u16(f, 1);
  put_u8(f, 4);
  ASSERT_EQ_INT(4, fwrite("root", 1, 4, f));
  put_u8(f, 1);
  put_u8(f, 3);
  write_u32(f, 1);
  put_u8(f, 5);
  ASSERT_EQ_INT(5, fwrite("layer", 1, 5, f));
  put_u8(f, 1);
  put_u8(f, 3);
  write_u32(f, 1);
  put_u8(f, 4);
  ASSERT_EQ_INT(4, fwrite("leaf", 1, 4, f));
  put_u8(f, 1);
  put_u8(f, 2);
  write_u32(f, 5);
  ASSERT_EQ_INT(5, fwrite("ab\0cd", 1, 5, f));
  write_u32(f, 0);
  ASSERT_EQ_INT(0, fclose(f));

  char *argv[] = {TEST_SCONV, "-q", input, output, NULL};
  TestProcessResult result = {0};
  ASSERT_EQ_INT(0, test_run_argv_capture(argv, 0, &result));
  ASSERT_EQ_INT(0, result.exit_code);
  ASSERT_TRUE(strstr(result.stderr_text, "layer.leaf") != NULL);
  ASSERT_TRUE(strstr(result.stderr_text, "embedded NUL") != NULL);
  test_process_result_free(&result);
  ITEM_t *root = load_itemstore_with_options(output, false);
  ASSERT_NOT_NULL(root);
  ITEM_t *child = find_item(root, "layer.leaf");
  ASSERT_NOT_NULL(child);
  ASSERT_EQ_INT(VALUE_str, child->value.type);
  ASSERT_TRUE(strcmp(child->value.s, "ab") == 0);
  destroy_item(root);
  f = fopen(output, "wb");
  ASSERT_NOT_NULL(f);
  ASSERT_EQ_INT(8, fwrite("sentinel", 1, 8, f));
  ASSERT_EQ_INT(0, fclose(f));
  char *refuse_argv[] = {TEST_SCONV, input, output, NULL};
  TestProcessResult refused = {0};
  ASSERT_EQ_INT(0, test_run_argv_capture(refuse_argv, 0, &refused));
  ASSERT_EQ_INT(1, refused.exit_code);
  ASSERT_TRUE(strstr(refused.stderr_text, "embedded NUL") == NULL);
  test_process_result_free(&refused);
  f = fopen(output, "rb");
  ASSERT_NOT_NULL(f);
  char sentinel[8];
  ASSERT_EQ_INT(8, fread(sentinel, 1, 8, f));
  ASSERT_EQ_INT(0, fclose(f));
  ASSERT_TRUE(memcmp(sentinel, "sentinel", 8) == 0);
  ASSERT_EQ_INT(0, unlink(input));
  ASSERT_EQ_INT(0, unlink(output));
}

void test_sconv_mixed_code_tree_and_failure_atomicity(void) {
  char input[4096], output[4096];
  temp_path(input, sizeof input, "sconv-mixed");
  temp_path(output, sizeof output, "sconv-mixed-out");
  const uint8_t legacy[] = {1, 0, 'b', 1, 'Q', 'h'};
  const uint8_t v1[] = {0, 0xff, 'S', 'B', 1, 0, 1, 0, 'b', 1, 'Q', 'h'};
  ITEM_t *source = make_root_item("root");
  ASSERT_NOT_NULL(source);
  uint8_t *legacy_copy = malloc(sizeof legacy);
  uint8_t *v1_copy = malloc(sizeof v1);
  ASSERT_NOT_NULL(legacy_copy);
  ASSERT_NOT_NULL(v1_copy);
  memcpy(legacy_copy, legacy, sizeof legacy);
  memcpy(v1_copy, v1, sizeof v1);
  ASSERT_NOT_NULL(make_item("legacy", source, ITEM_code, VALUE_NIL, legacy_copy,
                            sizeof legacy));
  ASSERT_NOT_NULL(
      make_item("versioned", source, ITEM_code, VALUE_NIL, v1_copy, sizeof v1));
  ASSERT_TRUE(
      save_itemstore_with_options(input, source, ITEMSTORE_DURABLE_FAST));
  destroy_item(source);
  ASSERT_EQ_INT(
      ITEMSTORE_CONVERT_SUCCESS,
      itemstore_convert(input, output, ITEMSTORE_DURABLE_FAST, false));
  ITEM_t *root = load_itemstore_with_options(output, false);
  ASSERT_NOT_NULL(root);
  ITEM_t *legacy_item = find_item(root, "legacy");
  ITEM_t *versioned_item = find_item(root, "versioned");
  ASSERT_NOT_NULL(legacy_item);
  ASSERT_NOT_NULL(versioned_item);
  ASSERT_EQ_INT(12, legacy_item->bytecode_len);
  ASSERT_EQ_INT(12, versioned_item->bytecode_len);
  ASSERT_TRUE(memcmp(legacy_item->bytecode, versioned_item->bytecode, 12) == 0);
  ASSERT_EQ_INT(BC_VERIFY_OK,
                bc_verify_executable_bytecode(legacy_item->bytecode,
                                              legacy_item->bytecode_len,
                                              "legacy output").status);
  ASSERT_EQ_INT(BC_VERIFY_OK,
                bc_verify_executable_bytecode(versioned_item->bytecode,
                                              versioned_item->bytecode_len,
                                              "v1 output").status);
  destroy_item(root);

  FILE *f = fopen(output, "wb");
  ASSERT_NOT_NULL(f);
  ASSERT_EQ_INT(8, fwrite("sentinel", 1, 8, f));
  ASSERT_EQ_INT(0, fclose(f));
  /* An unrecognized opcode in one sibling must not publish partial results. */
  source = make_root_item("root");
  ASSERT_NOT_NULL(source);
  legacy_copy = malloc(sizeof legacy);
  ASSERT_NOT_NULL(legacy_copy);
  memcpy(legacy_copy, legacy, sizeof legacy);
  uint8_t *invalid = malloc(3);
  ASSERT_NOT_NULL(invalid);
  memcpy(invalid, "!h\0", 3);
  ASSERT_NOT_NULL(
      make_item("invalid", source, ITEM_code, VALUE_NIL, invalid, 2));
  /* Invalid is inserted first so LIFO traversal processes valid first and
   * proves prepared successful data is discarded after the later failure. */
  ASSERT_NOT_NULL(make_item("valid", source, ITEM_code, VALUE_NIL, legacy_copy,
                            sizeof legacy));
  ASSERT_TRUE(
      save_itemstore_with_options(input, source, ITEMSTORE_DURABLE_FAST));
  destroy_item(source);
  ASSERT_EQ_INT(ITEMSTORE_CONVERT_FAILURE,
                itemstore_convert(input, output, ITEMSTORE_DURABLE_FAST, true));
  f = fopen(output, "rb");
  ASSERT_NOT_NULL(f);
  char sentinel[8];
  ASSERT_EQ_INT(8, fread(sentinel, 1, 8, f));
  ASSERT_EQ_INT(0, fclose(f));
  ASSERT_TRUE(memcmp(sentinel, "sentinel", 8) == 0);
  ASSERT_EQ_INT(0, unlink(input));
  ASSERT_EQ_INT(0, unlink(output));
}

void test_sconv_v2_canonical_and_invocation_modes(void) {
  char input[4096], output[4096], output2[4096], output3[4096];
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
  uint8_t *code = malloc(9);
  ASSERT_NOT_NULL(code);
  memcpy(code, "\0\xffSB\1\0\0\0h", 9);
  ASSERT_NOT_NULL(make_item("program", root, ITEM_code, VALUE_NIL, code, 9));
  ASSERT_TRUE(save_itemstore_with_options(input, root, ITEMSTORE_DURABLE_FAST));
  destroy_item(root);
  char *missing_argv[] = {TEST_SCONV, "-i", input, NULL};
  TestProcessResult missing = {0};
  ASSERT_EQ_INT(0, test_run_argv_capture(missing_argv, 0, &missing));
  ASSERT_EQ_INT(1, missing.exit_code);
  test_process_result_free(&missing);
  char *mixed_argv[] = {TEST_SCONV, "-i", input, output2, NULL};
  TestProcessResult mixed = {0};
  ASSERT_EQ_INT(0, test_run_argv_capture(mixed_argv, 0, &mixed));
  ASSERT_EQ_INT(1, mixed.exit_code);
  test_process_result_free(&mixed);
  char *bad_d_argv[] = {TEST_SCONV, "-d", "bogus", input, output2, NULL};
  TestProcessResult bad_d = {0};
  ASSERT_EQ_INT(0, test_run_argv_capture(bad_d_argv, 0, &bad_d));
  ASSERT_EQ_INT(1, bad_d.exit_code);
  test_process_result_free(&bad_d);
  ASSERT_EQ_INT(
      ITEMSTORE_CONVERT_SUCCESS,
      itemstore_convert(input, output, ITEMSTORE_DURABLE_FAST, false));
  char *pos_argv[] = {TEST_SCONV, "-q", input, output2, NULL};
  TestProcessResult pos = {0};
  ASSERT_EQ_INT(0, test_run_argv_capture(pos_argv, 0, &pos));
  ASSERT_EQ_INT(0, pos.exit_code);
  ASSERT_EQ_INT(0, (int)pos.stdout_length);
  ASSERT_EQ_INT(0, (int)pos.stderr_length);
  test_process_result_free(&pos);
  char *opt_argv[] = {TEST_SCONV, "-v", "-d",    "fast", "-i",
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
  char input[4096], output[4096], alias[4096];
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
  char *refuse_argv[] = {TEST_SCONV, input, output, NULL};
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
  char *replace_argv[] = {TEST_SCONV, "--replace", input, output, NULL};
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
  char input[4096], output[4096];
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
  char *unknown_argv[] = {TEST_SCONV, input, output, NULL};
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
  char fast_output[4096];
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
  char temp_pattern[sizeof output + 8];
  ASSERT_TRUE(snprintf(temp_pattern, sizeof(temp_pattern), "%s.tmp.*",
                       output) > 0);
  glob_t temp_matches = {0};
  int glob_result = glob(temp_pattern, 0, NULL, &temp_matches);
  ASSERT_TRUE(glob_result == 0 || glob_result == GLOB_NOMATCH);
  ASSERT_EQ_INT(0, temp_matches.gl_pathc);
  globfree(&temp_matches);
  ASSERT_EQ_INT(0, unlink(input));
  ASSERT_EQ_INT(0, unlink(output));
  ASSERT_EQ_INT(0, unlink(fast_output));
}
