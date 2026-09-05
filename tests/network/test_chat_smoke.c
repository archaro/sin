// End-to-end smoke test for the docs/guide/examples/chat-* flow.

// Licensed under the MIT License - see LICENSE file for details.

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <ftw.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "test_helpers.h"

#define TEST_TIMEOUT_MS 8000
#define CONNECT_TIMEOUT_MS 5000

typedef struct {
  char *tmp_dir;
  pid_t server_pid;
  int client_fd;
  int shutdown_client_fd;
  bool failed;
  bool cleaned;
} SmokeResources;

static SmokeResources resources = {
  .server_pid = -1,
  .client_fd = -1,
  .shutdown_client_fd = -1,
};

static void cleanup_resources(void);
static void stop_server(void);

static void fail(const char *message) {
  resources.failed = true;
  fprintf(stderr, "[chat-smoke][FAIL] %s\n", message);
  fprintf(stderr, "[chat-smoke] totals: ran=1 passed=0 failed=1 skipped=0 status=FAILURE\n");
  cleanup_resources();
  exit(EXIT_FAILURE);
}

static void fail_errno(const char *message) {
  int saved_errno = errno;
  resources.failed = true;
  fprintf(stderr, "[chat-smoke][FAIL] %s: %s\n", message,
          strerror(saved_errno));
  fprintf(stderr, "[chat-smoke] totals: ran=1 passed=0 failed=1 skipped=0 status=FAILURE\n");
  cleanup_resources();
  errno = saved_errno;
  exit(EXIT_FAILURE);
}

static int64_t monotonic_ms(void) {
  struct timespec ts;
  if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) fail_errno("clock_gettime");
  return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static void make_path(char *out, size_t out_size, const char *dir,
                      const char *name) {
  int written = snprintf(out, out_size, "%s/%s", dir, name);
  if (written < 0 || (size_t)written >= out_size) fail("path buffer overflow");
}

static void replay_subprocess_capture(FILE *capture, const char *label) {
  if (!capture) return;
  fflush(capture);
  rewind(capture);
  fprintf(stderr, "[chat-smoke][FAIL] captured %s output:\n", label);
  char buffer[4096];
  size_t used;
  while ((used = fread(buffer, 1, sizeof(buffer), capture)) > 0) {
    (void)fwrite(buffer, 1, used, stderr);
  }
}

static void run_checked(char *const argv[], const char *label) {
  FILE *capture = tmpfile();
  if (!capture) fail_errno("tmpfile");
  pid_t pid = fork();
  if (pid < 0) {
    fclose(capture);
    fail_errno("fork");
  }
  if (pid == 0) {
    int fd = fileno(capture);
    if (dup2(fd, STDOUT_FILENO) < 0 || dup2(fd, STDERR_FILENO) < 0) _exit(126);
    fclose(capture);
    execv(argv[0], argv);
    _exit(127);
  }

  int status = 0;
  if (waitpid(pid, &status, 0) < 0) {
    int saved_errno = errno;
    kill(pid, SIGKILL);
    while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {
    }
    replay_subprocess_capture(capture, label);
    fclose(capture);
    errno = saved_errno;
    fail_errno("waitpid");
  }
  if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
    fprintf(stderr, "[chat-smoke][FAIL] %s failed with status %d\n", label,
            status);
    replay_subprocess_capture(capture, label);
    fclose(capture);
    fail("subprocess failed");
  }
  fclose(capture);
}

static void run_expect_failure(char *const argv[], const char *label) {
  FILE *capture = tmpfile();
  if (!capture) fail_errno("tmpfile");
  pid_t pid = fork();
  if (pid < 0) {
    fclose(capture);
    fail_errno("fork");
  }
  if (pid == 0) {
    int fd = fileno(capture);
    if (dup2(fd, STDOUT_FILENO) < 0 || dup2(fd, STDERR_FILENO) < 0) _exit(126);
    fclose(capture);
    execv(argv[0], argv);
    _exit(127);
  }

  int status = 0;
  if (waitpid(pid, &status, 0) < 0) {
    replay_subprocess_capture(capture, label);
    fclose(capture);
    fail_errno("waitpid");
  }
  if (!WIFEXITED(status) || WEXITSTATUS(status) == 0) {
    fprintf(stderr, "[chat-smoke][FAIL] %s unexpectedly succeeded\n", label);
    replay_subprocess_capture(capture, label);
    fclose(capture);
    fail("expected nonzero subprocess status");
  }
  fclose(capture);
}

