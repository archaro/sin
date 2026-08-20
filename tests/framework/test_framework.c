#include "test_framework.h"

#if defined(SIN_COVERAGE_GCC)
extern void __gcov_dump(void);
#elif defined(SIN_COVERAGE_CLANG)
extern int __llvm_profile_write_file(void);
#endif

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
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
#define TF_MAX_FIXTURES 128u
static char g_fixture_paths[TF_MAX_FIXTURES][sizeof(((TF_Fixture *)0)->path)];

static void tf_configure_gcov_prefix(const char *program) {
  const char *base = getenv("GCOV_PREFIX_BASE");
  uint64_t hash = UINT64_C(1469598103934665603);
  char prefix[4096];
  int written;
  if (!base || !base[0] || !program) return;
  for (const unsigned char *p = (const unsigned char *)program; *p; p++) {
    hash ^= *p;
    hash *= UINT64_C(1099511628211);
  }
  written = snprintf(prefix, sizeof prefix, "%s/gcov-%016llx", base,
                     (unsigned long long)hash);
  if (written > 0 && (size_t)written < sizeof prefix)
    (void)setenv("GCOV_PREFIX", prefix, 1);
}
static size_t g_fixture_count;

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

static bool tf_append(char **data, size_t *length, const char *buffer,
                      size_t count) {
  if (count > SIZE_MAX - *length - 1u) return false;
  char *grown = realloc(*data, *length + count + 1);
  if (!grown) return false;
  memcpy(grown + *length, buffer, count);
  *length += count;
  grown[*length] = '\0';
  *data = grown;
  return true;
}

