#include "test_helpers.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

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

int8_t t_emit_bytecode_diag(IR_Unit *unit, uint8_t local_count,
                            uint8_t param_count, OUTPUT_t *out,
                            CompilerDiagnostic *diag) {
  return emit_bytecode_diag(unit, local_count, param_count, out, diag);
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
  OUTPUT_t *out = NULL;
  CompilerDiagnostic diag;
  compiler_diag_init(&diag);
  int8_t rc = compile_source_to_bytecode_diag(source, strlen(source), &out,
                                              &diag);
  ASSERT_EQ_INT(ERR_NOERROR, rc);
  ASSERT_EQ_INT(ERR_NOERROR, diag.code);
  ASSERT_NOT_NULL(out);

  size_t expected_len = 0;
  uint8_t *expected = load_hex_fixture(fixture_path, &expected_len);
  size_t actual_len = (size_t)(out->nextbyte - out->bytecode);
  assert_bytes_equal_with_diag(expected, expected_len, out->bytecode, actual_len, fixture_path);

  free(expected);
  free(out->bytecode);
  free(out);
  compiler_diag_reset(&diag);
}

char *test_read_text_file(const char *path) {
  FILE *f = fopen(path, "rb");
  if (!f) return NULL;
  if (fseek(f, 0, SEEK_END) != 0) {
    fclose(f);
    return NULL;
  }
  long n = ftell(f);
  if (n < 0 || fseek(f, 0, SEEK_SET) != 0) {
    fclose(f);
    return NULL;
  }

  char *buf = malloc((size_t)n + 1);
  if (!buf) {
    fclose(f);
    return NULL;
  }
  size_t got = fread(buf, 1, (size_t)n, f);
  int close_rc = fclose(f);
  if (got != (size_t)n || close_rc != 0) {
    free(buf);
    return NULL;
  }
  buf[n] = '\0';
  return buf;
}

char *test_normalize_text(char *text) {
  size_t len = strlen(text);
  while (len > 0 && text[len - 1] == '\n') {
    text[--len] = '\0';
  }
  return text;
}

char *test_extract_fixture_block(const char *fixture, const char *header) {
  const char *start = strstr(fixture, header);
  if (!start) return NULL;
  start += strlen(header);
  const char *end = strstr(start, "\n===");
  if (!end) end = fixture + strlen(fixture);
  size_t len = (size_t)(end - start);
  char *out = malloc(len + 1);
  if (!out) return NULL;
  memcpy(out, start, len);
  out[len] = '\0';
  return out;
}

int test_contains_all_lines(const char *expected_lines, const char *actual,
                            int *missing_line) {
  char *copy = strdup(expected_lines);
  if (!copy) return 0;
  int line = 0;
  for (char *tok = strtok(copy, "\n"); tok; tok = strtok(NULL, "\n")) {
    line++;
    if (tok[0] == '\0') continue;
    if (!strstr(actual, tok)) {
      if (missing_line) *missing_line = line;
      free(copy);
      return 0;
    }
  }
  free(copy);
  return 1;
}

static int append_capture(char **text, size_t *length, const char *data,
                          size_t data_length) {
  if (data_length > SIZE_MAX - *length - 1) return -1;
  size_t needed = *length + data_length + 1;
  char *grown = realloc(*text, needed);
  if (!grown) return -1;
  memcpy(grown + *length, data, data_length);
  *length += data_length;
  grown[*length] = '\0';
  *text = grown;
  return 0;
}

static int64_t monotonic_ms(void) {
  struct timespec ts;
  if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) return -1;
  return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static void close_fd(int *fd) {
  if (*fd >= 0) {
    close(*fd);
    *fd = -1;
  }
}

static void kill_and_reap(pid_t pid, int *status) {
  if (pid <= 0) return;
  if (kill(pid, SIGKILL) != 0 && errno != ESRCH) {
    /* waitpid below still prevents leaving a child behind. */
  }
  while (waitpid(pid, status, 0) < 0 && errno == EINTR) {
  }
}

static int read_capture_fd(int *fd, char **text, size_t *length) {
  char buffer[4096];
  ssize_t got = read(*fd, buffer, sizeof(buffer));
  if (got > 0) return append_capture(text, length, buffer, (size_t)got);
  if (got == 0) {
    close_fd(fd);
    return 0;
  }
  if (errno == EINTR) return 0;
  return -1;
}

void test_process_result_free(TestProcessResult *result) {
  if (!result) return;
  free(result->stdout_text);
  free(result->stderr_text);
  result->stdout_text = NULL;
  result->stdout_length = 0;
  result->stderr_text = NULL;
  result->stderr_length = 0;
  result->exit_code = -1;
  result->timed_out = 0;
}

int test_make_temp_path(const char *prefix, char *path, size_t path_size) {
  const char *root = test_temp_root();
  if (!prefix || !path || path_size == 0) return -1;
  int written = snprintf(path, path_size, "%s/%s-XXXXXX", root, prefix);
  if (written < 0 || (size_t)written >= path_size) return -1;
  int fd = mkstemp(path);
  if (fd < 0) return -1;
  if (close(fd) != 0 || unlink(path) != 0) {
    unlink(path);
    return -1;
  }
  return 0;
}

