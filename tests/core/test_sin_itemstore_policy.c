#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>
#include <sys/stat.h>

#include "item.h"
#include "test_assert.h"
#include "test_helpers.h"

static void write_bytes(const char *path, const uint8_t *bytes, size_t length) {
  FILE *file = fopen(path, "wb");
  ASSERT_NOT_NULL(file);
  ASSERT_EQ_INT((int)length, (int)fwrite(bytes, 1, length, file));
  ASSERT_EQ_INT(0, fclose(file));
}

static uint8_t *read_bytes(const char *path, size_t *length_out) {
  FILE *file = fopen(path, "rb");
  ASSERT_NOT_NULL(file);
  ASSERT_EQ_INT(0, fseek(file, 0, SEEK_END));
  long size = ftell(file);
  ASSERT_TRUE(size >= 0);
  ASSERT_EQ_INT(0, fseek(file, 0, SEEK_SET));
  uint8_t *bytes = malloc((size_t)size);
  ASSERT_TRUE(size == 0 || bytes != NULL);
  ASSERT_EQ_INT((int)size, (int)fread(bytes, 1, (size_t)size, file));
  ASSERT_EQ_INT(0, fclose(file));
  *length_out = (size_t)size;
  return bytes;
}

static void assert_unchanged(const char *path, const uint8_t *expected,
                             size_t expected_length) {
  size_t actual_length = 0;
  uint8_t *actual = read_bytes(path, &actual_length);
  ASSERT_EQ_INT((int)expected_length, (int)actual_length);
  ASSERT_TRUE(memcmp(expected, actual, expected_length) == 0);
  free(actual);
}

static void run_rejected(const char *path, const uint8_t *bytes, size_t length,
                         char *const argv[], const char *diagnostic) {
  write_bytes(path, bytes, length);
  TestProcessResult result = {0};
  ASSERT_EQ_INT(0, test_run_argv_capture(argv, 0, &result));
  ASSERT_EQ_INT(1, result.exit_code);
  ASSERT_NOT_NULL(diagnostic);
  ASSERT_TRUE(strstr(result.stderr_text, diagnostic) != NULL);
  if (strstr(diagnostic, "required version 2") != NULL) {
    char output_path[128];
    ASSERT_TRUE(snprintf(output_path, sizeof(output_path), "%s.v2", path) > 0);
    ASSERT_TRUE(strstr(result.stderr_text, path) != NULL);
    ASSERT_TRUE(strstr(result.stderr_text, output_path) != NULL);
    errno = 0;
    ASSERT_EQ_INT(-1, access(output_path, F_OK));
    ASSERT_EQ_INT(ENOENT, errno);
  }
  test_process_result_free(&result);
  assert_unchanged(path, bytes, length);
}

static void run_sin_from_directory(const char *directory, const char *sin_path,
                                   const char *object_path,
                                   TestProcessResult *result) {
  char command[PATH_MAX * 3];
  int written = snprintf(command, sizeof(command),
                         "cd '%s' && exec '%s' --loadonly -o '%s'",
                         directory, sin_path, object_path);
  ASSERT_TRUE(written > 0 && (size_t)written < sizeof(command));
  char *argv[] = {"/bin/sh", "-c", command, NULL};
  ASSERT_EQ_INT(0, test_run_argv_capture(argv, 0, result));
}

