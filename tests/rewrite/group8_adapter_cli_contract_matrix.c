#include "test_framework.h"
#include "test_helpers.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void write_bytes(const char *path, const void *data, size_t length) {
  FILE *file = fopen(path, "wb");
  TF_ASSERT_TRUE(file != NULL);
  TF_ASSERT_U64(length, fwrite(data, 1, length, file));
  TF_ASSERT_I64(0, fclose(file));
}

static uint8_t *read_bytes(const char *path, size_t *length) {
  FILE *file = fopen(path, "rb");
  long size;
  uint8_t *data;
  TF_ASSERT_TRUE(file != NULL);
  TF_ASSERT_I64(0, fseek(file, 0, SEEK_END));
  size = ftell(file);
  TF_ASSERT_TRUE(size >= 0);
  TF_ASSERT_I64(0, fseek(file, 0, SEEK_SET));
  data = malloc((size_t)size ? (size_t)size : 1u);
  TF_ASSERT_TRUE(data != NULL);
  TF_ASSERT_U64((size_t)size, fread(data, 1, (size_t)size, file));
  TF_ASSERT_I64(0, fclose(file));
  *length = (size_t)size;
  return data;
}

static bool file_exists(const char *path) {
  FILE *file = fopen(path, "rb");
  if (!file) return false;
  (void)fclose(file);
  return true;
}

static void run_process(char *const argv[], int expected_status,
                        const char *stdout_text, const char *stderr_text,
                        bool stdout_empty, bool stderr_empty) {
  TF_ProcessResult result = {0};
  int run_status = tf_process_run(argv, 30000, &result);
  TF_ASSERT_I64(0, run_status);
  TF_ASSERT_PROCESS(&result, expected_status);
  if (stdout_empty)
    TF_ASSERT_U64(0, result.stdout_len);
  if (stderr_empty)
    TF_ASSERT_U64(0, result.stderr_len);
  if (stdout_text)
    TF_ASSERT_DIAGNOSTIC(stdout_text, result.stdout_data);
  if (stderr_text)
    TF_ASSERT_DIAGNOSTIC(stderr_text, result.stderr_data);
  tf_process_result_destroy(&result);
}

static void fixture_path(const TF_Fixture *fixture, const char *name,
                         char *path, size_t path_size) {
  TF_ASSERT_I64(0, tf_fixture_file(fixture, name, path, path_size));
}

static const uint8_t sdiss_basic[] = {
    0x00, 0x00, 0x70, 0x2a, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x61, 0x68,
};

static const uint8_t sconv_v1[] = {
    0x53, 0x49, 0x4e, 0x49, 0x54, 0x45, 0x4d, 0x00, 0x01, 0x00,
    0x04, 0x72, 0x6f, 0x6f, 0x74, 0x01, 0x03, 0x00, 0x00, 0x00, 0x00,
};

static void test_scomp_cli_contract_matrix(void) {
  TF_Fixture fixture;
  char source_path[4096], object_path[4096], missing_path[4096];
  const char source[] = "return 42;\n";
  uint8_t *before;
  uint8_t *after;
  size_t before_length, after_length;
  tf_fixture_init(&fixture);
  TF_ASSERT_TRUE(tf_fixture_path(&fixture) != NULL);
  fixture_path(&fixture, "program.src", source_path, sizeof source_path);
  fixture_path(&fixture, "program.obj", object_path, sizeof object_path);
  fixture_path(&fixture, "missing.src", missing_path, sizeof missing_path);
  write_bytes(source_path, source, sizeof source - 1u);
  before = read_bytes(source_path, &before_length);

  char *normal[] = {TEST_SCOMP, "--quiet", source_path, object_path, NULL};
  run_process(normal, 0, NULL, NULL, true, true);
  TF_ASSERT_TRUE(file_exists(object_path));
  after = read_bytes(source_path, &after_length);
  TF_ASSERT_BYTES(before, before_length, after, after_length);
  free(after);

  char *help[] = {TEST_SCOMP, "--help", NULL};
  run_process(help, 0, "Usage:", NULL, false, true);
  char *version[] = {TEST_SCOMP, "--version", NULL};
  run_process(version, 0, "scomp ", NULL, false, true);
  char *unknown[] = {TEST_SCOMP, "--unknown-option", NULL};
  run_process(unknown, 1, NULL, "invalid option", true, false);
  char *missing_argument[] = {TEST_SCOMP, "-i", source_path, "-o", NULL};
  run_process(missing_argument, 1, NULL, "invalid option", true, false);
  char *unreadable[] = {TEST_SCOMP, "--quiet", missing_path, object_path,
                        NULL};
  run_process(unreadable, 1, NULL, "Error:", true, false);

  free(before);
  tf_fixture_cleanup(&fixture);
}