static void print_child_status(pid_t pid, int status) {
  if (WIFEXITED(status)) {
    fprintf(stderr, "[chat-smoke][FAIL] server child %ld exited with code %d\n",
            (long)pid, WEXITSTATUS(status));
  } else if (WIFSIGNALED(status)) {
    fprintf(stderr,
            "[chat-smoke][FAIL] server child %ld terminated by signal %d\n",
            (long)pid, WTERMSIG(status));
  } else {
    fprintf(stderr, "[chat-smoke][FAIL] server child %ld has status %d\n",
            (long)pid, status);
  }
}

static void report_log_wait_failure(const char *path, const char *phase,
                                    const char *needle,
                                    const int *known_status) {
  fprintf(stderr, "[chat-smoke][FAIL] phase '%s' %s log marker '%s'\n",
          phase, known_status ? "lost its server before observing"
                              : "timed out waiting for",
          needle);
  if (known_status) {
    print_child_status(resources.server_pid, *known_status);
    resources.server_pid = -1;
  } else if (resources.server_pid > 0) {
    int status = 0;
    pid_t ret = waitpid(resources.server_pid, &status, WNOHANG);
    if (ret == 0) {
      fprintf(stderr, "[chat-smoke][FAIL] server child %ld is still running\n",
              (long)resources.server_pid);
    } else if (ret == resources.server_pid) {
      print_child_status(resources.server_pid, status);
      resources.server_pid = -1;
    } else {
      fprintf(stderr, "[chat-smoke][FAIL] waitpid(%ld) failed: %s\n",
              (long)resources.server_pid, strerror(errno));
    }
  }

  fprintf(stderr, "[chat-smoke][FAIL] captured log %s:\n", path);
  FILE *file = fopen(path, "r");
  if (!file) {
    fprintf(stderr, "<unavailable: %s>\n", strerror(errno));
  } else {
    char buffer[4096];
    size_t used;
    while ((used = fread(buffer, 1, sizeof(buffer), file)) > 0) {
      (void)fwrite(buffer, 1, used, stderr);
    }
    fclose(file);
    fputc('\n', stderr);
  }
  fail(known_status ? "server exited before expected log marker"
                    : "timed out waiting for server log");
}

static void fail_if_server_exited(const char *path, const char *phase,
                                  const char *needle) {
  if (resources.server_pid <= 0) return;
  int status = 0;
  pid_t ret = waitpid(resources.server_pid, &status, WNOHANG);
  if (ret < 0 && errno == EINTR) return;
  if (ret < 0) fail_errno("waitpid while waiting for server log");
  if (ret == resources.server_pid) {
    report_log_wait_failure(path, phase, needle, &status);
  }
}

#if defined(SIN_CHAT_SMOKE_FRAMEWORK)
static void assert_server_reaped(pid_t pid) {
  int status = 0;
  errno = 0;
  if (waitpid(pid, &status, WNOHANG) != -1 || errno != ECHILD) {
    fail("framework server child was not reaped");
  }
}

static void assert_server_in_descriptor_group(pid_t pid) {
  pid_t descriptor_pgid = getpgrp();
  pid_t server_pgid = getpgid(pid);
  if (descriptor_pgid <= 0 || server_pgid != descriptor_pgid) {
    fail("server is not in the framework descriptor process group");
  }
}

static void assert_server_teardown(pid_t pid) {
  assert_server_reaped(pid);
}
#else
static void assert_server_teardown(pid_t pid) {
  if (pid <= 0) fail("invalid server process-group identity");
  errno = 0;
  if (kill(-pid, 0) == 0 || errno != ESRCH) {
    fail("server process group remains after teardown");
  }
}
#endif