const char *test_temp_root(void) {
  const char *root = getenv("SIN_TEST_TMP_ROOT");
  return root && root[0] ? root : "/tmp";
}

int test_temp_template(char *path, size_t path_size, const char *prefix) {
  const char *root = test_temp_root();
  int written;
  if (!path || path_size == 0 || !prefix || !prefix[0]) return -1;
  written = snprintf(path, path_size, "%s/%s-XXXXXX", root, prefix);
  return written < 0 || (size_t)written >= path_size ? -1 : 0;
}

const char *test_program_path(const char *program) {
  const char *path;
  if (!program) return NULL;
  if (strcmp(program, "scomp") == 0) path = getenv("SIN_TEST_SCOMP");
  else if (strcmp(program, "sdiss") == 0) path = getenv("SIN_TEST_SDISS");
  else if (strcmp(program, "sin") == 0) path = getenv("SIN_TEST_SIN");
  else if (strcmp(program, "sconv") == 0) path = getenv("SIN_TEST_SCONV");
  else return NULL;
  if (path && path[0]) return path;
  if (strcmp(program, "scomp") == 0) return "./scomp";
  if (strcmp(program, "sdiss") == 0) return "./sdiss";
  if (strcmp(program, "sin") == 0) return "./sin";
  return "./sconv";
}

