#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "compiler/compiler_pipeline.h"
#include "error.h"
#include "test_assert.h"
#include "test_helpers.h"

typedef struct {
  const char *name;
  const char *src_path;
  const char *reference_obj_out_path;
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

  OUTPUT_t *out = NULL;
  CompilerDiagnostic diag;
  compiler_diag_init(&diag);
  int8_t rc = compile_source_to_bytecode_diag(source, src_len, &out, &diag);
  ASSERT_EQ_INT(ERR_NOERROR, rc);
  ASSERT_EQ_INT(ERR_NOERROR, diag.code);
  ASSERT_NOT_NULL(out);

  write_output_file(tc->out_path, out);
  {
    char compile_cmd[512];
    int cmd_rc = snprintf(compile_cmd, sizeof(compile_cmd), "%s %s %s",
                          test_program_path("scomp"), tc->src_path,
                          tc->reference_obj_out_path);
    ASSERT_TRUE(cmd_rc > 0 && (size_t)cmd_rc < sizeof(compile_cmd));
    ASSERT_EQ_INT(0, system(compile_cmd));
  }
  assert_file_bytes_equal(tc->reference_obj_out_path, tc->out_path, tc->name);

  free(source);
  free(out->bytecode);
  free(out);
  compiler_diag_reset(&diag);
  remove(tc->reference_obj_out_path);
  remove(tc->out_path);
}

void test_parser_examples_obj_golden(void) {
  ParserObjGoldenCase cases[4] = {
      {"chat_boot", "docs/guide/examples/chat-boot.src", NULL, NULL},
      {"chat_load", "docs/guide/examples/chat-load.src", NULL, NULL},
      {"echo_boot", "docs/guide/examples/echo-boot.src", NULL, NULL},
      {"echo_load", "docs/guide/examples/echo-load.src", NULL, NULL},
  };
  char references[4][4096], outputs[4][4096];

  for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
    int ref_n = snprintf(references[i], sizeof references[i], "%s/%s.reference.obj",
                         test_temp_root(), cases[i].name);
    int out_n = snprintf(outputs[i], sizeof outputs[i], "%s/%s.generated.obj",
                         test_temp_root(), cases[i].name);
    ASSERT_TRUE(ref_n > 0 && (size_t)ref_n < sizeof references[i]);
    ASSERT_TRUE(out_n > 0 && (size_t)out_n < sizeof outputs[i]);
    cases[i].reference_obj_out_path = references[i];
    cases[i].out_path = outputs[i];
  }

  for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
    run_case(&cases[i]);
  }
}