void test_sin_default_source_root_validation(void) {
  char cwd[PATH_MAX];
  ASSERT_NOT_NULL(getcwd(cwd, sizeof(cwd)));

  const char *sin_path = test_program_path("sin");
  char source_path[PATH_MAX];
  char object_path[PATH_MAX];
  ASSERT_TRUE(snprintf(source_path, sizeof(source_path),
                       "%s/examples/echo-load.src", cwd) > 0);
  ASSERT_EQ_INT(0, test_make_temp_path("sin-default-source-object", object_path,
                                       sizeof(object_path)));

  char *compile_argv[] = {TEST_SCOMP, source_path, object_path, NULL};
  TestProcessResult compile_result = {0};
  ASSERT_EQ_INT(0, test_run_argv_capture(compile_argv, 0, &compile_result));
  ASSERT_EQ_INT(0, compile_result.exit_code);
  test_process_result_free(&compile_result);

  char run_dir[4096];

  ASSERT_EQ_INT(0, test_temp_template(run_dir, sizeof run_dir, "sin-default-srcroot"));
  ASSERT_NOT_NULL(mkdtemp(run_dir));
  TestProcessResult result = {0};
  run_sin_from_directory(run_dir, sin_path, object_path, &result);
  ASSERT_EQ_INT(0, result.exit_code);
  ASSERT_TRUE(strstr(result.stdout_text,
                     "Creating new source root in current directory.") != NULL);
  ASSERT_TRUE(strstr(result.stdout_text,
                     "Using 'srcroot' as the source root.") != NULL);
  ASSERT_TRUE(strstr(result.stderr_text, "Unable to") == NULL);
  test_process_result_free(&result);

  char created_root[PATH_MAX];
  char created_store[PATH_MAX];
  ASSERT_TRUE(snprintf(created_root, sizeof(created_root), "%s/srcroot",
                       run_dir) > 0);
  ASSERT_TRUE(snprintf(created_store, sizeof(created_store), "%s/items.dat",
                       run_dir) > 0);
  struct stat root_stat;
  ASSERT_EQ_INT(0, stat(created_root, &root_stat));
  ASSERT_TRUE(S_ISDIR(root_stat.st_mode));
  ASSERT_EQ_INT(0, unlink(created_store));
  char created_source[PATH_MAX];
  char created_input[PATH_MAX];
  ASSERT_TRUE(snprintf(created_source, sizeof(created_source),
                       "%s/input/source.sin", created_root) > 0);
  ASSERT_TRUE(snprintf(created_input, sizeof(created_input), "%s/input",
                       created_root) > 0);
  ASSERT_EQ_INT(0, unlink(created_source));
  ASSERT_EQ_INT(0, rmdir(created_input));
  ASSERT_EQ_INT(0, rmdir(created_root));
  ASSERT_EQ_INT(0, rmdir(run_dir));

  ASSERT_EQ_INT(0, test_temp_template(run_dir, sizeof run_dir,
                                      "sin-default-srcroot"));
  ASSERT_NOT_NULL(mkdtemp(run_dir));
  char dangling_root[PATH_MAX];
  ASSERT_TRUE(snprintf(dangling_root, sizeof(dangling_root), "%s/srcroot",
                       run_dir) > 0);
  ASSERT_EQ_INT(0, symlink("missing-target", dangling_root));
  run_sin_from_directory(run_dir, sin_path, object_path, &result);
  ASSERT_EQ_INT(1, result.exit_code);
  ASSERT_TRUE(strstr(result.stderr_text, "Unable to create source root") != NULL);
  ASSERT_TRUE(strstr(result.stdout_text,
                     "Creating new source root in current directory.") == NULL);
  ASSERT_TRUE(strstr(result.stdout_text,
                     "Using 'srcroot' as the source root.") == NULL);
  test_process_result_free(&result);
  ASSERT_EQ_INT(0, unlink(dangling_root));
  ASSERT_EQ_INT(0, rmdir(run_dir));
  ASSERT_EQ_INT(0, unlink(object_path));
}