static void test_sdiss_cli_contract_matrix(void) {
  TF_Fixture fixture;
  char object_path[4096], empty_path[4096];
  uint8_t *before;
  uint8_t *after;
  size_t before_length, after_length;
  tf_fixture_init(&fixture);
  TF_ASSERT_TRUE(tf_fixture_path(&fixture) != NULL);
  fixture_path(&fixture, "program.obj", object_path, sizeof object_path);
  fixture_path(&fixture, "empty.obj", empty_path, sizeof empty_path);
  write_bytes(object_path, sdiss_basic, sizeof sdiss_basic);
  write_bytes(empty_path, "bad", 3u);
  before = read_bytes(object_path, &before_length);

  char *normal[] = {TEST_SDISS, "--quiet", "--object", object_path, NULL};
  run_process(normal, 0, "INTEGER 42", NULL, false, true);
  after = read_bytes(object_path, &after_length);
  TF_ASSERT_BYTES(before, before_length, after, after_length);
  free(after);

  char *help[] = {TEST_SDISS, "--help", NULL};
  run_process(help, 0, "Usage:", NULL, false, true);
  char *version[] = {TEST_SDISS, "--version", NULL};
  run_process(version, 0, "sdiss ", NULL, false, true);
  char *unknown[] = {TEST_SDISS, "--unknown-option", NULL};
  run_process(unknown, 1, NULL, "invalid option", true, false);
  char *missing_argument[] = {TEST_SDISS, "--object", NULL};
  run_process(missing_argument, 1, NULL, "invalid option", true, false);
  char *malformed[] = {TEST_SDISS, "--quiet", "--object", empty_path, NULL};
  run_process(malformed, 1, "Local variables",
              "Disassembly aborted due to malformed bytecode.", false, false);

  free(before);
  tf_fixture_cleanup(&fixture);
}

static void test_sin_cli_contract_matrix(void) {
  TF_Fixture fixture;
  char source_path[4096], object_path[4096], itemstore_path[4096];
  char missing_object_path[4096];
  const char source[] = "return 42;\n";
  uint8_t *source_before, *object_before, *source_after, *object_after;
  size_t source_before_length, object_before_length;
  size_t source_after_length, object_after_length;
  tf_fixture_init(&fixture);
  TF_ASSERT_TRUE(tf_fixture_path(&fixture) != NULL);
  fixture_path(&fixture, "program.src", source_path, sizeof source_path);
  fixture_path(&fixture, "program.obj", object_path, sizeof object_path);
  fixture_path(&fixture, "items.dat", itemstore_path, sizeof itemstore_path);
  fixture_path(&fixture, "missing.obj", missing_object_path,
               sizeof missing_object_path);
  write_bytes(source_path, source, sizeof source - 1u);
  char *compile[] = {TEST_SCOMP, "--quiet", source_path, object_path, NULL};
  run_process(compile, 0, NULL, NULL, true, true);
  source_before = read_bytes(source_path, &source_before_length);
  object_before = read_bytes(object_path, &object_before_length);

  char *normal[] = {TEST_SIN, "--loadonly", "--quiet", "--itemstore",
                    itemstore_path, "--srcroot", "tests/fixtures", "--object",
                    object_path, NULL};
  run_process(normal, 0, NULL, NULL, true, true);
  TF_ASSERT_TRUE(file_exists(itemstore_path));
  source_after = read_bytes(source_path, &source_after_length);
  object_after = read_bytes(object_path, &object_after_length);
  TF_ASSERT_BYTES(source_before, source_before_length, source_after,
                  source_after_length);
  TF_ASSERT_BYTES(object_before, object_before_length, object_after,
                  object_after_length);
  free(source_after);
  free(object_after);

  char *help[] = {TEST_SIN, "--help", NULL};
  run_process(help, 0, "Syntax:", NULL, false, true);
  char *version[] = {TEST_SIN, "--version", NULL};
  run_process(version, 0, "sin ", NULL, false, true);
  char *unknown[] = {TEST_SIN, "--unknown-option", NULL};
  run_process(unknown, 1, NULL, "invalid option", true, false);
  char *missing_argument[] = {TEST_SIN, "--loadonly", "--quiet", "--itemstore",
                              itemstore_path, "--srcroot", "tests/fixtures",
                              "--object", NULL};
  run_process(missing_argument, 1, NULL, "invalid option", true, false);
  char *unreadable[] = {TEST_SIN, "--loadonly", "--quiet", "--itemstore",
                        itemstore_path, "--srcroot", "tests/fixtures", "--object",
                        missing_object_path, NULL};
  run_process(unreadable, 1, NULL, "Unable to read object file", true, false);

  free(source_before);
  free(object_before);
  tf_fixture_cleanup(&fixture);
}

