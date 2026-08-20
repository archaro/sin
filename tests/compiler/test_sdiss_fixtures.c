#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include "test_assert.h"
#include "test_helpers.h"
#include "sdiss_core.h"

typedef struct { char *buf; size_t len; size_t cap; } SdissCapture;
static char *read_text(const char *path);

static bool capture_sdiss(void *ctx, const char *data, size_t len) {
  SdissCapture *c = ctx;
  if (c->len + len + 1u > c->cap) return false;
  memcpy(c->buf + c->len, data, len);
  c->len += len;
  c->buf[c->len] = '\0';
  return true;
}

static bool failing_sdiss_writer(void *ctx, const char *data, size_t len) {
  (void)data;
  (void)len;
  (*(int *)ctx)++;
  return false;
}

typedef struct {
  int calls;
  int post_failure_calls;
  bool failed;
} SummaryFailureWriter;

static bool fail_summary_sdiss_writer(void *ctx, const char *data, size_t len) {
  SummaryFailureWriter *writer = ctx;
  if (writer->failed) {
    writer->post_failure_calls++;
    return false;
  }
  writer->calls++;
  if (len >= strlen("Summary:") && memcmp(data, "Summary:", strlen("Summary:")) == 0) {
    writer->failed = true;
    return false;
  }
  return true;
}

void test_sdiss_writer_failure_stops_output(void) {
  const uint8_t bytes[] = {0x00, 0x00, 'N', 'h'};
  int calls = 0;
  SDissOptions options = {.raw = 0, .no_header = 1};

  SDissResult result = sdiss_disassemble_bytes(bytes, sizeof(bytes), &options,
                                                failing_sdiss_writer, &calls);
  ASSERT_TRUE(result.output_error);
  ASSERT_EQ_INT(1, calls);
  ASSERT_EQ_INT(BC_VERIFY_OK, result.status);
}

void test_sdiss_summary_writer_failure_propagates(void) {
  const uint8_t bytes[] = {0x00, 0x00, 'N', 'h'};
  SummaryFailureWriter writer = {0};
  SDissOptions options = {.raw = 0, .no_header = 1};

  SDissResult result = sdiss_disassemble_bytes(bytes, sizeof(bytes), &options,
                                                fail_summary_sdiss_writer, &writer);
  ASSERT_TRUE(result.output_error);
  ASSERT_EQ_INT(5, writer.calls);
  ASSERT_EQ_INT(0, writer.post_failure_calls);
  ASSERT_EQ_INT(BC_VERIFY_OK, result.status);
}

void test_sdiss_summary_writer_failure_preserves_verifier_error(void) {
  const uint8_t bytes[] = {0x00, 0x00, 'N', 'l'};
  SummaryFailureWriter writer = {0};
  SDissOptions options = {.raw = 0, .no_header = 1};

  SDissResult result = sdiss_disassemble_bytes(bytes, sizeof(bytes), &options,
                                                fail_summary_sdiss_writer, &writer);
  ASSERT_TRUE(result.output_error);
  ASSERT_EQ_INT(3, writer.calls);
  ASSERT_EQ_INT(0, writer.post_failure_calls);
  ASSERT_EQ_INT(BC_VERIFY_ERROR, result.status);
  ASSERT_TRUE(strstr(result.diagnostic.message, "truncated") != NULL);
}

