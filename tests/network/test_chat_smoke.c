// End-to-end smoke test for the examples/chat-* flow.

// Licensed under the MIT License - see LICENSE file for details.

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
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

static void fail(const char *message) {
  fprintf(stderr, "[chat-smoke][FAIL] %s\n", message);
  exit(EXIT_FAILURE);
}

static void fail_errno(const char *message) {
  fprintf(stderr, "[chat-smoke][FAIL] %s: %s\n", message, strerror(errno));
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
  if (waitpid(pid, &status, 0) < 0) fail_errno("waitpid");
  if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
    fprintf(stderr, "[chat-smoke][FAIL] %s failed with status %d\n", label,
            status);
    exit(EXIT_FAILURE);
  }
}

static uint16_t reserve_port(void) {
  int fd = socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) fail_errno("socket");

  struct sockaddr_in addr = {0};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  addr.sin_port = 0;
  if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
    fail_errno("bind");
  }

  socklen_t len = sizeof(addr);
  if (getsockname(fd, (struct sockaddr *)&addr, &len) != 0) {
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
      exit(EXIT_FAILURE);
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

static void wait_server_clean(pid_t pid) {
  int64_t deadline = monotonic_ms() + TEST_TIMEOUT_MS;
  while (monotonic_ms() < deadline) {
    int status = 0;
    pid_t ret = waitpid(pid, &status, WNOHANG);
    if (ret < 0) fail_errno("waitpid");
    if (ret == pid) {
      if (WIFEXITED(status) && WEXITSTATUS(status) == 0) return;
      fprintf(stderr, "[chat-smoke][FAIL] server exited with status %d\n",
              status);
      exit(EXIT_FAILURE);
    }
    usleep(50000);
  }

  kill(pid, SIGTERM);
  waitpid(pid, NULL, 0);
  fail("server did not exit after shutdown command");
}

static void cleanup_tmp(const char *dir) {
  char path[512];
  const char *files[] = {
    "chat-boot.obj", "chat-load.obj", "items.dat", "server.log", NULL
  };
  for (size_t i = 0; files[i]; i++) {
    make_path(path, sizeof(path), dir, files[i]);
    unlink(path);
  }
  make_path(path, sizeof(path), dir, "srcroot");
  rmdir(path);
  rmdir(dir);
}

int main(void) {
  printf("[chat-smoke][RUN] chat example localhost flow\n");

  char tmp_template[] = "/tmp/sin-chat-smoke-XXXXXX";
  char *tmp = mkdtemp(tmp_template);
  if (!tmp) fail_errno("mkdtemp");

  char srcroot[512];
  char boot_obj[512];
  char load_obj[512];
  char itemstore[512];
  char server_log[512];
  make_path(srcroot, sizeof(srcroot), tmp, "srcroot");
  make_path(boot_obj, sizeof(boot_obj), tmp, "chat-boot.obj");
  make_path(load_obj, sizeof(load_obj), tmp, "chat-load.obj");
  make_path(itemstore, sizeof(itemstore), tmp, "items.dat");
  make_path(server_log, sizeof(server_log), tmp, "server.log");
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

  uint16_t port = reserve_port();
  pid_t server = spawn_server(itemstore, srcroot, boot_obj, port, server_log);

  char seen[4096] = {0};
  int client = connect_loop(port);
  read_until_contains(client, "Connected.", seen, sizeof(seen));
  read_until_contains(client, "Hello!  You are on line", seen, sizeof(seen));
  send_all(client, "hello from smoke\n");
  send_all(client, "\\quit\n");
  read_until_contains(client, "You have been disconnected.", seen, sizeof(seen));
  expect_eof(client);
  close(client);

  char shutdown_seen[2048] = {0};
  int shutdown_client = connect_loop(port);
  read_until_contains(shutdown_client, "Hello!  You are on line",
                      shutdown_seen, sizeof(shutdown_seen));
  send_all(shutdown_client, "\\shutdown\n");

  wait_server_clean(server);
  close(shutdown_client);
  cleanup_tmp(tmp);

  printf("[chat-smoke][PASS] chat example localhost flow\n");
  return EXIT_SUCCESS;
}
