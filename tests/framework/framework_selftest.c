#include "test_framework.h"

#include <signal.h>
#include <errno.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>

static void assert_fail_bool(void) { TF_ASSERT_TRUE(false); }
static void assert_fail_i64(void) { TF_ASSERT_I64(1, 2); }
static void assert_fail_u64(void) { TF_ASSERT_U64(1, 2); }
static void assert_fail_str(void) { TF_ASSERT_STR("expected", "actual"); }
static void assert_fail_bytes(void) {
  const unsigned char a[] = {1, 2}; const unsigned char b[] = {1, 3};
  TF_ASSERT_BYTES(a, sizeof a, b, sizeof b);
}
static void assert_fail_float(void) { TF_ASSERT_FLOAT_BITS(UINT64_C(1), UINT64_C(2)); }
static void assert_fail_diag(void) { TF_ASSERT_DIAGNOSTIC("needle", "hay"); }
static void crash_test(void) { raise(SIGABRT); }
static void hang_test(void) { for (;;) pause(); }
static void delay_test(void) {
  struct timespec delay = {0, 150000000L};
  (void)nanosleep(&delay, NULL);
}

static void assertion_equal(void) {
  const unsigned char bytes[] = {0, 1, 2};
  TF_ASSERT_TRUE(true); TF_ASSERT_FALSE(false);
  TF_ASSERT_I64(-4, -4); TF_ASSERT_U64(4, 4); TF_ASSERT_STR("same", "same");
  TF_ASSERT_BYTES(bytes, sizeof bytes, bytes, sizeof bytes);
  TF_ASSERT_FLOAT_BITS(UINT64_C(0x3ff0000000000000), UINT64_C(0x3ff0000000000000));
  TF_ASSERT_DIAGNOSTIC("needle", "a useful needle diagnostic");
}

static void assertion_diagnostics(void) {
  const char *program = tf_program_path();
  const char *ids[] = {"assert_fail_bool", "assert_fail_i64", "assert_fail_u64",
                       "assert_fail_str", "assert_fail_bytes", "assert_fail_float",
                       "assert_fail_diag"};
  for (size_t i = 0; i < sizeof ids / sizeof ids[0]; i++) {
    char *args[] = {(char *)program, "--run", (char *)ids[i], NULL};
    TF_ProcessResult result;
    TF_ASSERT_TRUE(tf_process_run(args, 1000, &result) == 0);
    TF_ASSERT_TRUE(result.exited && result.exit_status != 0);
    TF_ASSERT_DIAGNOSTIC("assertion failed at", result.stderr_data);
    TF_ASSERT_DIAGNOSTIC("expected:", result.stderr_data);
    TF_ASSERT_DIAGNOSTIC("actual:", result.stderr_data);
    tf_process_result_destroy(&result);
  }
}

static void process_capture(void) {
  char *args[] = {"/bin/sh", "-c", "printf out; printf err >&2; exit 7", NULL};
  TF_ProcessResult result;
  TF_ASSERT_TRUE(tf_process_run(args, 1000, &result) == 0);
  TF_ASSERT_PROCESS(&result, 7);
  TF_ASSERT_STR("out", result.stdout_data);
  TF_ASSERT_STR("err", result.stderr_data);
  tf_process_result_destroy(&result);
}

static void timeout_and_group_cleanup(void) {
  TF_Fixture fixture;
  char marker[4096];
  char command[8192];
  char *args[4];
  TF_ProcessResult result;
  tf_fixture_init(&fixture);
  TF_ASSERT_TRUE(tf_fixture_file(&fixture, "pid", marker, sizeof marker) == 0);
  (void)snprintf(command, sizeof command, "sleep 30 & echo $! > '%s'; wait", marker);
  args[0] = "/bin/sh"; args[1] = "-c"; args[2] = command; args[3] = NULL;
  TF_ASSERT_TRUE(tf_process_run(args, 100, &result) == 0);
  TF_ASSERT_TRUE(result.timed_out);
  tf_process_result_destroy(&result);
  {
    FILE *pid_file = fopen(marker, "r");
    long descendant = -1;
    if (pid_file) { (void)fscanf(pid_file, "%ld", &descendant); (void)fclose(pid_file); }
    if (descendant > 0) {
      struct timespec pause_time = {0, 10000000L};
      bool gone = false;
      for (int attempt = 0; attempt < 20; attempt++) {
        errno = 0;
        if (kill((pid_t)descendant, 0) < 0 && errno == ESRCH) { gone = true; break; }
        (void)nanosleep(&pause_time, NULL);
      }
      TF_ASSERT_TRUE(gone);
    }
  }
  tf_fixture_cleanup(&fixture);
  TF_ASSERT_TRUE(tf_fixture_path(&fixture) == NULL);
}

static void crash_isolation(void) {
  char *args[] = {"/bin/sh", "-c", "kill -ABRT $$", NULL};
  TF_ProcessResult result;
  TF_ASSERT_TRUE(tf_process_run(args, 1000, &result) == 0);
  TF_ASSERT_TRUE(result.signaled && result.signal_number == SIGABRT);
  tf_process_result_destroy(&result);
}