static int test_run_argv_capture_impl(char *const argv[], const void *stdin_data,
                                      size_t stdin_length, bool provide_stdin,
                                      unsigned timeout_ms,
                                      TestProcessResult *result) {
  if (!argv || !argv[0] || !result || (stdin_length > 0 && !stdin_data)) {
    return -1;
  }
  result->stdout_text = NULL;
  result->stdout_length = 0;
  result->stderr_text = NULL;
  result->stderr_length = 0;
  result->exit_code = -1;
  result->timed_out = 0;

  int stdin_pipe[2] = {-1, -1};
  int stdout_pipe[2] = {-1, -1};
  int stderr_pipe[2] = {-1, -1};
  if ((provide_stdin && pipe(stdin_pipe) != 0) || pipe(stdout_pipe) != 0 ||
      pipe(stderr_pipe) != 0) {
    close_fd(&stdin_pipe[0]);
    close_fd(&stdin_pipe[1]);
    close_fd(&stdout_pipe[0]);
    close_fd(&stdout_pipe[1]);
    close_fd(&stderr_pipe[0]);
    close_fd(&stderr_pipe[1]);
    return -1;
  }

  pid_t pid = fork();
  if (pid < 0) {
    close_fd(&stdin_pipe[0]);
    close_fd(&stdin_pipe[1]);
    close_fd(&stdout_pipe[0]);
    close_fd(&stdout_pipe[1]);
    close_fd(&stderr_pipe[0]);
    close_fd(&stderr_pipe[1]);
    return -1;
  }
  if (pid == 0) {
    if (provide_stdin) {
      close(stdin_pipe[1]);
      if (dup2(stdin_pipe[0], STDIN_FILENO) < 0) _exit(127);
      close(stdin_pipe[0]);
    } else {
      int null_fd = open("/dev/null", O_RDONLY);
      if (null_fd < 0 || dup2(null_fd, STDIN_FILENO) < 0) _exit(127);
      if (null_fd != STDIN_FILENO) close(null_fd);
    }
    close(stdout_pipe[0]);
    close(stderr_pipe[0]);
    if (dup2(stdout_pipe[1], STDOUT_FILENO) < 0 ||
        dup2(stderr_pipe[1], STDERR_FILENO) < 0) {
      _exit(127);
    }
    close(stdout_pipe[1]);
    close(stderr_pipe[1]);
    execvp(argv[0], argv);
    _exit(127);
  }

  close_fd(&stdin_pipe[0]);
  close_fd(&stdout_pipe[1]);
  close_fd(&stderr_pipe[1]);
  bool failed = false;
  bool sigpipe_ignored = false;
  struct sigaction old_sigpipe;
  if (provide_stdin) {
    int flags = fcntl(stdin_pipe[1], F_GETFL, 0);
    if (flags < 0 || fcntl(stdin_pipe[1], F_SETFL, flags | O_NONBLOCK) < 0) {
      failed = true;
    }
    struct sigaction ignore_sigpipe;
    memset(&ignore_sigpipe, 0, sizeof(ignore_sigpipe));
    ignore_sigpipe.sa_handler = SIG_IGN;
    sigemptyset(&ignore_sigpipe.sa_mask);
    if (!failed && sigaction(SIGPIPE, &ignore_sigpipe, &old_sigpipe) == 0) {
      sigpipe_ignored = true;
    } else if (!failed) {
      failed = true;
    }
    if (stdin_length == 0) close_fd(&stdin_pipe[1]);
  }

  size_t stdout_length = 0;
  size_t stderr_length = 0;
  size_t stdin_offset = 0;
  bool child_reaped = false;
  bool timed_out = false;
  int status = 0;
  int64_t deadline = timeout_ms ? monotonic_ms() + (int64_t)timeout_ms : -1;
  if (timeout_ms && deadline < 0) failed = true;

  while (!failed && (!child_reaped || stdin_pipe[1] >= 0 ||
                     stdout_pipe[0] >= 0 || stderr_pipe[0] >= 0)) {
    struct pollfd fds[3];
    int poll_kind[3];
    nfds_t nfds = 0;
    if (stdin_pipe[1] >= 0) {
      fds[nfds].fd = stdin_pipe[1];
      fds[nfds].events = POLLOUT;
      fds[nfds].revents = 0;
      poll_kind[nfds] = 0;
      nfds++;
    }
    if (stdout_pipe[0] >= 0) {
      fds[nfds].fd = stdout_pipe[0];
      fds[nfds].events = POLLIN;
      fds[nfds].revents = 0;
      poll_kind[nfds] = 1;
      nfds++;
    }
    if (stderr_pipe[0] >= 0) {
      fds[nfds].fd = stderr_pipe[0];
      fds[nfds].events = POLLIN;
      fds[nfds].revents = 0;
      poll_kind[nfds] = 2;
      nfds++;
    }

    int wait_ms = 50;
    if (deadline >= 0) {
      int64_t remaining = deadline - monotonic_ms();
      if (remaining <= 0) {
        if (!child_reaped) {
          kill_and_reap(pid, &status);
          child_reaped = true;
          timed_out = true;
        }
        deadline = -1;
        close_fd(&stdin_pipe[1]);
      }
      if (!timed_out && remaining < wait_ms) wait_ms = (int)remaining;
    }
    int poll_rc = nfds ? poll(fds, nfds, wait_ms) : poll(NULL, 0, wait_ms);
    if (poll_rc < 0 && errno != EINTR) {
      failed = true;
      break;
    }
    if (poll_rc > 0) {
      for (nfds_t index = 0; index < nfds && !failed; index++) {
        short revents = fds[index].revents;
        if (poll_kind[index] == 0) {
          if (revents & (POLLERR | POLLHUP | POLLNVAL)) {
            close_fd(&stdin_pipe[1]);
            if (stdin_offset < stdin_length && !child_reaped) failed = true;
          } else if (revents & POLLOUT) {
            ssize_t written = write(stdin_pipe[1],
                                    (const char *)stdin_data + stdin_offset,
                                    stdin_length - stdin_offset);
            if (written > 0) {
              stdin_offset += (size_t)written;
              if (stdin_offset == stdin_length) close_fd(&stdin_pipe[1]);
            } else if (written < 0 && errno != EINTR && errno != EAGAIN &&
                       errno != EWOULDBLOCK) {
              failed = true;
            }
          }
        } else if (poll_kind[index] == 1) {
          if (revents & (POLLIN | POLLHUP)) {
            if (read_capture_fd(&stdout_pipe[0], &result->stdout_text,
                                &stdout_length) != 0) failed = true;
          }
        } else if (revents & (POLLIN | POLLHUP)) {
          if (read_capture_fd(&stderr_pipe[0], &result->stderr_text,
                              &stderr_length) != 0) failed = true;
        }
      }
    }

    if (!child_reaped) {
      pid_t waited = waitpid(pid, &status, WNOHANG);
      if (waited == pid) {
        child_reaped = true;
        close_fd(&stdin_pipe[1]);
      }
      else if (waited < 0 && errno != EINTR) failed = true;
    }
  }

  if (failed) {
    if (!child_reaped) {
      kill_and_reap(pid, &status);
      child_reaped = true;
    }
  } else if (!child_reaped) {
    while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {
    }
  }
  close_fd(&stdin_pipe[1]);
  close_fd(&stdout_pipe[0]);
  close_fd(&stderr_pipe[0]);
  if (sigpipe_ignored) sigaction(SIGPIPE, &old_sigpipe, NULL);
  if (failed) {
    test_process_result_free(result);
    return -1;
  }

  if (!result->stdout_text) result->stdout_text = strdup("");
  if (!result->stderr_text) result->stderr_text = strdup("");
  if (!result->stdout_text || !result->stderr_text) {
    test_process_result_free(result);
    return -1;
  }
  if (WIFEXITED(status)) result->exit_code = WEXITSTATUS(status);
  else if (WIFSIGNALED(status)) result->exit_code = 128 + WTERMSIG(status);
  result->stdout_length = stdout_length;
  result->stderr_length = stderr_length;
  result->timed_out = timed_out ? 1 : 0;
  return 0;
}

int test_run_argv_capture(char *const argv[], unsigned timeout_ms,
                          TestProcessResult *result) {
  return test_run_argv_capture_impl(argv, NULL, 0, false, timeout_ms, result);
}

int test_run_argv_capture_with_stdin(char *const argv[], const void *stdin_data,
                                     size_t stdin_length, unsigned timeout_ms,
                                     TestProcessResult *result) {
  return test_run_argv_capture_impl(argv, stdin_data, stdin_length, true,
                                    timeout_ms, result);
}