static void wait_for_log_text(const char *path, const char *phase,
                              const char *needle) {
  int64_t deadline = monotonic_ms() + TEST_TIMEOUT_MS;
  while (monotonic_ms() < deadline) {
    FILE *file = fopen(path, "r");
    if (file) {
      char buffer[4096] = {0};
      size_t used = fread(buffer, 1, sizeof(buffer) - 1, file);
      buffer[used] = '\0';
      fclose(file);
      if (strstr(buffer, needle)) return;
    }
    fail_if_server_exited(path, phase, needle);
    usleep(50000);
  }
  report_log_wait_failure(path, phase, needle, NULL);
}

static size_t log_text_occurrences(const char *path, const char *needle) {
  if (!needle || needle[0] == '\0') fail("empty log occurrence marker");
  FILE *file = fopen(path, "r");
  if (!file) return 0;

  char buffer[16384] = {0};
  size_t used = fread(buffer, 1, sizeof(buffer) - 1u, file);
  if (ferror(file)) {
    fclose(file);
    fail_errno("read server log");
  }
  buffer[used] = '\0';
  if (fclose(file) != 0) fail_errno("close server log");

  size_t count = 0;
  size_t needle_len = strlen(needle);
  for (char *match = strstr(buffer, needle); match;
       match = strstr(match + needle_len, needle)) {
    count++;
  }
  return count;
}

static void wait_for_log_occurrences(const char *path, const char *phase,
                                     const char *needle,
                                     size_t expected_count) {
  char expected_marker[512];
  int written = snprintf(expected_marker, sizeof(expected_marker),
                         "%s (occurrence %zu)", needle, expected_count);
  if (written < 0 || (size_t)written >= sizeof(expected_marker)) {
    fail("log marker diagnostic overflow");
  }
  int64_t deadline = monotonic_ms() + TEST_TIMEOUT_MS;
  while (monotonic_ms() < deadline) {
    if (log_text_occurrences(path, needle) >= expected_count) return;
    fail_if_server_exited(path, phase, expected_marker);
    usleep(50000);
  }
  report_log_wait_failure(path, phase, expected_marker, NULL);
}

static void wait_server_failure(void) {
  pid_t pid = resources.server_pid;
  int64_t deadline = monotonic_ms() + TEST_TIMEOUT_MS;
  while (monotonic_ms() < deadline) {
    int status = 0;
    pid_t ret = waitpid(pid, &status, WNOHANG);
    if (ret < 0 && errno == EINTR) continue;
    if (ret < 0) fail_errno("waitpid");
    if (ret == pid) {
      resources.server_pid = -1;
      if (WIFEXITED(status) && WEXITSTATUS(status) != 0) {
        assert_server_teardown(pid);
        return;
      }
      fail("occupied-port server did not fail");
    }
    usleep(50000);
  }
  stop_server();
  fail("occupied-port server did not exit promptly");
}

static uint16_t reserve_port(void) {
  int fd = socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) fail_errno("socket");

  struct sockaddr_in addr = {0};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  addr.sin_port = 0;
  if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
    int saved_errno = errno;
    close(fd);
    errno = saved_errno;
    fail_errno("bind");
  }

  socklen_t len = sizeof(addr);
  if (getsockname(fd, (struct sockaddr *)&addr, &len) != 0) {
    int saved_errno = errno;
    close(fd);
    errno = saved_errno;
    fail_errno("getsockname");
  }
  uint16_t port = ntohs(addr.sin_port);
  close(fd);
  return port;
}

static pid_t spawn_server(const char *itemstore, const char *srcroot,
                          const char *boot_obj, uint16_t port,
                          const char *log_path) {
  pid_t pid = fork();
  if (pid < 0) fail_errno("fork");
  if (pid == 0) {
#if !defined(SIN_CHAT_SMOKE_FRAMEWORK)
    if (setpgid(0, 0) != 0) _exit(126);
#endif
    int log_fd = open(log_path, O_CREAT | O_WRONLY | O_TRUNC, 0600);
    if (log_fd >= 0) {
      dup2(log_fd, STDOUT_FILENO);
      dup2(log_fd, STDERR_FILENO);
      close(log_fd);
    }

    char port_arg[16];
    snprintf(port_arg, sizeof(port_arg), "%u", (unsigned)port);
    char *const argv[] = {
      TEST_SIN, "-i", (char *)itemstore, "-s", (char *)srcroot,
      "-p", port_arg, "-o", (char *)boot_obj, NULL
    };
    execv(argv[0], argv);
    _exit(127);
  }
#if !defined(SIN_CHAT_SMOKE_FRAMEWORK)
  if (setpgid(pid, pid) != 0 && errno != EACCES && errno != ESRCH) {
    fail_errno("setpgid server");
  }
#endif
  return pid;
}

