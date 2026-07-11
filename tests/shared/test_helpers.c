#include "test_helpers.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "compiler/compiler_pipeline.h"
#include "error.h"
#include "test_assert.h"

// Tests intentionally allocate heap buffers/strings (e.g. strdup/realloc)
// to mirror production ownership boundaries; call sites free these
// allocations in the same test scope.

AS_NODE *t_int(int64_t value) {
  char buf[32];
  snprintf(buf, sizeof(buf), "%lld", (long long)value);
  return as_new_valnode(V_INT, strdup(buf));
}

AS_NODE *t_local(const char *name) {
  return as_new_valnode(V_LOCAL, strdup(name));
}

AS_NODE *t_node(ENUM_NODE nodetype, void *lhs, void *rhs) {
  return as_new_node(nodetype, lhs, rhs);
}

AS_NODE *t_stmtlist_with_one(AS_NODE *stmt) {
  AS_NODE *list = as_new_stmtlist_node();
  return as_stmtlist_append(list, stmt);
}

IR_Unit *t_new_unit(void) {
  return ir_create_unit();
}

void t_emit(IR_Unit *unit, IR_Inst inst) {
  (void)ir_emit(unit, inst);
}

void t_bind(IR_Unit *unit, int32_t label_id) {
  (void)ir_bind_label(unit, label_id);
}

int8_t t_emit_bytecode(IR_Unit *unit, uint8_t local_count, uint8_t param_count,
                       OUTPUT_t *out, char **errdetail) {
  return emit_bytecode(unit, local_count, param_count, out, errdetail);
}


uint8_t hex_nibble(char c) {
  if (c >= '0' && c <= '9') return (uint8_t)(c - '0');
  if (c >= 'a' && c <= 'f') return (uint8_t)(10 + c - 'a');
  if (c >= 'A' && c <= 'F') return (uint8_t)(10 + c - 'A');
  return 0xFF;
}

uint8_t *load_hex_fixture(const char *path, size_t *out_len) {
  ASSERT_NOT_NULL(path);
  ASSERT_NOT_NULL(out_len);
  FILE *f = fopen(path, "rb");
  if (!f) {
    char alt[512];
    if (strncmp(path, "tests/", 6) == 0) {
      snprintf(alt, sizeof(alt), "%s", path + 6);
      f = fopen(alt, "rb");
    }
    if (!f) {
      snprintf(alt, sizeof(alt), "../%s", path);
      f = fopen(alt, "rb");
    }
  }
  ASSERT_NOT_NULL(f);
  uint8_t *buf = NULL;
  size_t cap = 0, len = 0;
  int c;
  while ((c = fgetc(f)) != EOF) {
    if (isspace(c)) continue;
    if (c == '#') {
      while ((c = fgetc(f)) != EOF && c != '\n') {
      }
      continue;
    }
    uint8_t hi = hex_nibble((char)c);
    ASSERT_TRUE(hi != 0xFF);
    int c2 = fgetc(f);
    ASSERT_TRUE(c2 != EOF);
    uint8_t lo = hex_nibble((char)c2);
    ASSERT_TRUE(lo != 0xFF);
    if (len == cap) {
      cap = cap ? cap * 2 : 32;
      buf = realloc(buf, cap);
      ASSERT_NOT_NULL(buf);
    }
    buf[len++] = (uint8_t)((hi << 4) | lo);
  }
  fclose(f);
  *out_len = len;
  return buf;
}

void assert_bytes_equal_with_diag(const uint8_t *expected, size_t expected_len,
                                  const uint8_t *actual, size_t actual_len,
                                  const char *context) {
  const char *ctx = context ? context : "byte-compare";
  if (expected_len != actual_len) {
    TEST_FAILF("%s length mismatch: expected len=%zu actual len=%zu first differing byte offset=0",
               ctx, expected_len, actual_len);
  }

  for (size_t i = 0; i < expected_len; i++) {
    if (expected[i] != actual[i]) {
      TEST_FAILF("%s byte mismatch: expected len=%zu actual len=%zu first differing byte offset=%zu (expected=0x%02x actual=0x%02x)",
                 ctx, expected_len, actual_len, i, expected[i], actual[i]);
    }
  }
}

void assert_file_bytes_equal(const char *expected_path, const char *actual_path,
                             const char *context) {
  FILE *expected_f = fopen(expected_path, "rb");
  ASSERT_NOT_NULL(expected_f);
  FILE *actual_f = fopen(actual_path, "rb");
  ASSERT_NOT_NULL(actual_f);

  ASSERT_EQ_INT(0, fseek(expected_f, 0, SEEK_END));
  long expected_n = ftell(expected_f);
  ASSERT_TRUE(expected_n >= 0);
  ASSERT_EQ_INT(0, fseek(expected_f, 0, SEEK_SET));

  ASSERT_EQ_INT(0, fseek(actual_f, 0, SEEK_END));
  long actual_n = ftell(actual_f);
  ASSERT_TRUE(actual_n >= 0);
  ASSERT_EQ_INT(0, fseek(actual_f, 0, SEEK_SET));

  size_t expected_len = (size_t)expected_n;
  size_t actual_len = (size_t)actual_n;
  uint8_t *expected = malloc(expected_len ? expected_len : 1);
  uint8_t *actual = malloc(actual_len ? actual_len : 1);
  ASSERT_NOT_NULL(expected);
  ASSERT_NOT_NULL(actual);

  ASSERT_EQ_INT((int)expected_len, (int)fread(expected, 1, expected_len, expected_f));
  ASSERT_EQ_INT((int)actual_len, (int)fread(actual, 1, actual_len, actual_f));
  fclose(expected_f);
  fclose(actual_f);

  assert_bytes_equal_with_diag(expected, expected_len, actual, actual_len, context);
  free(expected);
  free(actual);
}

void compile_source_and_assert_hex(const char *source, const char *fixture_path) {
  char *errdetail = NULL;
  OUTPUT_t *out = NULL;
  int8_t rc = compile_source_to_bytecode(source, strlen(source), &out, &errdetail);
  ASSERT_EQ_INT(ERR_NOERROR, rc);
  ASSERT_TRUE(errdetail == NULL);
  ASSERT_NOT_NULL(out);

  size_t expected_len = 0;
  uint8_t *expected = load_hex_fixture(fixture_path, &expected_len);
  size_t actual_len = (size_t)(out->nextbyte - out->bytecode);
  assert_bytes_equal_with_diag(expected, expected_len, out->bytecode, actual_len, fixture_path);

  free(expected);
  free(out->bytecode);
  free(out);
}

int run_command_and_capture(const char *cmd, char **captured_output) {
  ASSERT_NOT_NULL(cmd);
  ASSERT_NOT_NULL(captured_output);
  *captured_output = NULL;

  FILE *pipe = popen(cmd, "r");
  ASSERT_NOT_NULL(pipe);

  size_t cap = 256;
  size_t len = 0;
  char *buf = malloc(cap);
  ASSERT_NOT_NULL(buf);

  int c;
  while ((c = fgetc(pipe)) != EOF) {
    if (len + 1 >= cap) {
      cap *= 2;
      buf = realloc(buf, cap);
      ASSERT_NOT_NULL(buf);
    }
    buf[len++] = (char)c;
  }
  buf[len] = '\0';
  *captured_output = buf;
  return pclose(pipe);
}
