#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "test_assert.h"

typedef struct {
  const char *name;
  const char *src_path;
  const char *obj_path;
  const char *expected_path;
} StressCase;

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

static char *normalize_text(char *text) {
  size_t len = strlen(text);
  while (len > 0 && text[len - 1] == '\n') {
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

static RunResult run_obj_capture(const char *obj_path, const char *tag) {
  char out_path[256];
  char err_path[256];
  int rc = snprintf(out_path, sizeof(out_path), "tests/fixtures/interpret/%s.stdout.tmp", tag);
  ASSERT_TRUE(rc > 0 && (size_t)rc < sizeof(out_path));
  rc = snprintf(err_path, sizeof(err_path), "tests/fixtures/interpret/%s.stderr.tmp", tag);
  ASSERT_TRUE(rc > 0 && (size_t)rc < sizeof(err_path));

  pid_t pid = fork();
  ASSERT_TRUE(pid >= 0);
  if (pid == 0) {
    FILE *out = fopen(out_path, "wb");
    FILE *err = fopen(err_path, "wb");
    if (!out || !err) _exit(127);
    if (dup2(fileno(out), STDOUT_FILENO) < 0 || dup2(fileno(err), STDERR_FILENO) < 0) _exit(127);
    fclose(out);
    fclose(err);
    execl("./sin", "./sin", "-o", obj_path, (char *)NULL);
    _exit(127);
  }

  int status = 0;
  ASSERT_EQ_INT(pid, waitpid(pid, &status, 0));

  RunResult result;
  result.stdout_text = normalize_text(read_text_file(out_path));
  result.stderr_text = normalize_text(read_text_file(err_path));
  if (WIFEXITED(status)) result.exit_code = WEXITSTATUS(status);
  else if (WIFSIGNALED(status)) result.exit_code = 128 + WTERMSIG(status);
  else result.exit_code = -1;

  remove(out_path);
  remove(err_path);
  return result;
}

static void free_result(RunResult *r) {
  free(r->stdout_text);
  free(r->stderr_text);
}

static char *extract_block(const char *fixture, const char *header) {
  const char *start = strstr(fixture, header);
  ASSERT_NOT_NULL(start);
  start += strlen(header);
  const char *end = strstr(start, "\n===");
  if (!end) end = fixture + strlen(fixture);
  size_t len = (size_t)(end - start);
  char *out = malloc(len + 1);
  ASSERT_NOT_NULL(out);
  memcpy(out, start, len);
  out[len] = "\0"[0];
  return out;
}

void test_interpret_stress(void) {
  const StressCase cases[] = {
      {"chat_boot", "examples/chat-boot.src", "tests/fixtures/interpret/chat-boot.stress.obj", "tests/fixtures/interpret/chat-boot.expected.txt"},
      {"echo_boot", "examples/echo-boot.src", "tests/fixtures/interpret/echo-boot.stress.obj", "tests/fixtures/interpret/echo-boot.expected.txt"},
  };
  const int iterations = 30;

  RunResult baselines[sizeof(cases) / sizeof(cases[0])];
  memset(baselines, 0, sizeof(baselines));

  for (int iter = 0; iter < iterations; ++iter) {
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
      char compile_cmd[512];
      int rc = snprintf(compile_cmd, sizeof(compile_cmd), "./scomp %s %s", cases[i].src_path,
                        cases[i].obj_path);
      ASSERT_TRUE(rc > 0 && (size_t)rc < sizeof(compile_cmd));
      ASSERT_EQ_INT(0, system(compile_cmd));

      char tag[128];
      rc = snprintf(tag, sizeof(tag), "stress_%s_iter_%d", cases[i].name, iter);
      ASSERT_TRUE(rc > 0 && (size_t)rc < sizeof(tag));
      RunResult current = run_obj_capture(cases[i].obj_path, tag);

      char *fixture = read_text_file(cases[i].expected_path);
      char *expected_stdout = normalize_text(extract_block(fixture, "===stdout===\n"));
      char *expected_stderr = normalize_text(extract_block(fixture, "===stderr===\n"));
      char *expected_exit_text = extract_block(fixture, "===exit===\n");
      int expected_exit = atoi(expected_exit_text);

      int missing_line = -1;
      if (expected_exit >= 0) {
        ASSERT_EQ_INT(expected_exit, current.exit_code);
      } else {
        ASSERT_TRUE(current.exit_code != 0);
      }
      ASSERT_TRUE(contains_all_lines(expected_stdout, current.stdout_text, &missing_line));
      ASSERT_TRUE(contains_all_lines(expected_stderr, current.stderr_text, &missing_line));

      free(expected_stdout);
      free(expected_stderr);
      free(expected_exit_text);
      free(fixture);

      if (iter == 0) {
        baselines[i] = current;
      } else {
        ASSERT_EQ_INT(baselines[i].exit_code, current.exit_code);
        ASSERT_EQ_INT(0, strcmp(baselines[i].stdout_text, current.stdout_text));
        ASSERT_EQ_INT(0, strcmp(baselines[i].stderr_text, current.stderr_text));
        free_result(&current);
      }

      remove(cases[i].obj_path);
    }
  }

  for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
    free_result(&baselines[i]);
  }
}
