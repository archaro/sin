// End-to-end smoke test for the examples/chat-* flow.

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

#define TEST_TIMEOUT_MS 8000
#define CONNECT_TIMEOUT_MS 5000

typedef struct {
  char *tmp_dir;
  pid_t server_pid;
  int client_fd;
  int shutdown_client_fd;
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
  fprintf(stderr, "[chat-smoke][FAIL] %s\n", message);
  cleanup_resources();
  exit(EXIT_FAILURE);
}

static void fail_errno(const char *message) {
  int saved_errno = errno;
  fprintf(stderr, "[chat-smoke][FAIL] %s: %s\n", message,
          strerror(saved_errno));
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

static void run_checked(char *const argv[], const char *label) {
  pid_t pid = fork();
  if (pid < 0) fail_errno("fork");
  if (pid == 0) {
    execv(argv[0], argv);
    _exit(127);
  }

  int status = 0;
  if (waitpid(pid, &status, 0) < 0) {
    int saved_errno = errno;
    kill(pid, SIGKILL);
    while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {
    }
    errno = saved_errno;
    fail_errno("waitpid");
  }
  if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
    fprintf(stderr, "[chat-smoke][FAIL] %s failed with status %d\n", label,
            status);
    fail("subprocess failed");
  }
}

static void run_expect_failure(char *const argv[], const char *label) {
  pid_t pid = fork();
  if (pid < 0) fail_errno("fork");
  if (pid == 0) {
    execv(argv[0], argv);
    _exit(127);
  }

  int status = 0;
  if (waitpid(pid, &status, 0) < 0) fail_errno("waitpid");
  if (!WIFEXITED(status) || WEXITSTATUS(status) == 0) {
    fprintf(stderr, "[chat-smoke][FAIL] %s unexpectedly succeeded\n", label);
    fail("expected nonzero subprocess status");
  }
}

static void wait_for_log_text(const char *path, const char *needle) {
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
    usleep(50000);
  }
  fail("timed out waiting for server startup log");
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
      if (WIFEXITED(status) && WEXITSTATUS(status) != 0) return;
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
    int log_fd = open(log_path, O_CREAT | O_WRONLY | O_TRUNC, 0600);
    if (log_fd >= 0) {
      dup2(log_fd, STDOUT_FILENO);
      dup2(log_fd, STDERR_FILENO);
      close(log_fd);
    }

    char port_arg[16];
    snprintf(port_arg, sizeof(port_arg), "%u", (unsigned)port);
    char *const argv[] = {
      "./sin", "-i", (char *)itemstore, "-s", (char *)srcroot,
      "-p", port_arg, "-o", (char *)boot_obj, NULL
    };
    execv(argv[0], argv);
    _exit(127);
  }
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

  (void)kill(resources.server_pid, SIGTERM);
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

  (void)kill(resources.server_pid, SIGKILL);
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
      resources.server_pid = -1;
      if (WIFEXITED(status) && WEXITSTATUS(status) == 0) return;
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
  if (resources.tmp_dir) {
    cleanup_tmp(resources.tmp_dir);
    resources.tmp_dir = NULL;
  }
}

int main(void) {
  printf("[chat-smoke][RUN] chat example localhost flow\n");

  char tmp_template[] = "/tmp/sin-chat-smoke-XXXXXX";
  char *tmp = mkdtemp(tmp_template);
  if (!tmp) fail_errno("mkdtemp");
  resources.tmp_dir = tmp;
  atexit(cleanup_resources);

  char srcroot[512];
  char boot_obj[512];
  char load_obj[512];
  char itemstore[512];
  char server_log[512];
  char metadata_log[512];
  char metadata_output[512];
  make_path(srcroot, sizeof(srcroot), tmp, "srcroot");
  make_path(boot_obj, sizeof(boot_obj), tmp, "chat-boot.obj");
  make_path(load_obj, sizeof(load_obj), tmp, "chat-load.obj");
  make_path(itemstore, sizeof(itemstore), tmp, "items.dat");
  make_path(server_log, sizeof(server_log), tmp, "server.log");
  make_path(metadata_log, sizeof(metadata_log), tmp, "metadata");
  make_path(metadata_output, sizeof(metadata_output), tmp, "metadata.log");
  if (mkdir(srcroot, 0700) != 0) fail_errno("mkdir srcroot");

  char *const compile_boot[] = {
    "./scomp", "examples/chat-boot.src", boot_obj, NULL
  };
  char *const compile_load[] = {
    "./scomp", "examples/chat-load.src", load_obj, NULL
  };
  run_checked(compile_boot, "compile chat-boot.src");
  run_checked(compile_load, "compile chat-load.src");

  char *const load_chat[] = {
    "./sin", "--loadonly", "-q", "-i", itemstore, "-s", srcroot,
    "-o", load_obj, NULL
  };
  run_checked(load_chat, "load chat-load.obj");

  char *const help_after_itemstore[] = {
    "./sin", "-i", itemstore, "--help", NULL
  };
  char *const version_after_itemstore[] = {
    "./sin", "--log", metadata_log, "-i", itemstore, "--version", NULL
  };
  run_checked(help_after_itemstore, "help after itemstore");
  run_checked(version_after_itemstore, "version after itemstore");
  wait_for_log_text(metadata_output, "sin ");

  char *const missing_short_port[] = {"./sin", "-p", NULL};
  char *const missing_long_port[] = {"./sin", "--port", NULL};
  char *const empty_port[] = {"./sin", "--port=", NULL};
  char *const signed_port[] = {"./sin", "-p", "+1", NULL};
  char *const negative_port[] = {"./sin", "--port=-1", NULL};
  char *const junk_port[] = {"./sin", "-p", "1x", NULL};
  char *const overflow_port[] = {
    "./sin", "--port=999999999999999999999999", NULL
  };
  char *const high_port[] = {"./sin", "-p", "65536", NULL};
  run_expect_failure(missing_short_port, "missing short port");
  run_expect_failure(missing_long_port, "missing long port");
  run_expect_failure(empty_port, "empty port");
  run_expect_failure(signed_port, "signed port");
  run_expect_failure(negative_port, "negative port");
  run_expect_failure(junk_port, "junk port");
  run_expect_failure(overflow_port, "overflow port");
  run_expect_failure(high_port, "high port");

  resources.server_pid = spawn_server(itemstore, srcroot, boot_obj, 0,
                                       server_log);
  wait_for_log_text(server_log, "Listening on port");
  if (kill(resources.server_pid, SIGUSR1) != 0) fail_errno("kill SIGUSR1");
  wait_server_failure();
  wait_for_log_text(server_log,
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
                                       server_log);
  wait_server_failure();
  close(occupied_fd);

  uint16_t port = reserve_port();
  resources.server_pid = spawn_server(itemstore, srcroot, boot_obj, port,
                                       server_log);

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

  printf("[chat-smoke][PASS] chat example localhost flow\n");
  return EXIT_SUCCESS;
}
