#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>

#include "test_assert.h"
#include "test_helpers.h"

static char *read_text(const char *path) {
  FILE *f = fopen(path, "rb");
  ASSERT_NOT_NULL(f);
  fseek(f, 0, SEEK_END);
  long n = ftell(f);
  fseek(f, 0, SEEK_SET);
  char *buf = malloc((size_t)n + 1);
  ASSERT_NOT_NULL(buf);
  fread(buf, 1, (size_t)n, f);
  buf[n] = '\0';
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
}

void test_sdiss_reads_compiler_operand_widths(void) {
  const uint8_t bytes[] = {
      0x00, 0x00, /* locals/params */
      'A', 0x02, 0x03, /* LIBCALL argc=2 id=3 */
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

  ASSERT_TRUE(strstr(output, "LIBCALL ARGC 2 ID 3") != NULL);
  ASSERT_TRUE(strstr(output, "CALL ARGC 2") != NULL);
  ASSERT_TRUE(strstr(output, "unknown=0") != NULL);
}