void test_sdiss_cli_reports_output_failure(void) {
  if (access("/dev/full", W_OK) != 0) return;

  const uint8_t bytes[] = {0x00, 0x00, 'N', 'h'};
  const char *tmp_path = "tests/fixtures/sdiss/output-failure.bin";
  const char *err_path = "tests/fixtures/sdiss/output-failure.err";
  FILE *out = fopen(tmp_path, "wb");
  ASSERT_NOT_NULL(out);
  ASSERT_EQ_INT((int)sizeof(bytes), (int)fwrite(bytes, 1, sizeof(bytes), out));
  ASSERT_EQ_INT(0, fclose(out));

  char cmd[512];
  int written = snprintf(cmd, sizeof(cmd),
                         "./sdiss --quiet --no-header -o %s > /dev/full 2> %s",
                         tmp_path, err_path);
  ASSERT_TRUE(written > 0 && (size_t)written < sizeof(cmd));
  int rc = system(cmd);
  ASSERT_TRUE(rc != -1);
  ASSERT_TRUE(WIFEXITED(rc));
  ASSERT_EQ_INT(1, WEXITSTATUS(rc));

  char *error = read_text(err_path);
  ASSERT_TRUE(strstr(error, "output") != NULL);
  free(error);
  ASSERT_EQ_INT(0, remove(tmp_path));
  ASSERT_EQ_INT(0, remove(err_path));
}

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
  struct MalformedCase {
    const uint8_t *bytes;
    size_t length;
    const char *diagnostic;
  };
  const uint8_t truncated[] = {0x00, 0x00, 'M', 0x01};
  const uint8_t unknown[] = {0x00, 0x00, 'M', 0xff, 0xff, 'h'};
  const struct MalformedCase cases[] = {
      {truncated, sizeof(truncated), "truncated LIBCALL"},
      {unknown, sizeof(unknown), "unknown libcall pair (255,255)"},
  };

  for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
    char pair_output[4096] = {0};
    SdissCapture capture = {pair_output, 0, sizeof(pair_output)};
    SDissOptions options = {.raw = 0, .no_header = 1};
    SDissResult result = sdiss_disassemble_bytes(cases[i].bytes,
                                                  (uint32_t)cases[i].length,
                                                  &options, capture_sdiss,
                                                  &capture);
    ASSERT_TRUE(result.status != BC_VERIFY_OK);
    ASSERT_TRUE(strstr(result.diagnostic.message, cases[i].diagnostic) != NULL);
  }
}