static int connect_loop(uint16_t port) {
  int64_t deadline = monotonic_ms() + CONNECT_TIMEOUT_MS;
  while (monotonic_ms() < deadline) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) fail_errno("socket");

    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(port);
    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) == 0) return fd;

    close(fd);
    usleep(50000);
  }
  fail("timed out connecting to chat server");
  return -1;
}

static void send_all(int fd, const char *text) {
  size_t len = strlen(text);
  while (len > 0) {
    ssize_t written = send(fd, text, len, 0);
    if (written < 0) fail_errno("send");
    if (written == 0) fail("send returned zero");
    text += written;
    len -= (size_t)written;
  }
}

static void read_until_contains(int fd, const char *needle,
                                char *buffer, size_t buffer_size) {
  size_t used = strlen(buffer);
  int64_t deadline = monotonic_ms() + TEST_TIMEOUT_MS;
  while (!strstr(buffer, needle)) {
    int64_t remaining = deadline - monotonic_ms();
    if (remaining <= 0) {
      fprintf(stderr, "[chat-smoke][FAIL] timed out waiting for '%s'; saw: %s\n",
              needle, buffer);
      fail("timed out waiting for expected chat text");
    }

    fd_set readfds;
    FD_ZERO(&readfds);
    FD_SET(fd, &readfds);
    struct timeval tv = {
      .tv_sec = (time_t)(remaining / 1000),
      .tv_usec = (suseconds_t)((remaining % 1000) * 1000)
    };
    int ready = select(fd + 1, &readfds, NULL, NULL, &tv);
    if (ready < 0) fail_errno("select");
    if (ready == 0) continue;

    if (used + 1 >= buffer_size) fail("socket read buffer full");
    ssize_t got = recv(fd, buffer + used, buffer_size - used - 1, 0);
    if (got < 0) fail_errno("recv");
    if (got == 0) fail("connection closed before expected text arrived");
    used += (size_t)got;
    buffer[used] = '\0';
  }
}

static void expect_eof(int fd) {
  int64_t deadline = monotonic_ms() + TEST_TIMEOUT_MS;
  char byte;
  while (true) {
    int64_t remaining = deadline - monotonic_ms();
    if (remaining <= 0) fail("timed out waiting for chat disconnect");

    fd_set readfds;
    FD_ZERO(&readfds);
    FD_SET(fd, &readfds);
    struct timeval tv = {
      .tv_sec = (time_t)(remaining / 1000),
      .tv_usec = (suseconds_t)((remaining % 1000) * 1000)
    };
    int ready = select(fd + 1, &readfds, NULL, NULL, &tv);
    if (ready < 0) fail_errno("select");
    if (ready == 0) continue;

    ssize_t got = recv(fd, &byte, 1, 0);
    if (got < 0) fail_errno("recv");
    if (got == 0) return;
  }
}

static void stop_server(void) {
  if (resources.server_pid <= 0) return;

  int status = 0;
  pid_t ret = waitpid(resources.server_pid, &status, WNOHANG);
  if (ret == resources.server_pid || (ret < 0 && errno == ECHILD)) {
    resources.server_pid = -1;
    return;
  }
  if (ret < 0 && errno != EINTR) {
    resources.server_pid = -1;
    return;
  }

#if defined(SIN_CHAT_SMOKE_FRAMEWORK)
  (void)kill(resources.server_pid, SIGTERM);
#else
  (void)kill(-resources.server_pid, SIGTERM);
#endif
  int64_t deadline = monotonic_ms() + 1000;
  while (monotonic_ms() < deadline) {
    ret = waitpid(resources.server_pid, &status, WNOHANG);
    if (ret == resources.server_pid || (ret < 0 && errno == ECHILD)) {
      resources.server_pid = -1;
      return;
    }
    if (ret < 0 && errno != EINTR) break;
    usleep(10000);
  }

#if defined(SIN_CHAT_SMOKE_FRAMEWORK)
  (void)kill(resources.server_pid, SIGKILL);
#else
  (void)kill(-resources.server_pid, SIGKILL);
#endif
  while (waitpid(resources.server_pid, &status, 0) < 0 && errno == EINTR) {
  }
  resources.server_pid = -1;
}

