#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "config.h"
#include "error.h"
#include "interpret.h"
#include "item.h"
#include "test_assert.h"
#include "value.h"
#include "vm.h"

extern CONFIG_t config;

typedef struct {
  const char *name;
  const char *src_path;
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

static RunResult run_and_capture_obj(const char *obj_path, const char *tag) {
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
    if (!out || !err) {
      _exit(127);
    }
    if (dup2(fileno(out), STDOUT_FILENO) < 0 || dup2(fileno(err), STDERR_FILENO) < 0) {
      _exit(127);
    }
    fclose(out);
    fclose(err);
    execl("./sin", "./sin", "-o", obj_path, (char *)NULL);
    _exit(127);
  }

  int status = 0;
  int waited = 0;
  while (waited < 20) {
    pid_t w = waitpid(pid, &status, WNOHANG);
    ASSERT_TRUE(w >= 0);
    if (w == pid) {
      break;
    }
    usleep(100000);
    waited++;
  }
  if (waited >= 20) {
    kill(pid, SIGKILL);
    ASSERT_EQ_INT(pid, waitpid(pid, &status, 0));
  }

  RunResult result;
  result.stdout_text = normalize_text(read_text_file(out_path));
  result.stderr_text = normalize_text(read_text_file(err_path));
  if (WIFEXITED(status)) {
    result.exit_code = WEXITSTATUS(status);
  } else if (WIFSIGNALED(status)) {
    result.exit_code = 128 + WTERMSIG(status);
  } else {
    result.exit_code = -1;
  }

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

  RunResult generated = run_and_capture_obj(tc->generated_obj_path, "generated");
  assert_run_matches(tc->name, "generated_obj", &generated, &expected);

  free(generated.stdout_text);
  free(generated.stderr_text);
  free(expected_stdout);
  free(expected_stderr);
  free(expected_exit);
  free(fixture);

  remove(tc->generated_obj_path);
}

void test_interpret_semantics_golden(void) {
  const InterpretGoldenCase cases[] = {
      {"chat_boot", "examples/chat-boot.src", "tests/fixtures/interpret/chat-boot.generated.obj", "tests/fixtures/interpret/chat-boot.expected.txt"},
      {"chat_load", "examples/chat-load.src", "tests/fixtures/interpret/chat-load.generated.obj", "tests/fixtures/interpret/chat-load.expected.txt"},
      {"echo_boot", "examples/echo-boot.src", "tests/fixtures/interpret/echo-boot.generated.obj", "tests/fixtures/interpret/echo-boot.expected.txt"},
      {"echo_load", "examples/echo-load.src", "tests/fixtures/interpret/echo-load.generated.obj", "tests/fixtures/interpret/echo-load.expected.txt"},
  };

  for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
    run_case(&cases[i]);
  }
}


void test_interpret_rejects_malformed_bytecode_before_execution(void) {
  memset(&config, 0, sizeof(config));
  init_errmsg();
  config.strict_validation = true;
  config.itemroot = make_root_item("root");
  ASSERT_NOT_NULL(config.itemroot);
  config.vm = make_vm();
  ASSERT_NOT_NULL(config.vm);

  uint8_t bytecode[] = {0, 0, 'l', 3, 0, 'a'};
  uint8_t *owned = malloc(sizeof(bytecode));
  ASSERT_NOT_NULL(owned);
  memcpy(owned, bytecode, sizeof(bytecode));
  ITEM_t *code = insert_code_item(config.itemroot, "malformed", sizeof(bytecode), owned);
  ASSERT_NOT_NULL(code);

  VALUE_t result = interpret(code);
  ASSERT_EQ_INT(VALUE_nil, result.type);
  ITEM_t *err = find_item(config.itemroot, "error");
  ASSERT_NOT_NULL(err);
  ASSERT_EQ_INT(ERR_RUNTIME_BYTECODE, err->value.i);
  ITEM_t *msg = find_item(config.itemroot, "error.msg");
  ASSERT_NOT_NULL(msg);
  ASSERT_EQ_INT(VALUE_str, msg->value.type);
  ASSERT_TRUE(strstr(msg->value.s, "Runtime bytecode validation failed") != NULL);
  ASSERT_TRUE(strstr(msg->value.s, "truncated") != NULL);

  destroy_vm(config.vm);
  destroy_item(config.itemroot);
  memset(&config, 0, sizeof(config));
}
