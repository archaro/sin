#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "test_assert.h"

typedef struct {
  const char *name;
  const char *src_path;
  const char *obj_path;
  const char *out_path;
} ParserObjGoldenCase;

static size_t file_size(FILE *f) {
  ASSERT_EQ_INT(0, fseek(f, 0, SEEK_END));
  long n = ftell(f);
  ASSERT_TRUE(n >= 0);
  ASSERT_EQ_INT(0, fseek(f, 0, SEEK_SET));
  return (size_t)n;
}

static void run_case(const ParserObjGoldenCase *tc) {
  char cmd[512];
  int rc = snprintf(cmd, sizeof(cmd), "./scomp %s %s", tc->src_path, tc->out_path);
  ASSERT_TRUE(rc > 0 && (size_t)rc < sizeof(cmd));

  rc = system(cmd);
  ASSERT_EQ_INT(0, rc);

  FILE *expected_f = fopen(tc->obj_path, "rb");
  ASSERT_NOT_NULL(expected_f);

  FILE *actual_f = fopen(tc->out_path, "rb");
  ASSERT_NOT_NULL(actual_f);

  size_t expected_len = file_size(expected_f);
  size_t actual_len = file_size(actual_f);
  ASSERT_EQ_INT((int)expected_len, (int)actual_len);

  uint8_t *expected = malloc(expected_len);
  uint8_t *actual = malloc(actual_len);
  ASSERT_NOT_NULL(expected);
  ASSERT_NOT_NULL(actual);

  size_t expected_read = fread(expected, 1, expected_len, expected_f);
  size_t actual_read = fread(actual, 1, actual_len, actual_f);
  ASSERT_EQ_INT((int)expected_len, (int)expected_read);
  ASSERT_EQ_INT((int)actual_len, (int)actual_read);

  ASSERT_EQ_INT(0, memcmp(expected, actual, expected_len));

  fclose(expected_f);
  fclose(actual_f);
  free(expected);
  free(actual);
  remove(tc->out_path);
}

void test_parser_examples_obj_golden(void) {
  const ParserObjGoldenCase cases[] = {
      {"chat_boot", "examples/chat-boot.src", "examples/chat-boot.obj", "tests/fixtures/chat-boot.generated.obj"},
      {"chat_load", "examples/chat-load.src", "examples/chat-load.obj", "tests/fixtures/chat-load.generated.obj"},
      {"echo_boot", "examples/echo-boot.src", "examples/echo-boot.obj", "tests/fixtures/echo-boot.generated.obj"},
      {"echo_load", "examples/echo-load.src", "examples/echo-load.obj", "tests/fixtures/echo-load.generated.obj"},
  };

  for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
    run_case(&cases[i]);
  }
}