static void wait_server_clean(void) {
  pid_t pid = resources.server_pid;
  int64_t deadline = monotonic_ms() + TEST_TIMEOUT_MS;
  while (monotonic_ms() < deadline) {
    int status = 0;
    pid_t ret = waitpid(pid, &status, WNOHANG);
    if (ret < 0 && errno == EINTR) continue;
    if (ret < 0) fail_errno("waitpid");
    if (ret == pid) {
      if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
        resources.server_pid = -1;
        assert_server_teardown(pid);
        return;
      }
      fprintf(stderr, "[chat-smoke][FAIL] server exited with status %d\n",
              status);
      fail("server exited unsuccessfully");
    }
    usleep(50000);
  }

  stop_server();
  fail("server did not exit after shutdown command");
}

static int remove_tree_entry(const char *path, const struct stat *statbuf,
                             int typeflag, struct FTW *ftwbuf) {
  (void)statbuf;
  (void)typeflag;
  (void)ftwbuf;
  return remove(path);
}

static void cleanup_tmp(const char *dir) {
  (void)nftw(dir, remove_tree_entry, 16, FTW_DEPTH | FTW_PHYS);
}

static void cleanup_resources(void) {
  if (resources.cleaned) return;
  resources.cleaned = true;

  if (resources.client_fd >= 0) {
    close(resources.client_fd);
    resources.client_fd = -1;
  }
  if (resources.shutdown_client_fd >= 0) {
    close(resources.shutdown_client_fd);
    resources.shutdown_client_fd = -1;
  }
  stop_server();
  if (resources.tmp_dir && !resources.failed) {
    cleanup_tmp(resources.tmp_dir);
    resources.tmp_dir = NULL;
  } else if (resources.tmp_dir) {
    fprintf(stderr, "[chat-smoke][FAIL] artifacts preserved in %s\n",
            resources.tmp_dir);
  }
}