void test_sin_itemstore_version_policy(void) {
  char path[96];
  ASSERT_EQ_INT(0, test_make_temp_path("sin-policy-items", path, sizeof(path)));
  const uint8_t v1[] = {'S','I','N','I','T','E','M','\0',1,0};
  char *v1_argv[] = {TEST_SIN, "--itemstore", path, NULL};
  run_rejected(path, v1, sizeof(v1), v1_argv,
               "found version 1; required version 2");

  char *v1_loadonly[] = {TEST_SIN, "--loadonly", "--itemstore", path, NULL};
  run_rejected(path, v1, sizeof(v1), v1_loadonly,
               "found version 1; required version 2");
  char *v1_strict_after[] = {TEST_SIN, "--itemstore", path,
                             "--strict-validation", NULL};
  run_rejected(path, v1, sizeof(v1), v1_strict_after,
               "found version 1; required version 2");
  char *v1_strict_before[] = {TEST_SIN, "--strict-validation", "--itemstore",
                              path, NULL};
  run_rejected(path, v1, sizeof(v1), v1_strict_before,
               "found version 1; required version 2");

  const uint8_t unknown[] = {'S','I','N','I','T','E','M','\0',9,0,0x42};
  char *unknown_argv[] = {TEST_SIN, "--itemstore", path, NULL};
  run_rejected(path, unknown, sizeof(unknown), unknown_argv,
               "found 9");
  const uint8_t invalid_magic[] = {'N','O','P','E','\0','\0','\0','\0',2,0};
  run_rejected(path, invalid_magic, sizeof(invalid_magic), unknown_argv,
               "invalid itemstore magic");
  const uint8_t unversioned[] = {'S','I','N','I','T','E','M','\0'};
  run_rejected(path, unversioned, sizeof(unversioned), unknown_argv,
               "truncated file header version");

  ITEMSTORE_t *store = itemstore_create("root");
  ASSERT_NOT_NULL(store);
  ASSERT_TRUE(itemstore_save(path, store));
  itemstore_destroy(store);
  size_t before_length = 0;
  uint8_t *before = read_bytes(path, &before_length);
  char *parse_failure[] = {TEST_SIN, "--itemstore", path, "--not-an-option", NULL};
  TestProcessResult result = {0};
  ASSERT_EQ_INT(0, test_run_argv_capture(parse_failure, 0, &result));
  ASSERT_EQ_INT(1, result.exit_code);
  test_process_result_free(&result);
  assert_unchanged(path, before, before_length);
  free(before);
  ASSERT_EQ_INT(0, unlink(path));

  char object_path[96];
  ASSERT_EQ_INT(0, test_make_temp_path("sin-policy-object", object_path,
                                       sizeof(object_path)));
  const uint8_t truncated_header[] = {0x00, 0xff, 'S'};
  const uint8_t unsupported_version[] = {
    0x00, 0xff, 'S', 'B', 0x00, 0x02, 0x00, 0x00
  };
  const uint8_t invalid_opcode[] = {
    0x00, 0xff, 'S', 'B', 0x01, 0x00, 0x00, 0x00, 0xff
  };
  const struct {
    const uint8_t *bytes;
    size_t length;
    const char *diagnostic;
  } malformed[] = {
    {truncated_header, sizeof(truncated_header), "truncated header"},
    {unsupported_version, sizeof(unsupported_version), "unsupported bytecode version"},
    {invalid_opcode, sizeof(invalid_opcode), "invalid opcode"}
  };
  char *malformed_argv[] = {TEST_SIN, "--loadonly", "--itemstore", path,
                            "--srcroot", "tests/fixtures", "--object",
                            object_path, NULL};
  for (size_t i = 0; i < sizeof(malformed) / sizeof(malformed[0]); i++) {
    write_bytes(object_path, malformed[i].bytes, malformed[i].length);
    ASSERT_EQ_INT(0, test_run_argv_capture(malformed_argv, 0, &result));
    ASSERT_EQ_INT(1, result.exit_code);
    ASSERT_TRUE(strstr(result.stderr_text, malformed[i].diagnostic) != NULL);
    test_process_result_free(&result);
    errno = 0;
    ASSERT_EQ_INT(-1, access(path, F_OK));
    ASSERT_EQ_INT(ENOENT, errno);
  }

  size_t legacy_length = 0;
  uint8_t *legacy = load_hex_fixture(
      "tests/fixtures/bytecode-migration/legacy-0.7.1.hex", &legacy_length);
  ASSERT_NOT_NULL(legacy);
  write_bytes(object_path, legacy, legacy_length);
  free(legacy);
  char *legacy_argv[] = {TEST_SIN, "--loadonly", "--itemstore", path,
                         "--srcroot", "tests/fixtures", "--object",
                         object_path, NULL};
  ASSERT_EQ_INT(0, test_run_argv_capture(legacy_argv, 0, &result));
  ASSERT_EQ_INT(1, result.exit_code);
  ASSERT_TRUE(strstr(result.stderr_text, "unversioned") != NULL);
  ASSERT_TRUE(strstr(result.stderr_text, "recompile with scomp") != NULL);
  test_process_result_free(&result);
  ASSERT_EQ_INT(0, unlink(object_path));

  size_t object_length = 0;
  uint8_t *object = load_hex_fixture("tests/fixtures/nil_literal.hex",
                                     &object_length);
  ASSERT_NOT_NULL(object);
  write_bytes(object_path, object, object_length);
  free(object);
  char *missing_argv[] = {TEST_SIN, "--loadonly", "--itemstore", path,
                          "--srcroot", "tests/fixtures", "--object",
                          object_path, NULL};
  ASSERT_EQ_INT(0, test_run_argv_capture(missing_argv, 0, &result));
  ASSERT_EQ_INT(0, result.exit_code);
  test_process_result_free(&result);
  size_t created_length = 0;
  uint8_t *created = read_bytes(path, &created_length);
  ASSERT_TRUE(created_length >= 10);
  ASSERT_EQ_INT('S', created[0]);
  ASSERT_EQ_INT('I', created[1]);
  ASSERT_EQ_INT('N', created[2]);
  ASSERT_EQ_INT('I', created[3]);
  ASSERT_EQ_INT('T', created[4]);
  ASSERT_EQ_INT('E', created[5]);
  ASSERT_EQ_INT('M', created[6]);
  ASSERT_EQ_INT(0, created[7]);
  ASSERT_EQ_INT(2, created[8]);
  ASSERT_EQ_INT(0, created[9]);
  free(created);
  ASSERT_EQ_INT(0, unlink(path));
  ASSERT_EQ_INT(0, unlink(object_path));
}

