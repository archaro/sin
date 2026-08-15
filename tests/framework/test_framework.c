#include "test_framework.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "item.h"
#include "item_internal.h"
#include "memory.h"

static const char *g_program_path;
static bool g_io_write_failure;
static bool g_io_close_failure;
static bool g_io_sync_failure;

static void tf_detail(char *detail, size_t size, const char *format,
                      const char *value) {
  if (!detail || size == 0) return;
  if (value) (void)snprintf(detail, size, "%s%s", format, value);
  else (void)snprintf(detail, size, "%s", format);
}

static bool tf_token(const char *text, bool comma_separated) {
  const unsigned char *p = (const unsigned char *)text;
  bool have = false;
  bool previous_comma = true;
  if (!text) return false;
  while (*p) {
    if (*p == ',') {
      if (!comma_separated || previous_comma) return false;
      previous_comma = true;
      p++;
      continue;
    }
    if (!((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
          (*p >= '0' && *p <= '9') || *p == '_' || *p == '-' || *p == '.' ||
          *p == ':')) return false;
    have = true;
    previous_comma = false;
    p++;
  }
  return have && !previous_comma;
}

int tf_validate_descriptors(const TF_TestDescriptor *tests, size_t count,
                            char *detail, size_t detail_size) {
  if (!tests && count != 0) {
    tf_detail(detail, detail_size, "descriptor array is NULL", NULL);
    return -1;
  }
  for (size_t i = 0; i < count; i++) {
    const TF_TestDescriptor *test = &tests[i];
    if (!tf_token(test->id, false)) {
      tf_detail(detail, detail_size, "invalid test ID: ",
                test->id ? test->id : "(null)");
      return -1;
    }
    if (!test->fn) {
      tf_detail(detail, detail_size, "test has NULL function: ", test->id);
      return -1;
    }
    if (!test->tags || (!tf_token(test->tags, true) && test->tags[0] != '\0')) {
      tf_detail(detail, detail_size, "invalid tags for: ", test->id);
      return -1;
    }
    if (!test->contracts || !tf_token(test->contracts, true)) {
      tf_detail(detail, detail_size, "invalid contracts for: ", test->id);
      return -1;
    }
    for (size_t j = 0; j < i; j++) {
      if (strcmp(test->id, tests[j].id) == 0) {
        tf_detail(detail, detail_size, "duplicate test ID: ", test->id);
        return -1;
      }
    }
  }
  return 0;
}

static void tf_write_record(const char *kind, const char *id, const char *a,
                            const char *b, const char *c) {
  (void)printf("TF|%s|%s|%s|%s|%s\n", kind, id ? id : "", a ? a : "",
               b ? b : "", c ? c : "");
}

static uint64_t tf_now_ms(void) {
  struct timespec ts;
  (void)clock_gettime(CLOCK_MONOTONIC, &ts);
  return (uint64_t)ts.tv_sec * UINT64_C(1000) + (uint64_t)ts.tv_nsec / UINT64_C(1000000);
}

static void tf_append(char **data, size_t *length, const char *buffer,
                      size_t count) {
  char *grown = realloc(*data, *length + count + 1);
  if (!grown) return;
  memcpy(grown + *length, buffer, count);
  *length += count;
  grown[*length] = '\0';
  *data = grown;
}

static int tf_set_nonblock(int fd) {
  int flags = fcntl(fd, F_GETFL, 0);
  return flags < 0 ? -1 : fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

int tf_process_run(char *const argv[], unsigned timeout_ms,
                   TF_ProcessResult *result) {
  int out_pipe[2], err_pipe[2];
  pid_t pid;
  int status = 0;
  bool out_open = true, err_open = true, child_reaped = false;
  uint64_t deadline;
  if (!argv || !argv[0] || !result) return -1;
  memset(result, 0, sizeof *result);
  out_pipe[0] = out_pipe[1] = err_pipe[0] = err_pipe[1] = -1;
  if (pipe(out_pipe) < 0 || pipe(err_pipe) < 0) {
    if (out_pipe[0] >= 0) { (void)close(out_pipe[0]); (void)close(out_pipe[1]); }
    if (err_pipe[0] >= 0) { (void)close(err_pipe[0]); (void)close(err_pipe[1]); }
    return -1;
  }
  pid = fork();
  if (pid < 0) return -1;
  if (pid == 0) {
    (void)setpgid(0, 0);
    (void)dup2(out_pipe[1], STDOUT_FILENO);
    (void)dup2(err_pipe[1], STDERR_FILENO);
    (void)close(out_pipe[0]); (void)close(out_pipe[1]);
    (void)close(err_pipe[0]); (void)close(err_pipe[1]);
    execvp(argv[0], argv);
    dprintf(STDERR_FILENO, "exec %s: %s\n", argv[0], strerror(errno));
    _exit(127);
  }
  (void)setpgid(pid, pid);
  (void)close(out_pipe[1]); (void)close(err_pipe[1]);
  (void)tf_set_nonblock(out_pipe[0]); (void)tf_set_nonblock(err_pipe[0]);
  deadline = tf_now_ms() + (timeout_ms ? timeout_ms : 30000u);
  while (out_open || err_open || !child_reaped) {
    struct pollfd pollfds[2];
    int nfds = 0;
    int wait_result;
    if (out_open) { pollfds[nfds].fd = out_pipe[0]; pollfds[nfds].events = POLLIN; nfds++; }
    if (err_open) { pollfds[nfds].fd = err_pipe[0]; pollfds[nfds].events = POLLIN; nfds++; }
    if (nfds) {
      int timeout = (int)((deadline > tf_now_ms()) ? deadline - tf_now_ms() : 0);
      (void)poll(pollfds, (nfds_t)nfds, timeout > 50 ? 50 : timeout);
      char buffer[4096];
      ssize_t got;
      if (out_open) {
        while ((got = read(out_pipe[0], buffer, sizeof buffer)) > 0) tf_append(&result->stdout_data, &result->stdout_len, buffer, (size_t)got);
        if (got == 0) { (void)close(out_pipe[0]); out_open = false; }
      }
      if (err_open) {
        while ((got = read(err_pipe[0], buffer, sizeof buffer)) > 0) tf_append(&result->stderr_data, &result->stderr_len, buffer, (size_t)got);
        if (got == 0) { (void)close(err_pipe[0]); err_open = false; }
      }
    }
    wait_result = waitpid(pid, &status, WNOHANG);
    if (wait_result == pid) child_reaped = true;
    if (!child_reaped && tf_now_ms() >= deadline) {
      result->timed_out = true;
      (void)kill(-pid, SIGKILL);
      (void)waitpid(pid, &status, 0);
      child_reaped = true;
    }
  }
  if (WIFEXITED(status)) { result->exited = true; result->exit_status = WEXITSTATUS(status); }
  if (WIFSIGNALED(status)) { result->signaled = true; result->signal_number = WTERMSIG(status); }
  return 0;
}

void tf_process_result_destroy(TF_ProcessResult *result) {
  if (!result) return;
  free(result->stdout_data); free(result->stderr_data);
  memset(result, 0, sizeof *result);
}

static int tf_remove_tree(const char *path) {
  struct stat st;
  if (lstat(path, &st) < 0) return errno == ENOENT ? 0 : -1;
  if (!S_ISDIR(st.st_mode)) return unlink(path);
  DIR *dir = opendir(path);
  if (!dir) return -1;
  struct dirent *entry;
  while ((entry = readdir(dir)) != NULL) {
    char child[4096];
    if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;
    if (snprintf(child, sizeof child, "%s/%s", path, entry->d_name) >= (int)sizeof child || tf_remove_tree(child) < 0) {
      (void)closedir(dir); return -1;
    }
  }
  (void)closedir(dir);
  return rmdir(path);
}

void tf_fixture_init(TF_Fixture *fixture) {
  if (!fixture) return;
  memset(fixture, 0, sizeof *fixture);
  (void)snprintf(fixture->path, sizeof fixture->path, "/tmp/sin-test-XXXXXX");
  fixture->active = mkdtemp(fixture->path) != NULL;
}

const char *tf_fixture_path(const TF_Fixture *fixture) {
  return fixture && fixture->active ? fixture->path : NULL;
}

int tf_fixture_file(const TF_Fixture *fixture, const char *name, char *path,
                    size_t path_size) {
  int n;
  if (!fixture || !fixture->active || !name || !path || path_size == 0) return -1;
  n = snprintf(path, path_size, "%s/%s", fixture->path, name);
  return n < 0 || (size_t)n >= path_size ? -1 : 0;
}

void tf_fixture_cleanup(TF_Fixture *fixture) {
  if (!fixture || !fixture->active) return;
  (void)tf_remove_tree(fixture->path);
  fixture->active = false;
  fixture->path[0] = '\0';
}

static int tf_source_write(const char *source, FILE *file) {
  if (g_io_write_failure) return -1;
  return fputs(source, file) == EOF ? -1 : 0;
}
static int tf_source_close(FILE *file) { return g_io_close_failure ? -1 : fclose(file); }
static bool tf_sync(FILE *file, const char *path) {
  (void)file; (void)path;
  return !g_io_sync_failure;
}
static bool tf_directory_sync(const char *path) { (void)path; return !g_io_sync_failure; }

void tf_reset_hooks(void) {
  alloc_test_fail_after(-1);
  g_io_write_failure = false; g_io_close_failure = false; g_io_sync_failure = false;
  itemstore_set_load_constructor_failure_hook_for_tests(NULL);
  itemstore_set_item_creation_failure_hook_for_tests(NULL);
  itemstore_set_source_io_hooks_for_tests(NULL, NULL);
  itemstore_set_directory_sync_hook_for_tests(NULL);
  itemstore_set_pre_publish_hook_for_tests(NULL);
  itemstore_set_sync_hook_for_tests(NULL);
}

void tf_alloc_fail_after(long allocation) { alloc_test_fail_after(allocation); }

void tf_io_failures(bool write_failure, bool close_failure, bool sync_failure) {
  g_io_write_failure = write_failure; g_io_close_failure = close_failure; g_io_sync_failure = sync_failure;
  itemstore_set_source_io_hooks_for_tests(tf_source_write, tf_source_close);
  itemstore_set_directory_sync_hook_for_tests(tf_directory_sync);
  itemstore_set_sync_hook_for_tests(tf_sync);
}

void tf_fail(const char *file, int line, const char *expression,
             const char *expected, const char *actual, const char *detail) {
  (void)fprintf(stderr, "assertion failed at %s:%d: %s\n  expected: %s\n  actual:   %s\n",
                file, line, expression, expected, actual);
  if (detail) (void)fprintf(stderr, "  detail:   %s\n", detail);
  tf_reset_hooks();
  _exit(1);
}

void tf_assert_process(const char *file, int line, const char *expression,
                       const TF_ProcessResult *result, int expected_status) {
  char expected[64], actual[128];
  if (result && result->exited && !result->timed_out && result->exit_status == expected_status) return;
  (void)snprintf(expected, sizeof expected, "exit status %d", expected_status);
  if (!result) (void)snprintf(actual, sizeof actual, "(null result)");
  else if (result->timed_out) (void)snprintf(actual, sizeof actual, "timeout");
  else if (result->signaled) (void)snprintf(actual, sizeof actual, "signal %d", result->signal_number);
  else (void)snprintf(actual, sizeof actual, "exit status %d", result->exit_status);
  tf_fail(file, line, expression, expected, actual, result ? result->stderr_data : NULL);
}

static const TF_TestDescriptor *tf_find(const TF_TestDescriptor *tests,
                                        size_t count, const char *id) {
  for (size_t i = 0; i < count; i++) if (strcmp(tests[i].id, id) == 0) return &tests[i];
  return NULL;
}

static int tf_run_one(const TF_TestDescriptor *test) {
  int out_pipe[2], err_pipe[2];
  pid_t pid;
  int status = 0;
  bool out_open = true, err_open = true, reaped = false, timed_out = false;
  char *out = NULL, *err = NULL;
  size_t out_len = 0, err_len = 0;
  uint64_t deadline = tf_now_ms() + (test->timeout_ms ? test->timeout_ms : 30000u);
  if (pipe(out_pipe) < 0 || pipe(err_pipe) < 0) return -1;
  pid = fork();
  if (pid < 0) return -1;
  if (pid == 0) {
    (void)setpgid(0, 0);
    (void)close(out_pipe[0]); (void)close(err_pipe[0]);
    (void)dup2(out_pipe[1], STDOUT_FILENO); (void)dup2(err_pipe[1], STDERR_FILENO);
    (void)close(out_pipe[1]); (void)close(err_pipe[1]);
    tf_reset_hooks();
    test->fn();
    _exit(0);
  }
  (void)setpgid(pid, pid);
  (void)close(out_pipe[1]); (void)close(err_pipe[1]);
  (void)tf_set_nonblock(out_pipe[0]); (void)tf_set_nonblock(err_pipe[0]);
  while (out_open || err_open || !reaped) {
    struct pollfd fds[2];
    int nfds = 0;
    if (out_open) { fds[nfds].fd = out_pipe[0]; fds[nfds].events = POLLIN; nfds++; }
    if (err_open) { fds[nfds].fd = err_pipe[0]; fds[nfds].events = POLLIN; nfds++; }
    if (nfds) (void)poll(fds, (nfds_t)nfds, 25);
    char buf[4096];
    ssize_t n;
    if (out_open) {
      while ((n = read(out_pipe[0], buf, sizeof buf)) > 0) tf_append(&out, &out_len, buf, (size_t)n);
      if (n == 0) { (void)close(out_pipe[0]); out_open = false; }
    }
    if (err_open) {
      while ((n = read(err_pipe[0], buf, sizeof buf)) > 0) tf_append(&err, &err_len, buf, (size_t)n);
      if (n == 0) { (void)close(err_pipe[0]); err_open = false; }
    }
    if (!reaped && waitpid(pid, &status, WNOHANG) == pid) reaped = true;
    if (!reaped && tf_now_ms() >= deadline) {
      timed_out = true; (void)kill(-pid, SIGKILL); (void)waitpid(pid, &status, 0); reaped = true;
    }
  }
  if (out && out_len != 0) (void)fwrite(out, 1, out_len, stderr);
  if (err && err_len != 0) (void)fwrite(err, 1, err_len, stderr);
  free(out); free(err);
  if (!timed_out && WIFEXITED(status) && WEXITSTATUS(status) == 0) return 0;
  if (timed_out) (void)fprintf(stderr, "test %s timed out\n", test->id);
  if (WIFSIGNALED(status)) (void)fprintf(stderr, "test %s terminated by signal %d\n", test->id, WTERMSIG(status));
  return -1;
}

int tf_main(int argc, char **argv, const TF_TestDescriptor *tests,
            size_t count) {
  char detail[256];
  g_program_path = argc > 0 ? argv[0] : NULL;
  if (tf_validate_descriptors(tests, count, detail, sizeof detail) < 0) {
    (void)fprintf(stderr, "TF|ERROR|metadata|%s\n", detail); return 2;
  }
  if (argc == 2 && strcmp(argv[1], "--list") == 0) {
    for (size_t i = 0; i < count; i++) {
      char timeout[32];
      (void)snprintf(timeout, sizeof timeout, "%u", tests[i].timeout_ms);
      tf_write_record("LIST", tests[i].id, tests[i].tags, timeout, tests[i].contracts);
    }
    return 0;
  }
  if (argc == 3 && strcmp(argv[1], "--run") == 0) {
    const TF_TestDescriptor *test = tf_find(tests, count, argv[2]);
    uint64_t start = tf_now_ms();
    int pass;
    char duration[32];
    if (!test) { (void)fprintf(stderr, "TF|ERROR|unknown ID|%s\n", argv[2]); return 2; }
    pass = tf_run_one(test) == 0;
    (void)snprintf(duration, sizeof duration, "%llu", (unsigned long long)(tf_now_ms() - start));
    tf_write_record("RESULT", test->id, pass ? "PASS" : "FAIL", duration, "");
    tf_write_record("TOTAL", "selected", "1", pass ? "1" : "0", pass ? "0" : "1");
    return pass ? 0 : 1;
  }
  (void)fprintf(stderr, "usage: %s --list | --run ID\n", argv[0]);
  return 2;
}

const char *tf_program_path(void) { return g_program_path; }