int main(void) {
  char tmp_template[PATH_MAX];
  if (test_temp_template(tmp_template, sizeof tmp_template, "sin-chat-smoke") != 0)
    fail_errno("temp template");
  char *tmp = mkdtemp(tmp_template);
  if (!tmp) fail_errno("mkdtemp");
  resources.tmp_dir = tmp;
  atexit(cleanup_resources);

  char srcroot[512];
  char boot_obj[512];
  char load_obj[512];
  char itemstore[512];
  char boot_interrupt_log[512];
  char runtime_interrupt_log[512];
  char occupied_port_log[512];
  char chat_flow_log[512];
  char metadata_log[512];
  char metadata_output[512];
  char restart_src[512];
  char restart_obj[512];
  make_path(srcroot, sizeof(srcroot), tmp, "srcroot");
  make_path(boot_obj, sizeof(boot_obj), tmp, "chat-boot.obj");
  make_path(load_obj, sizeof(load_obj), tmp, "chat-load.obj");
  make_path(itemstore, sizeof(itemstore), tmp, "items.dat");
  make_path(boot_interrupt_log, sizeof(boot_interrupt_log), tmp,
            "boot-interrupt.log");
  make_path(runtime_interrupt_log, sizeof(runtime_interrupt_log), tmp,
            "runtime-interrupt.log");
  make_path(occupied_port_log, sizeof(occupied_port_log), tmp,
            "occupied-port.log");
  make_path(chat_flow_log, sizeof(chat_flow_log), tmp, "chat-flow.log");
  make_path(metadata_log, sizeof(metadata_log), tmp, "metadata");
  make_path(metadata_output, sizeof(metadata_output), tmp, "metadata.log");
  make_path(restart_src, sizeof(restart_src), tmp, "restart.src");
  make_path(restart_obj, sizeof(restart_obj), tmp, "restart.obj");
  if (mkdir(srcroot, 0700) != 0) fail_errno("mkdir srcroot");

  char *const compile_boot[] = {
    TEST_SCOMP, "docs/guide/examples/chat-boot.src", boot_obj, NULL
  };
  char *const compile_load[] = {
    TEST_SCOMP, "docs/guide/examples/chat-load.src", load_obj, NULL
  };
  run_checked(compile_boot, "compile chat-boot.src");
  run_checked(compile_load, "compile chat-load.src");

  char *const load_chat[] = {
    TEST_SIN, "--loadonly", "-q", "-i", itemstore, "-s", srcroot,
    "-o", load_obj, NULL
  };
  run_checked(load_chat, "load chat-load.obj");

  char *const help_after_itemstore[] = {
    TEST_SIN, "-i", itemstore, "--help", NULL
  };
  char *const version_after_itemstore[] = {
    TEST_SIN, "--log", metadata_log, "-i", itemstore, "--version", NULL
  };
  run_checked(help_after_itemstore, "help after itemstore");
  run_checked(version_after_itemstore, "version after itemstore");
  wait_for_log_text(metadata_output, "metadata version output", "sin ");

  char *const missing_short_port[] = {TEST_SIN, "-p", NULL};
  char *const missing_long_port[] = {TEST_SIN, "--port", NULL};
  char *const empty_port[] = {TEST_SIN, "--port=", NULL};
  char *const signed_port[] = {TEST_SIN, "-p", "+1", NULL};
  char *const negative_port[] = {TEST_SIN, "--port=-1", NULL};
  char *const junk_port[] = {TEST_SIN, "-p", "1x", NULL};
  char *const overflow_port[] = {
    TEST_SIN, "--port=999999999999999999999999", NULL
  };
  char *const high_port[] = {TEST_SIN, "-p", "65536", NULL};
  run_expect_failure(missing_short_port, "missing short port");
  run_expect_failure(missing_long_port, "missing long port");
  run_expect_failure(empty_port, "empty port");
  run_expect_failure(signed_port, "signed port");
  run_expect_failure(negative_port, "negative port");
  run_expect_failure(junk_port, "junk port");
  run_expect_failure(overflow_port, "overflow port");
  run_expect_failure(high_port, "high port");

  FILE *restart_file = fopen(restart_src, "w");
  if (!restart_file) fail_errno("fopen restart source");
  const char *entry_marker = "boot interrupt entry";
  if (fputs("sys.log{\"boot interrupt entry\\n\"}; "
            "while true do @x = 1; endwhile;\n", restart_file) < 0) {
    fclose(restart_file);
    fail_errno("write restart source");
  }
  if (fclose(restart_file) != 0) fail_errno("close restart source");
  char *const compile_restart[] = {
    TEST_SCOMP, restart_src, restart_obj, NULL
  };
  run_checked(compile_restart, "compile SIGUSR1 restart source");
  resources.server_pid = spawn_server(itemstore, srcroot, restart_obj, 0,
                                       boot_interrupt_log);
  wait_for_log_occurrences(boot_interrupt_log, "initial boot interrupt",
                           entry_marker, 1);
#if defined(SIN_CHAT_SMOKE_FRAMEWORK)
  assert_server_in_descriptor_group(resources.server_pid);
#endif
  if (kill(resources.server_pid, SIGUSR1) != 0) {
    fail_errno("kill boot-phase SIGUSR1");
  }
  wait_for_log_occurrences(boot_interrupt_log, "first boot interrupt recovery",
                           "SIGUSR1 received.  Restarting boot item.", 1);
  wait_for_log_occurrences(boot_interrupt_log, "first boot restart",
                           entry_marker, 2);
  if (kill(resources.server_pid, SIGUSR1) != 0) {
    fail_errno("kill repeated boot-phase SIGUSR1");
  }
  wait_for_log_occurrences(boot_interrupt_log,
                           "second boot interrupt recovery",
                           "SIGUSR1 received.  Restarting boot item.", 2);
  wait_for_log_occurrences(boot_interrupt_log, "second boot restart",
                           entry_marker, 3);
  wait_for_log_occurrences(boot_interrupt_log, "boot stack recreation",
                           "Destroying and recreating all stacks.", 2);
  if (kill(resources.server_pid, 0) != 0) {
    fail_errno("boot restart process exited");
  }
  stop_server();

  resources.server_pid = spawn_server(itemstore, srcroot, boot_obj, 0,
                                       runtime_interrupt_log);
  wait_for_log_text(runtime_interrupt_log, "runtime interrupt startup",
                    "Listening on port");
  if (kill(resources.server_pid, SIGUSR1) != 0) fail_errno("kill SIGUSR1");
  wait_server_failure();
  wait_for_log_text(runtime_interrupt_log, "runtime interrupt shutdown",
                    "SIGUSR1 received during runtime; shutting down.");

  int occupied_fd = socket(AF_INET, SOCK_STREAM, 0);
  if (occupied_fd < 0) fail_errno("socket");
  struct sockaddr_in occupied_addr = {0};
  occupied_addr.sin_family = AF_INET;
  occupied_addr.sin_addr.s_addr = htonl(INADDR_ANY);
  occupied_addr.sin_port = 0;
  if (bind(occupied_fd, (struct sockaddr *)&occupied_addr,
           sizeof(occupied_addr)) != 0) {
    int saved_errno = errno;
    close(occupied_fd);
    errno = saved_errno;
    fail_errno("bind occupied port");
  }
  socklen_t occupied_len = sizeof(occupied_addr);
  if (getsockname(occupied_fd, (struct sockaddr *)&occupied_addr,
                  &occupied_len) != 0) {
    int saved_errno = errno;
    close(occupied_fd);
    errno = saved_errno;
    fail_errno("getsockname occupied port");
  }
  if (listen(occupied_fd, 1) != 0) fail_errno("listen occupied port");
  resources.server_pid = spawn_server(itemstore, srcroot, boot_obj,
                                       ntohs(occupied_addr.sin_port),
                                       occupied_port_log);
  wait_server_failure();
  close(occupied_fd);

  uint16_t port = reserve_port();
  resources.server_pid = spawn_server(itemstore, srcroot, boot_obj, port,
                                       chat_flow_log);

  /* An early client disconnect must release its line without taking down the
   * listener; the next client proves the server remains usable. */
  resources.client_fd = connect_loop(port);
  close(resources.client_fd);
  resources.client_fd = -1;
  wait_for_log_text(chat_flow_log, "early client disconnect",
                    "Line 0: 127.0.0.1 disconnected.");
  if (kill(resources.server_pid, 0) != 0) {
    fail_errno("server exited after early client disconnect");
  }

  char seen[4096] = {0};
  resources.client_fd = connect_loop(port);
  read_until_contains(resources.client_fd, "Connected.", seen, sizeof(seen));
  read_until_contains(resources.client_fd, "Hello!  You are on line", seen,
                      sizeof(seen));
  send_all(resources.client_fd, "hello from smoke\n");
  send_all(resources.client_fd, "\\quit\n");
  read_until_contains(resources.client_fd, "You have been disconnected.",
                      seen, sizeof(seen));
  expect_eof(resources.client_fd);
  close(resources.client_fd);
  resources.client_fd = -1;

  char shutdown_seen[2048] = {0};
  resources.shutdown_client_fd = connect_loop(port);
  read_until_contains(resources.shutdown_client_fd, "Hello!  You are on line",
                      shutdown_seen, sizeof(shutdown_seen));
  send_all(resources.shutdown_client_fd, "\\shutdown\n");

  wait_server_clean();
  close(resources.shutdown_client_fd);
  resources.shutdown_client_fd = -1;
  cleanup_resources();

  printf("[chat-smoke] totals: ran=1 passed=1 failed=0 skipped=0 status=SUCCESS\n");
  return EXIT_SUCCESS;
}