void test_sin_boot_frees_aggregate_return_values(void) {
  static const char *sources[] = {
    "return #[1, &fred];",
    "return &fred;"
  };
  static const char *kinds[] = {"list", "itemref"};
  for (size_t i = 0; i < sizeof(sources) / sizeof(sources[0]); ++i) {
    char source_path[96], object_path[96], itemstore_path[96];
    ASSERT_EQ_INT(0, test_make_temp_path("sin-boot-source", source_path,
                                         sizeof(source_path)));
    ASSERT_EQ_INT(0, test_make_temp_path("sin-boot-object", object_path,
                                         sizeof(object_path)));
    ASSERT_EQ_INT(0, test_make_temp_path("sin-boot-items", itemstore_path,
                                         sizeof(itemstore_path)));
    write_bytes(source_path, (const uint8_t *)sources[i], strlen(sources[i]));
    char *compile_argv[] = {TEST_SCOMP, source_path, object_path, NULL};
    TestProcessResult result = {0};
    ASSERT_EQ_INT(0, test_run_argv_capture(compile_argv, 0, &result));
    ASSERT_EQ_INT(0, result.exit_code);
    test_process_result_free(&result);
    char *run_argv[] = {TEST_SIN, "--loadonly", "--verbose", "--itemstore",
                        itemstore_path, "--srcroot", "tests/fixtures", "--object",
                        object_path, NULL};
    ASSERT_EQ_INT(0, test_run_argv_capture(run_argv, 0, &result));
    ASSERT_EQ_INT(0, result.exit_code);
    ASSERT_TRUE(strstr(result.stderr_text, kinds[i]) != NULL);
    test_process_result_free(&result);
    ASSERT_EQ_INT(0, unlink(source_path));
    ASSERT_EQ_INT(0, unlink(object_path));
    ASSERT_EQ_INT(0, unlink(itemstore_path));
  }
}
