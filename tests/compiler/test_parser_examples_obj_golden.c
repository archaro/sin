#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "compiler_pipeline.h"
#include "error.h"
#include "test_assert.h"
#include "test_helpers.h"

typedef struct {
  const char *name;
  const char *src_path;
  const char *obj_path;
  const char *out_path;
} ParserObjGoldenCase;

static char *load_file_text(const char *path, size_t *out_len) {
  FILE *f = fopen(path, "rb");
  ASSERT_NOT_NULL(f);

  ASSERT_EQ_INT(0, fseek(f, 0, SEEK_END));
  long n = ftell(f);
  ASSERT_TRUE(n >= 0);
  ASSERT_EQ_INT(0, fseek(f, 0, SEEK_SET));

  size_t len = (size_t)n;
  char *buf = malloc(len + 1);
  ASSERT_NOT_NULL(buf);
  ASSERT_EQ_INT((int)len, (int)fread(buf, 1, len, f));
  ASSERT_EQ_INT(0, fclose(f));
  buf[len] = '\0';
  *out_len = len;
  return buf;
}

static void write_output_file(const char *path, const OUTPUT_t *out) {
  ASSERT_NOT_NULL(path);
  ASSERT_NOT_NULL(out);
  ASSERT_NOT_NULL(out->bytecode);

  size_t out_len = (size_t)(out->nextbyte - out->bytecode);
  FILE *f = fopen(path, "wb");
  ASSERT_NOT_NULL(f);
  ASSERT_EQ_INT((int)out_len, (int)fwrite(out->bytecode, 1, out_len, f));
  ASSERT_EQ_INT(0, fclose(f));
}

static void run_case(const ParserObjGoldenCase *tc) {
  size_t src_len = 0;
  char *source = load_file_text(tc->src_path, &src_len);

  char *errdetail = NULL;
  OUTPUT_t *out = NULL;
  int8_t rc = compile_source_to_bytecode(source, src_len, &out, &errdetail);
  ASSERT_EQ_INT(ERR_NOERROR, rc);
  ASSERT_TRUE(errdetail == NULL);
  ASSERT_NOT_NULL(out);

  write_output_file(tc->out_path, out);
  assert_file_bytes_equal(tc->obj_path, tc->out_path, tc->name);

  free(source);
  free(out->bytecode);
  free(out);
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