static void fixture_cleanup(void) {
  TF_Fixture fixture;
  char path[4096];
  FILE *file;
  tf_fixture_init(&fixture);
  TF_ASSERT_TRUE(tf_fixture_file(&fixture, "nested/file", path, sizeof path) == 0);
  (void)snprintf(path, sizeof path, "%s/nested", tf_fixture_path(&fixture));
  TF_ASSERT_TRUE(mkdir(path, 0700) == 0);
  (void)snprintf(path, sizeof path, "%s/nested/file", tf_fixture_path(&fixture));
  file = fopen(path, "w"); TF_ASSERT_TRUE(file != NULL); (void)fputs("x", file); (void)fclose(file);
  char saved[4096]; (void)snprintf(saved, sizeof saved, "%s", tf_fixture_path(&fixture));
  tf_fixture_cleanup(&fixture);
  TF_ASSERT_TRUE(access(saved, F_OK) != 0);
}

static void malformed_metadata(void) {
  char detail[256];
  static const TF_TestDescriptor duplicate[] = {
    {"same", assertion_equal, "", 100, "contract"}, {"same", assertion_equal, "", 100, "contract"}};
  static const TF_TestDescriptor empty_id[] = {{"", assertion_equal, "", 100, "contract"}};
  static const TF_TestDescriptor empty_tag[] = {{"valid", assertion_equal, "bad tag", 100, "contract"}};
  static const TF_TestDescriptor null_tag[] = {{"valid", assertion_equal, NULL, 100, "contract"}};
  static const TF_TestDescriptor empty_contract[] = {{"valid", assertion_equal, "", 100, ""}};
  static const TF_TestDescriptor null_fn[] = {{"valid", NULL, "", 100, "contract"}};
  TF_ASSERT_TRUE(tf_validate_descriptors(duplicate, 2, detail, sizeof detail) != 0);
  TF_ASSERT_TRUE(tf_validate_descriptors(empty_id, 1, detail, sizeof detail) != 0);
  TF_ASSERT_TRUE(tf_validate_descriptors(empty_tag, 1, detail, sizeof detail) != 0);
  TF_ASSERT_TRUE(tf_validate_descriptors(null_tag, 1, detail, sizeof detail) != 0);
  TF_ASSERT_TRUE(tf_validate_descriptors(empty_contract, 1, detail, sizeof detail) != 0);
  TF_ASSERT_TRUE(tf_validate_descriptors(null_fn, 1, detail, sizeof detail) != 0);
}

static void hooks_reset(void) {
  tf_alloc_fail_after(1);
  TF_ASSERT_TRUE(malloc(1) != NULL); /* libc allocation is intentionally independent. */
  tf_reset_hooks();
  tf_io_failures(false, false, false);
  tf_reset_hooks();
}

static void runner_discovery_and_jobs(void) {
  const char *runner = getenv("TF_FRAMEWORK_RUNNER");
  TF_ProcessResult result;
  if (!runner || !runner[0]) return;
  {
    char *runner_args[] = {(char *)runner, "--jobs", "2", (char *)tf_program_path(), NULL};
    (void)setenv("TEST_JOBS", "2", 1);
    TF_ASSERT_TRUE(tf_process_run(runner_args, 10000, &result) == 0);
  }
  TF_ASSERT_PROCESS(&result, 0);
  TF_ASSERT_DIAGNOSTIC("TF|TOTAL|all|12|12|0", result.stdout_data);
  tf_process_result_destroy(&result);
}

static const TF_TestDescriptor tests[] = {
  {"assertion_equal", assertion_equal, "", 2000, "framework.assertions"},
  {"assertion_diagnostics", assertion_diagnostics, "", 5000, "framework.assertions"},
  {"process_capture", process_capture, "", 2000, "framework.process"},
  {"timeout_group_cleanup", timeout_and_group_cleanup, "exclusive", 3000, "framework.process"},
  {"crash_isolation", crash_isolation, "", 2000, "framework.process"},
  {"fixture_cleanup", fixture_cleanup, "", 2000, "framework.fixtures"},
  {"malformed_metadata", malformed_metadata, "", 2000, "framework.metadata"},
  {"hooks_reset", hooks_reset, "exclusive", 2000, "framework.hooks"},
  {"runner_discovery_and_jobs", runner_discovery_and_jobs, "helper", 12000, "framework.runner"},
  {"parallel_delay_a", delay_test, "", 2000, "framework.runner"},
  {"parallel_delay_b", delay_test, "", 2000, "framework.runner"},
  {"serial_delay_a", delay_test, "exclusive", 2000, "framework.runner"},
  {"serial_delay_b", delay_test, "network", 2000, "framework.runner"},
  {"assert_fail_bool", assert_fail_bool, "helper", 500, "framework.assertions"},
  {"assert_fail_i64", assert_fail_i64, "helper", 500, "framework.assertions"},
  {"assert_fail_u64", assert_fail_u64, "helper", 500, "framework.assertions"},
  {"assert_fail_str", assert_fail_str, "helper", 500, "framework.assertions"},
  {"assert_fail_bytes", assert_fail_bytes, "helper", 500, "framework.assertions"},
  {"assert_fail_float", assert_fail_float, "helper", 500, "framework.assertions"},
  {"assert_fail_diag", assert_fail_diag, "helper", 500, "framework.assertions"},
  {"crash", crash_test, "helper", 500, "framework.process"},
  {"hang", hang_test, "helper", 500, "framework.process"}
};

int main(int argc, char **argv) {
  return tf_main(argc, argv, tests, sizeof tests / sizeof tests[0]);
}