static int tf_set_nonblock(int fd) {
  int flags = fcntl(fd, F_GETFL, 0);
  return flags < 0 ? -1 : fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

static void tf_close_fd(int *fd) {
  if (*fd >= 0) { (void)close(*fd); *fd = -1; }
}

static int tf_wait_blocking(pid_t pid, int *status) {
  int result;
  do { result = waitpid(pid, status, 0); } while (result < 0 && errno == EINTR);
  return result == pid ? 0 : -1;
}

/* Drain both capture streams until the child and all inherited descriptors
 * close, or until the deadline. The latter matters for a child that exits
 * while a descendant retains a pipe descriptor. */
static int tf_collect(pid_t pid, int out_fd, int err_fd, uint64_t deadline,
                      TF_ProcessResult *result) {
  int status = 0;
  bool out_open = out_fd >= 0, err_open = err_fd >= 0, reaped = false;
  bool wait_failed = false, group_killed = false;
  while (out_open || err_open || !reaped) {
    struct pollfd pollfds[2];
    int nfds = 0;
    int poll_timeout;
    int wait_result;
    if (!reaped) {
      do { wait_result = waitpid(pid, &status, WNOHANG); }
      while (wait_result < 0 && errno == EINTR);
      if (wait_result == pid) reaped = true;
      else if (wait_result < 0 && errno != EINTR) { wait_failed = true; break; }
    }
    if (tf_now_ms() >= deadline) {
      result->timed_out = true;
      (void)kill(-pid, SIGKILL);
      group_killed = true;
      if (!reaped && tf_wait_blocking(pid, &status) == 0) reaped = true;
      tf_close_fd(&out_fd); tf_close_fd(&err_fd);
      out_open = false; err_open = false;
      break;
    }
    if (out_open) { pollfds[nfds].fd = out_fd; pollfds[nfds].events = POLLIN; nfds++; }
    if (err_open) { pollfds[nfds].fd = err_fd; pollfds[nfds].events = POLLIN; nfds++; }
    poll_timeout = (int)(deadline - tf_now_ms());
    if (poll_timeout > 25) poll_timeout = 25;
    if (nfds && poll(pollfds, (nfds_t)nfds, poll_timeout) < 0 && errno != EINTR) {
      result->capture_failed = true;
      if (!group_killed) { (void)kill(-pid, SIGKILL); group_killed = true; }
      tf_close_fd(&out_fd); tf_close_fd(&err_fd);
      out_open = false; err_open = false;
      continue;
    }
    char buffer[4096];
    ssize_t got;
    if (out_open) {
      while ((got = read(out_fd, buffer, sizeof buffer)) > 0) {
        if (!tf_append(&result->stdout_data, &result->stdout_len, buffer, (size_t)got)) {
          result->capture_failed = true;
          if (!group_killed) { (void)kill(-pid, SIGKILL); group_killed = true; }
        }
      }
      if (got == 0) { tf_close_fd(&out_fd); out_open = false; }
      else if (got < 0 && errno != EAGAIN && errno != EINTR) {
        result->capture_failed = true;
        if (!group_killed) { (void)kill(-pid, SIGKILL); group_killed = true; }
        tf_close_fd(&out_fd); out_open = false;
      }
    }
    if (err_open) {
      while ((got = read(err_fd, buffer, sizeof buffer)) > 0) {
        if (!tf_append(&result->stderr_data, &result->stderr_len, buffer, (size_t)got)) {
          result->capture_failed = true;
          if (!group_killed) { (void)kill(-pid, SIGKILL); group_killed = true; }
        }
      }
      if (got == 0) { tf_close_fd(&err_fd); err_open = false; }
      else if (got < 0 && errno != EAGAIN && errno != EINTR) {
        result->capture_failed = true;
        if (!group_killed) { (void)kill(-pid, SIGKILL); group_killed = true; }
        tf_close_fd(&err_fd); err_open = false;
      }
    }
  }
  tf_close_fd(&out_fd); tf_close_fd(&err_fd);
  if (result->capture_failed && !group_killed) {
    (void)kill(-pid, SIGKILL); group_killed = true;
  }
  if (wait_failed) {
    if (!group_killed) (void)kill(-pid, SIGKILL);
    if (tf_wait_blocking(pid, &status) == 0) reaped = true;
  }
  if (!reaped && tf_wait_blocking(pid, &status) < 0) wait_failed = true;
  /* A direct child may have exited while descendants closed or redirected
   * both capture descriptors. Reap status first, then terminate the child's
   * process group so no descendant survives a successful run. */
  if (!group_killed) { (void)kill(-pid, SIGKILL); group_killed = true; }
  if (!wait_failed && reaped && WIFEXITED(status)) { result->exited = true; result->exit_status = WEXITSTATUS(status); }
  if (!wait_failed && reaped && WIFSIGNALED(status)) { result->signaled = true; result->signal_number = WTERMSIG(status); }
  return wait_failed || result->capture_failed ? -1 : 0;
}

int tf_process_run(char *const argv[], unsigned timeout_ms,
                   TF_ProcessResult *result) {
  int out_pipe[2] = {-1, -1}, err_pipe[2] = {-1, -1};
  pid_t pid;
  uint64_t deadline;
  int rc;
  if (!argv || !argv[0] || !result) return -1;
  memset(result, 0, sizeof *result);
  if (pipe(out_pipe) < 0 || pipe(err_pipe) < 0) goto setup_failure;
  pid = fork();
  if (pid < 0) goto setup_failure;
  if (pid == 0) {
    (void)setpgid(0, 0);
    if (dup2(out_pipe[1], STDOUT_FILENO) < 0 || dup2(err_pipe[1], STDERR_FILENO) < 0) _exit(126);
    tf_close_fd(&out_pipe[0]); tf_close_fd(&out_pipe[1]);
    tf_close_fd(&err_pipe[0]); tf_close_fd(&err_pipe[1]);
    tf_configure_gcov_prefix(argv[0]);
    execvp(argv[0], argv);
    dprintf(STDERR_FILENO, "exec %s: %s\n", argv[0], strerror(errno));
    _exit(127);
  }
  (void)setpgid(pid, pid);
  tf_close_fd(&out_pipe[1]); tf_close_fd(&err_pipe[1]);
  if (tf_set_nonblock(out_pipe[0]) < 0 || tf_set_nonblock(err_pipe[0]) < 0) {
    (void)kill(-pid, SIGKILL); (void)tf_wait_blocking(pid, &(int){0});
    tf_close_fd(&out_pipe[0]); tf_close_fd(&err_pipe[0]); return -1;
  }
  deadline = tf_now_ms() + (timeout_ms ? timeout_ms : 30000u);
  rc = tf_collect(pid, out_pipe[0], err_pipe[0], deadline, result);
  return rc;
setup_failure:
  tf_close_fd(&out_pipe[0]); tf_close_fd(&out_pipe[1]);
  tf_close_fd(&err_pipe[0]); tf_close_fd(&err_pipe[1]);
  return -1;
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

static bool tf_valid_fixture_name(const char *name) {
  const char *component = name;
  if (!name || name[0] == '\0' || name[0] == '/') return false;
  while (*component) {
    const char *end = strchr(component, '/');
    size_t length = end ? (size_t)(end - component) : strlen(component);
    if (length == 0 || (length == 1 && component[0] == '.') ||
        (length == 2 && component[0] == '.' && component[1] == '.')) return false;
    if (end && end[1] == '\0') return false;
    component = end ? end + 1 : component + length;
  }
  return true;
}

void tf_fixture_init(TF_Fixture *fixture) {
  const char *root = getenv("TF_TMP_ROOT");
  if (!fixture) return;
  memset(fixture, 0, sizeof *fixture);
  if (!root || !root[0]) root = "/tmp";
  (void)snprintf(fixture->path, sizeof fixture->path, "%s/sin-test-XXXXXX", root);
  fixture->active = mkdtemp(fixture->path) != NULL;
  if (fixture->active && g_fixture_count < TF_MAX_FIXTURES) {
    (void)snprintf(g_fixture_paths[g_fixture_count++], sizeof g_fixture_paths[0], "%s", fixture->path);
  } else if (fixture->active) {
    (void)tf_remove_tree(fixture->path);
    fixture->active = false;
  }
}

const char *tf_fixture_path(const TF_Fixture *fixture) {
  return fixture && fixture->active ? fixture->path : NULL;
}

int tf_fixture_file(const TF_Fixture *fixture, const char *name, char *path,
                    size_t path_size) {
  int n;
  if (!fixture || !fixture->active || !tf_valid_fixture_name(name) || !path || path_size == 0) return -1;
  n = snprintf(path, path_size, "%s/%s", fixture->path, name);
  return n < 0 || (size_t)n >= path_size ? -1 : 0;
}

void tf_fixture_cleanup(TF_Fixture *fixture) {
  size_t i;
  char old_path[sizeof fixture->path];
  if (!fixture || !fixture->active) return;
  (void)snprintf(old_path, sizeof old_path, "%s", fixture->path);
  (void)tf_remove_tree(fixture->path);
  fixture->active = false;
  fixture->path[0] = '\0';
  for (i = 0; i < g_fixture_count; i++) {
    if (strcmp(g_fixture_paths[i], old_path) == 0) {
      size_t last = --g_fixture_count;
      if (i != last) memmove(g_fixture_paths[i], g_fixture_paths[last], sizeof g_fixture_paths[0]);
      g_fixture_paths[g_fixture_count][0] = '\0';
      break;
    }
  }
}

static void tf_fixture_cleanup_all(void) {
  while (g_fixture_count != 0) {
    (void)tf_remove_tree(g_fixture_paths[g_fixture_count - 1]);
    g_fixture_paths[--g_fixture_count][0] = '\0';
  }
}

static int tf_source_write(const char *source, FILE *file) {
  if (g_io_write_failure) return -1;
  return fputs(source, file) == EOF ? -1 : 0;
}
static int tf_source_close(FILE *file) {
  int result = fclose(file);
  return g_io_close_failure ? -1 : result;
}
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

void tf_assert_bytes(const char *file, int line, const char *expression,
                     const void *expected, size_t expected_len,
                     const void *actual, size_t actual_len) {
  const uint8_t *expected_bytes = expected;
  const uint8_t *actual_bytes = actual;
  size_t mismatch = 0;
  char expected_text[192], actual_text[192], detail[128];
  size_t expected_offset = mismatch, actual_offset = mismatch;
  if (expected_len == actual_len &&
      (expected_len == 0 || (expected_bytes && actual_bytes &&
                             memcmp(expected_bytes, actual_bytes, expected_len) == 0))) return;
  while (mismatch < expected_len && mismatch < actual_len && expected_bytes && actual_bytes &&
         expected_bytes[mismatch] == actual_bytes[mismatch]) mismatch++;
  expected_offset = mismatch; actual_offset = mismatch;
  (void)snprintf(detail, sizeof detail, "lengths: expected=%zu actual=%zu; mismatch offset=%zu",
                 expected_len, actual_len, mismatch);
  (void)snprintf(expected_text, sizeof expected_text, "bytes at offset %zu: %02x",
                 expected_offset, expected_bytes && mismatch < expected_len ? expected_bytes[mismatch] : 0u);
  (void)snprintf(actual_text, sizeof actual_text, "bytes at offset %zu: %02x",
                 actual_offset, actual_bytes && mismatch < actual_len ? actual_bytes[mismatch] : 0u);
  tf_fail(file, line, expression, expected_text, actual_text, detail);
}

void tf_fail(const char *file, int line, const char *expression,
             const char *expected, const char *actual, const char *detail) {
  (void)fprintf(stderr, "assertion failed at %s:%d: %s\n  expected: %s\n  actual:   %s\n",
                file, line, expression, expected, actual);
  if (detail) (void)fprintf(stderr, "  detail:   %s\n", detail);
  tf_reset_hooks();
  tf_fixture_cleanup_all();
  _exit(1);
}

void tf_assertf(const char *file, int line, const char *format, ...) {
  char detail[1024];
  va_list args;
  va_start(args, format);
  /* Callers receive printf checking through tf_assertf's declaration,
   * but this va_list forwarding necessarily passes a runtime format string.
   * Clang warns about that specific stdio use, so keep the suppression local. */
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wformat-nonliteral"
#endif
  (void)vsnprintf(detail, sizeof detail, format, args);
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
  va_end(args);
  tf_fail(file, line, "formatted assertion", "no failure", detail, NULL);
}

void tf_assert_process(const char *file, int line, const char *expression,
                       const TF_ProcessResult *result, int expected_status) {
  char expected[64], actual[128];
  if (result && result->exited && !result->timed_out && !result->signaled &&
      !result->capture_failed && result->exit_status == expected_status) return;
  (void)snprintf(expected, sizeof expected, "exit status %d", expected_status);
  if (!result) (void)snprintf(actual, sizeof actual, "(null result)");
  else if (result->capture_failed) (void)snprintf(actual, sizeof actual, "capture failure");
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
  bool timed_out = false;
  TF_ProcessResult result;
  uint64_t deadline = tf_now_ms() + (test->timeout_ms ? test->timeout_ms : 30000u);
  memset(&result, 0, sizeof result);
  out_pipe[0] = out_pipe[1] = err_pipe[0] = err_pipe[1] = -1;
  if (pipe(out_pipe) < 0 || pipe(err_pipe) < 0) goto run_failure;
  pid = fork();
  if (pid < 0) goto run_failure;
  if (pid == 0) {
    (void)setpgid(0, 0);
    (void)close(out_pipe[0]); (void)close(err_pipe[0]);
    if (dup2(out_pipe[1], STDOUT_FILENO) < 0 || dup2(err_pipe[1], STDERR_FILENO) < 0) _exit(126);
    (void)close(out_pipe[1]); (void)close(err_pipe[1]);
    tf_reset_hooks();
    test->fn();
    (void)fflush(NULL);
    tf_fixture_cleanup_all();
#if defined(SIN_COVERAGE_GCC)
    __gcov_dump();
#elif defined(SIN_COVERAGE_CLANG)
    (void)__llvm_profile_write_file();
#endif
    _exit(0);
  }
  (void)setpgid(pid, pid);
  tf_close_fd(&out_pipe[1]); tf_close_fd(&err_pipe[1]);
  if (tf_set_nonblock(out_pipe[0]) < 0 || tf_set_nonblock(err_pipe[0]) < 0) {
    (void)kill(-pid, SIGKILL); (void)tf_wait_blocking(pid, &status); goto run_failure;
  }
  (void)tf_collect(pid, out_pipe[0], err_pipe[0], deadline, &result);
  timed_out = result.timed_out;
  if (result.exited && result.exit_status == 0 && !timed_out && !result.capture_failed) {
    if (getenv("TF_VERBOSE") && strcmp(getenv("TF_VERBOSE"), "0") != 0) {
      if (result.stdout_data) (void)fwrite(result.stdout_data, 1, result.stdout_len, stderr);
      if (result.stderr_data) (void)fwrite(result.stderr_data, 1, result.stderr_len, stderr);
    }
    tf_process_result_destroy(&result);
    return 0;
  }
  if (result.stdout_data) (void)fwrite(result.stdout_data, 1, result.stdout_len, stderr);
  if (result.stderr_data) (void)fwrite(result.stderr_data, 1, result.stderr_len, stderr);
  if (timed_out) (void)fprintf(stderr, "test %s timed out\n", test->id);
  if (result.signaled) (void)fprintf(stderr, "test %s terminated by signal %d\n", test->id, result.signal_number);
  tf_process_result_destroy(&result);
  return -1;
run_failure:
  tf_close_fd(&out_pipe[0]); tf_close_fd(&out_pipe[1]);
  tf_close_fd(&err_pipe[0]); tf_close_fd(&err_pipe[1]);
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
    tf_write_record("RESULT", test->id, pass ? "PASS" : "FAIL", duration, test->tags);
    tf_write_record("TOTAL", "selected", "1", pass ? "1" : "0", pass ? "0" : "1");
    return pass ? 0 : 1;
  }
  (void)fprintf(stderr, "usage: %s --list | --run ID\n", argv[0]);
  return 2;
}

const char *tf_program_path(void) { return g_program_path; }