void test_sdiss_reads_compiler_operand_widths(void) {
  const uint8_t bytes[] = {
      0x00, 0x00, /* locals/params */
      'M', 0x01, 0x01, /* LIBCALL pair sys.log */
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

  ASSERT_TRUE(strstr(output, "LIBCALL 1,1") != NULL);
  ASSERT_TRUE(strstr(output, "CALL ARGC 2") != NULL);
  ASSERT_TRUE(strstr(output, "unknown=0") != NULL);

  char output2[4096];
  int exit_code = 0;
  run_sdiss_fixture("tests/fixtures/sdiss/operand-widths.bin", output2, sizeof(output2), &exit_code);
  ASSERT_EQ_INT(0, exit_code);
  remove(tmp_path);
}

void test_sdiss_lists_and_itemrefs_show_full_operands(void) {
  const uint8_t bytes[] = {0x00, 0x00, '[', 0x01, 0x04, 0x00, 0x00, '&', 'h'};
  char output[4096] = {0};
  SdissCapture capture = {output, 0, sizeof(output)};
  SDissOptions options = {.raw = 0, .no_header = 1};
  SDissResult result = sdiss_disassemble_bytes(bytes, sizeof(bytes), &options,
                                                capture_sdiss, &capture);
  ASSERT_EQ_INT(0, result.status);
  ASSERT_TRUE(strstr(output, "BUILD LIST COUNT 1025") != NULL);
  ASSERT_TRUE(strstr(output, "MAKE ITEMREF") != NULL);
}

void test_sdiss_jump_display_offsets_and_range(void) {
  const uint8_t forward[] = {0x00, 0x00, 'j', 0x02, 0x00, 'h'};
  const uint8_t backward[] = {0x00, 0x00, 'F', 0x00, 0x00, 'j', 0xfc, 0xff, 'h'};
  const uint8_t out_of_range[] = {0x00, 0x00, 'j', 0x00, 0x80, 'h'};
  const SDissOptions options = {.raw = 0, .no_header = 1};
  char output[4096];

  SdissCapture capture = {output, 0, sizeof(output)};
  SDissResult result = sdiss_disassemble_bytes(forward, sizeof(forward),
                                                &options, capture_sdiss,
                                                &capture);
  ASSERT_EQ_INT(BC_VERIFY_OK, result.status);
  ASSERT_TRUE(strstr(output, "JUMP rel=2 abs=5") != NULL);

  memset(output, 0, sizeof(output));
  capture.len = 0;
  result = sdiss_disassemble_bytes(backward, sizeof(backward), &options,
                                   capture_sdiss, &capture);
  ASSERT_EQ_INT(BC_VERIFY_OK, result.status);
  ASSERT_TRUE(strstr(output, "JUMP rel=-4 abs=2") != NULL);

  memset(output, 0, sizeof(output));
  capture.len = 0;
  result = sdiss_disassemble_bytes(out_of_range, sizeof(out_of_range),
                                   &options, capture_sdiss, &capture);
  ASSERT_EQ_INT(BC_VERIFY_OK, result.status);
  ASSERT_TRUE(strstr(output, "JUMP rel=-32768 abs=out-of-range") != NULL);
}

void test_sdiss_legacy_and_v1_headers_report_absolute_offsets(void) {
  const uint8_t legacy[] = {3, 1, 'b', 1, 'h'};
  const uint8_t v1[] = {0, 0xff, 'S', 'B', 1, 0, 3, 1, 'b', 1, 'h'};
  char legacy_out[4096] = {0};
  char v1_out[4096] = {0};
  SdissCapture lc = {legacy_out, 0, sizeof(legacy_out)};
  SdissCapture vc = {v1_out, 0, sizeof(v1_out)};
  SDissOptions options = {.raw = 0, .no_header = 0};
  SDissResult lr = sdiss_disassemble_bytes(legacy, sizeof(legacy), &options, capture_sdiss, &lc);
  SDissResult vr = sdiss_disassemble_bytes(v1, sizeof(v1), &options, capture_sdiss, &vc);
  ASSERT_EQ_INT(BC_VERIFY_OK, lr.status);
  ASSERT_EQ_INT(BC_VERIFY_OK, vr.status);
  ASSERT_TRUE(strstr(legacy_out, "Local variables: 3") != NULL);
  ASSERT_TRUE(strstr(v1_out, "Local variables: 3") != NULL);
  ASSERT_TRUE(strstr(legacy_out, "(Of which, 1 are parameters.)") != NULL);
  ASSERT_TRUE(strstr(v1_out, "(Of which, 1 are parameters.)") != NULL);
  ASSERT_TRUE(strstr(legacy_out, "Byte 00002:") != NULL);
  ASSERT_TRUE(strstr(v1_out, "Byte 00008:") != NULL);
}

void test_sdiss_missing_or_unreadable_input(void) {
  const char *paths[] = {
      "tests/fixtures/sdiss/does-not-exist.bin",
      "tests/fixtures/sdiss",
  };
  for (size_t i = 0; i < sizeof(paths) / sizeof(paths[0]); i++) {
    char command[512];
    int written = snprintf(command, sizeof(command),
                           "./sdiss --quiet --no-header -o %s 2>&1",
                           paths[i]);
    ASSERT_TRUE(written > 0 && (size_t)written < sizeof(command));
    FILE *pipe = popen(command, "r");
    ASSERT_NOT_NULL(pipe);
    char output[4096];
    size_t total = fread(output, 1, sizeof(output) - 1, pipe);
    output[total] = '\0';
    int status = pclose(pipe);
    ASSERT_TRUE(status != -1);
    ASSERT_TRUE(WIFEXITED(status));
    ASSERT_EQ_INT(EXIT_FAILURE, WEXITSTATUS(status));
    ASSERT_TRUE(strstr(output, "Unable to read object file") != NULL);
  }
}