static void test_sconv_cli_contract_matrix(void) {
  TF_Fixture fixture;
  char input_path[4096], output_path[4096], malformed_path[4096];
  char malformed_output_path[4096];
  uint8_t *before, *after;
  size_t before_length, after_length;
  tf_fixture_init(&fixture);
  TF_ASSERT_TRUE(tf_fixture_path(&fixture) != NULL);
  fixture_path(&fixture, "input.itemstore", input_path, sizeof input_path);
  fixture_path(&fixture, "output.itemstore", output_path, sizeof output_path);
  fixture_path(&fixture, "malformed.itemstore", malformed_path,
               sizeof malformed_path);
  fixture_path(&fixture, "malformed-output.itemstore", malformed_output_path,
               sizeof malformed_output_path);
  write_bytes(input_path, sconv_v1, sizeof sconv_v1);
  write_bytes(malformed_path, "bad", 3u);
  before = read_bytes(input_path, &before_length);

  char *normal[] = {TEST_SCONV, "--quiet", input_path, output_path, NULL};
  run_process(normal, 0, NULL, NULL, true, true);
  TF_ASSERT_TRUE(file_exists(output_path));
  after = read_bytes(input_path, &after_length);
  TF_ASSERT_BYTES(before, before_length, after, after_length);
  free(after);

  char *help[] = {TEST_SCONV, "--help", NULL};
  run_process(help, 0, "Usage:", NULL, false, true);
  char *version[] = {TEST_SCONV, "--version", NULL};
  run_process(version, 0, "sconv ", NULL, false, true);
  char *unknown[] = {TEST_SCONV, "--unknown-option", NULL};
  run_process(unknown, 1, NULL, "invalid option", true, false);
  char *missing_argument[] = {TEST_SCONV, "-i", input_path, "-o", NULL};
  run_process(missing_argument, 1, NULL, "invalid option", true, false);
  char *malformed[] = {TEST_SCONV, "--quiet", malformed_path,
                       malformed_output_path, NULL};
  run_process(malformed, 1, NULL, "Failed to convert", true, false);

  free(before);
  tf_fixture_cleanup(&fixture);
}

static const TF_TestDescriptor tests[] = {
    {"rewrite.e2e.scomp_cli_contract_matrix", test_scomp_cli_contract_matrix,
     "exclusive", 30000,
     "api.entrypoint.scomp,executable.scomp.command-line,executable.scomp.input-output,executable.scomp.exit-status,executable.scomp.persistence,executable.scomp.errors"},
    {"rewrite.e2e.sdiss_cli_contract_matrix", test_sdiss_cli_contract_matrix,
     "exclusive", 30000,
     "api.entrypoint.sdiss,executable.sdiss.command-line,executable.sdiss.input-output,executable.sdiss.exit-status,executable.sdiss.persistence,executable.sdiss.errors"},
    {"rewrite.e2e.sin_cli_contract_matrix", test_sin_cli_contract_matrix,
     "exclusive", 30000,
     "api.entrypoint.sin,executable.sin.command-line,executable.sin.input-output,executable.sin.exit-status,executable.sin.persistence,executable.sin.errors"},
    {"rewrite.e2e.sconv_cli_contract_matrix", test_sconv_cli_contract_matrix,
     "exclusive", 30000,
     "api.entrypoint.sconv,executable.sconv.command-line,executable.sconv.input-output,executable.sconv.exit-status,executable.sconv.persistence,executable.sconv.errors"},
};

int main(int argc, char **argv) {
  return tf_main(argc, argv, tests, sizeof tests / sizeof tests[0]);
}
