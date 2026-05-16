#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "test_assert.h"

typedef struct {
  const char *name;
  const char *src_path;
  const char *golden_obj_path;
  const char *generated_obj_path;
  const char *fixture_path;
} InterpretGoldenCase;

typedef struct {
  char *stdout_text;
  char *stderr_text;
  int exit_code;
} RunResult;

static char *read_text_file(const char *path) {
  FILE *f = fopen(path, "rb");
  ASSERT_NOT_NULL(f);
  ASSERT_EQ_INT(0, fseek(f, 0, SEEK_END));
  long n = ftell(f);
  ASSERT_TRUE(n >= 0);
  ASSERT_EQ_INT(0, fseek(f, 0, SEEK_SET));
  char *buf = malloc((size_t)n + 1);
  ASSERT_NOT_NULL(buf);
  size_t got = fread(buf, 1, (size_t)n, f);
  ASSERT_EQ_INT((int)n, (int)got);
  buf[n] = '\0';
  fclose(f);
  return buf;
}

static char *extract_block(const char *fixture, const char *header) {
  const char *start = strstr(fixture, header);
  ASSERT_NOT_NULL(start);
  start += strlen(header);
  const char *end = strstr(start, "\n===");
  if (!end) {
    end = fixture + strlen(fixture);
  }
  size_t len = (size_t)(end - start);
  char *out = malloc(len + 1);
  ASSERT_NOT_NULL(out);
  memcpy(out, start, len);
  out[len] = '\0';
  return out;
}

static char *normalize_text(char *text) {
  size_t len = strlen(text);
  while (len > 0 && text[len - 1] == "\n"[0]) {
    text[--len] = '\0';
  }
  return text;
}

static int contains_all_lines(const char *expected_lines, const char *actual, int *missing_line) {
  char *copy = strdup(expected_lines);
  ASSERT_NOT_NULL(copy);
  int line = 0;
  for (char *tok = strtok(copy, "\n"); tok; tok = strtok(NULL, "\n")) {
    line++;
    if (tok[0] == '\0') continue;
    if (!strstr(actual, tok)) {
      *missing_line = line;
      free(copy);
      return 0;
    }
  }
  free(copy);
  return 1;
}

static RunResult run_and_capture(const char *cmd_base, const char *tag) {
  char out_path[256];
  char err_path[256];
  char cmd[1024];
  int rc = snprintf(out_path, sizeof(out_path), "tests/fixtures/interpret/%s.stdout.tmp", tag);
  ASSERT_TRUE(rc > 0 && (size_t)rc < sizeof(out_path));
  rc = snprintf(err_path, sizeof(err_path), "tests/fixtures/interpret/%s.stderr.tmp", tag);
  ASSERT_TRUE(rc > 0 && (size_t)rc < sizeof(err_path));
  rc = snprintf(cmd, sizeof(cmd), "%s > %s 2> %s", cmd_base, out_path, err_path);
  ASSERT_TRUE(rc > 0 && (size_t)rc < sizeof(cmd));

  int sysrc = system(cmd);
  ASSERT_TRUE(sysrc >= 0);

  RunResult result;
  result.stdout_text = normalize_text(read_text_file(out_path));
  result.stderr_text = normalize_text(read_text_file(err_path));
  result.exit_code = (sysrc >> 8) & 0xFF;

  remove(out_path);
  remove(err_path);
  return result;
}

static void assert_run_matches(const char *case_name, const char *variant,
                               const RunResult *actual, const RunResult *expected) {
  if (expected->exit_code >= 0 && actual->exit_code != expected->exit_code) {
    fprintf(stderr,
            "[%s/%s] mismatch exit code: expected=%d actual=%d\n",
            case_name, variant, expected->exit_code, actual->exit_code);
    ASSERT_EQ_INT(expected->exit_code, actual->exit_code);
  }

  int line = -1;
  if (!contains_all_lines(expected->stdout_text, actual->stdout_text, &line)) {
    fprintf(stderr,
            "[%s/%s] mismatch stdout: expected marker line %d not found\n",
            case_name, variant, line);
    ASSERT_TRUE(0);
  }

  line = -1;
  if (!contains_all_lines(expected->stderr_text, actual->stderr_text, &line)) {
    fprintf(stderr,
            "[%s/%s] mismatch stderr: expected marker line %d not found\n",
            case_name, variant, line);
    ASSERT_TRUE(0);
  }
}

static void run_case(const InterpretGoldenCase *tc) {
  char compile_cmd[512];
  int rc = snprintf(compile_cmd, sizeof(compile_cmd), "./scomp %s %s", tc->src_path, tc->generated_obj_path);
  ASSERT_TRUE(rc > 0 && (size_t)rc < sizeof(compile_cmd));
  ASSERT_EQ_INT(0, system(compile_cmd));

  char *fixture = read_text_file(tc->fixture_path);
  char *expected_stdout = extract_block(fixture, "===stdout===\n");
  char *expected_stderr = extract_block(fixture, "===stderr===\n");
  char *expected_exit = extract_block(fixture, "===exit===\n");

  normalize_text(expected_stdout);
  normalize_text(expected_stderr);

  RunResult expected = {.stdout_text = expected_stdout,
                        .stderr_text = expected_stderr,
                        .exit_code = atoi(expected_exit)};

  char generated_cmd[512];
  rc = snprintf(generated_cmd, sizeof(generated_cmd), "timeout 2s ./sin -o %s", tc->generated_obj_path);
  ASSERT_TRUE(rc > 0 && (size_t)rc < sizeof(generated_cmd));
  RunResult generated = run_and_capture(generated_cmd, "generated");
  assert_run_matches(tc->name, "generated_obj", &generated, &expected);

  char golden_cmd[512];
  rc = snprintf(golden_cmd, sizeof(golden_cmd), "timeout 2s ./sin -o %s", tc->golden_obj_path);
  ASSERT_TRUE(rc > 0 && (size_t)rc < sizeof(golden_cmd));
  RunResult golden = run_and_capture(golden_cmd, "golden");
  assert_run_matches(tc->name, "golden_obj", &golden, &expected);

  free(generated.stdout_text);
  free(generated.stderr_text);
  free(golden.stdout_text);
  free(golden.stderr_text);
  free(expected_stdout);
  free(expected_stderr);
  free(expected_exit);
  free(fixture);

  remove(tc->generated_obj_path);
}

void test_interpret_semantics_golden(void) {
  const InterpretGoldenCase cases[] = {
      {"chat_boot", "examples/chat-boot.src", "examples/chat-boot.obj", "tests/fixtures/interpret/chat-boot.generated.obj", "tests/fixtures/interpret/chat-boot.expected.txt"},
      {"chat_load", "examples/chat-load.src", "examples/chat-load.obj", "tests/fixtures/interpret/chat-load.generated.obj", "tests/fixtures/interpret/chat-load.expected.txt"},
      {"echo_boot", "examples/echo-boot.src", "examples/echo-boot.obj", "tests/fixtures/interpret/echo-boot.generated.obj", "tests/fixtures/interpret/echo-boot.expected.txt"},
      {"echo_load", "examples/echo-load.src", "examples/echo-load.obj", "tests/fixtures/interpret/echo-load.generated.obj", "tests/fixtures/interpret/echo-load.expected.txt"},
  };

  for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
    run_case(&cases[i]);
  }
}
