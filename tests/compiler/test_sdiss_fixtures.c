#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>

#include "test_assert.h"
#include "test_helpers.h"

static char *read_text(const char *path) {
  FILE *f = fopen(path, "rb");
  ASSERT_NOT_NULL(f);
  ASSERT_EQ_INT(0, fseek(f, 0, SEEK_END));
  long n = ftell(f);
  ASSERT_TRUE(n >= 0);
  ASSERT_EQ_INT(0, fseek(f, 0, SEEK_SET));
  size_t len = (size_t)n;
  char *buf = malloc(len + 1u);
  ASSERT_NOT_NULL(buf);
  ASSERT_TRUE(fread(buf, 1, len, f) == len);
  buf[len] = '\0';
  fclose(f);
  return buf;
}

void test_sdiss_fixture_basic(void) {
  size_t len = 0;
  uint8_t *bytes = load_hex_fixture("tests/fixtures/sdiss/basic.hex", &len);
  ASSERT_NOT_NULL(bytes);

  const char *tmp_path = "tests/fixtures/sdiss/basic.bin";
  FILE *out = fopen(tmp_path, "wb");
  ASSERT_NOT_NULL(out);
  ASSERT_EQ_INT((int)len, (int)fwrite(bytes, 1, len, out));
  fclose(out);
  free(bytes);

  FILE *pipe = popen("./sdiss --no-header -o tests/fixtures/sdiss/basic.bin 2>&1", "r");
  ASSERT_NOT_NULL(pipe);
  char output[4096];
  size_t total = fread(output, 1, sizeof(output) - 1, pipe);
  output[total] = '\0';
  int rc = pclose(pipe);
  ASSERT_TRUE(rc != -1);
  ASSERT_TRUE(WIFEXITED(rc));
  ASSERT_EQ_INT(0, WEXITSTATUS(rc));

  char *expected = read_text("tests/fixtures/sdiss/basic.expected.txt");
  char *line = strtok(expected, "\n");
  while (line != NULL) {
    ASSERT_TRUE(strstr(output, line) != NULL);
    line = strtok(NULL, "\n");
  }
  free(expected);
  remove(tmp_path);
}

static void run_sdiss_fixture(const char *path, char *output, size_t output_size, int *exit_code) {
  char cmd[512];
  int rc = snprintf(cmd, sizeof(cmd), "./sdiss --no-header -o %s 2>&1", path);
  ASSERT_TRUE(rc > 0 && (size_t)rc < sizeof(cmd));
  FILE *pipe = popen(cmd, "r");
  ASSERT_NOT_NULL(pipe);
  size_t total = fread(output, 1, output_size - 1, pipe);
  output[total] = '\0';
  rc = pclose(pipe);
  ASSERT_TRUE(rc != -1);
  ASSERT_TRUE(WIFEXITED(rc));
  *exit_code = WEXITSTATUS(rc);
}

void test_sdiss_malformed_fixture_reports_verifier_diagnostic(void) {
  const uint8_t bytes[] = {0x00, 0x00, 'l', 0x03, 0x00, 'a'};
  const char *tmp_path = "tests/fixtures/sdiss/malformed.bin";
  FILE *out = fopen(tmp_path, "wb");
  ASSERT_NOT_NULL(out);
  ASSERT_EQ_INT((int)sizeof(bytes), (int)fwrite(bytes, 1, sizeof(bytes), out));
  fclose(out);

  char output[4096];
  int exit_code = 0;
  run_sdiss_fixture(tmp_path, output, sizeof(output), &exit_code);
  ASSERT_EQ_INT(1, exit_code);
  ASSERT_TRUE(strstr(output, "truncated") != NULL);
  ASSERT_TRUE(strstr(output, "Disassembly aborted due to malformed bytecode") != NULL);
  remove(tmp_path);
}

void test_sdiss_reads_compiler_operand_widths(void) {
  const uint8_t bytes[] = {
      0x00, 0x00, /* locals/params */
      'M', 0x09, /* LIBCALL_TOKEN id=9 */
      'F', 0x02, 0x00, /* CALL argc=2 */
      'h'};

  const char *tmp_path = "tests/fixtures/sdiss/operand-widths.bin";
  FILE *out = fopen(tmp_path, "wb");
  ASSERT_NOT_NULL(out);
  ASSERT_EQ_INT((int)sizeof(bytes), (int)fwrite(bytes, 1, sizeof(bytes), out));
  fclose(out);

  FILE *pipe = popen("./sdiss --no-header -o tests/fixtures/sdiss/operand-widths.bin 2>&1", "r");
  ASSERT_NOT_NULL(pipe);
  char output[4096];
  size_t total = fread(output, 1, sizeof(output) - 1, pipe);
  output[total] = '\0';
  int rc = pclose(pipe);
  ASSERT_TRUE(rc != -1);
  ASSERT_TRUE(WIFEXITED(rc));
  ASSERT_EQ_INT(0, WEXITSTATUS(rc));

  ASSERT_TRUE(strstr(output, "LIBCALL_TOKEN 9") != NULL);
  ASSERT_TRUE(strstr(output, "CALL ARGC 2") != NULL);
  ASSERT_TRUE(strstr(output, "unknown=0") != NULL);

  char output2[4096];
  int exit_code = 0;
  run_sdiss_fixture("tests/fixtures/sdiss/operand-widths.bin", output2, sizeof(output2), &exit_code);
  ASSERT_EQ_INT(0, exit_code);
  remove(tmp_path);
}
