#include <stdint.h>
#include <stdlib.h>

#include "test_assert.h"
#include "test_helpers.h"

typedef struct {
  const char *name;
  const char *src_path;
  const char *obj_path;
  const char *out_path;
} ParserObjGoldenCase;

static void run_case(const ParserObjGoldenCase *tc) {
  char cmd[512];
  int rc = snprintf(cmd, sizeof(cmd), "./scomp %s %s", tc->src_path, tc->out_path);
  ASSERT_TRUE(rc > 0 && (size_t)rc < sizeof(cmd));

  char *output = NULL;
  rc = run_command_and_capture(cmd, &output);
  free(output);
  ASSERT_EQ_INT(0, rc);

  assert_file_bytes_equal(tc->obj_path, tc->out_path, tc->name);
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
