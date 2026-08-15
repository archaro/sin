#include "test_framework.h"

#include <signal.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>

#include "item.h"
#include "memory.h"

static void assertion_equal(void) {
  const unsigned char bytes[] = {0, 1, 2};
  TF_ASSERT_TRUE(true); TF_ASSERT_FALSE(false);
  TF_ASSERT_I64(-4, -4); TF_ASSERT_U64(4, 4); TF_ASSERT_STR("same", "same");
  TF_ASSERT_BYTES(bytes, sizeof bytes, bytes, sizeof bytes);
  TF_ASSERT_FLOAT_BITS(UINT64_C(0x3ff0000000000000), UINT64_C(0x3ff0000000000000));
  TF_ASSERT_DIAGNOSTIC("needle", "a useful needle diagnostic");
}

static void assertion_diagnostics(void) {
  const char *program = getenv("TF_FRAMEWORK_NEGATIVE");
  TF_ASSERT_TRUE(program != NULL && program[0] != '\0');
  const char *ids[] = {"assert_fail_bool", "assert_fail_i64", "assert_fail_u64",
                       "assert_fail_str", "assert_fail_bytes", "assert_fail_float",
                       "assert_fail_diag", "assert_process_capture_failure"};
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

static void exited_descendant_cleanup(void) {
  TF_Fixture fixture;
  char marker[4096], command[8192];
  char *args[4];
  TF_ProcessResult result;
  tf_fixture_init(&fixture);
  TF_ASSERT_TRUE(tf_fixture_file(&fixture, "pid", marker, sizeof marker) == 0);
  (void)snprintf(command, sizeof command, "sleep 30 & echo $! > '%s'; exit 0", marker);
  args[0] = "/bin/sh"; args[1] = "-c"; args[2] = command; args[3] = NULL;
  TF_ASSERT_TRUE(tf_process_run(args, 150, &result) == 0);
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
}

static void normal_group_cleanup(void) {
  TF_Fixture fixture;
  char marker[4096], command[8192];
  char *args[4];
  TF_ProcessResult result;
  tf_fixture_init(&fixture);
  TF_ASSERT_TRUE(tf_fixture_file(&fixture, "pid", marker, sizeof marker) == 0);
  (void)snprintf(command, sizeof command, "sleep 30 >/dev/null 2>/dev/null & echo $! > '%s'; exit 0", marker);
  args[0] = "/bin/sh"; args[1] = "-c"; args[2] = command; args[3] = NULL;
  TF_ASSERT_TRUE(tf_process_run(args, 1000, &result) == 0);
  TF_ASSERT_PROCESS(&result, 0);
  tf_process_result_destroy(&result);
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
  tf_fixture_cleanup(&fixture);
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

static void fixture_failure_cleanup(void) {
  TF_Fixture fixture;
  char marker[4096], child_path[4096];
  char *args[] = {(char *)getenv("TF_FRAMEWORK_NEGATIVE"), "--run", "fixture_failure_helper", NULL};
  TF_ProcessResult result;
  FILE *file;
  tf_fixture_init(&fixture);
  TF_ASSERT_TRUE(tf_fixture_file(&fixture, "child-path", marker, sizeof marker) == 0);
  TF_ASSERT_TRUE(setenv("TF_FIXTURE_MARKER", marker, 1) == 0);
  TF_ASSERT_TRUE(tf_process_run(args, 1000, &result) == 0);
  TF_ASSERT_TRUE(result.exited && result.exit_status != 0);
  tf_process_result_destroy(&result);
  (void)unsetenv("TF_FIXTURE_MARKER");
  file = fopen(marker, "r");
  TF_ASSERT_TRUE(file != NULL);
  TF_ASSERT_TRUE(fgets(child_path, sizeof child_path, file) != NULL);
  (void)fclose(file);
  child_path[strcspn(child_path, "\n")] = '\0';
  TF_ASSERT_TRUE(access(child_path, F_OK) != 0);
  tf_fixture_cleanup(&fixture);
}

static void fixture_implicit_and_multiple_cleanup(void) {
  TF_Fixture fixture;
  char marker[4096], paths[8192];
  char *implicit_args[] = {(char *)getenv("TF_FRAMEWORK_NEGATIVE"), "--run", "fixture_implicit_helper", NULL};
  char *multiple_args[] = {(char *)getenv("TF_FRAMEWORK_NEGATIVE"), "--run", "fixture_multiple_helper", NULL};
  TF_ProcessResult result;
  FILE *file;
  tf_fixture_init(&fixture);
  TF_ASSERT_TRUE(tf_fixture_file(&fixture, "marker", marker, sizeof marker) == 0);
  TF_ASSERT_TRUE(setenv("TF_FIXTURE_MARKER", marker, 1) == 0);
  TF_ASSERT_TRUE(tf_process_run(implicit_args, 1000, &result) == 0);
  TF_ASSERT_PROCESS(&result, 0); tf_process_result_destroy(&result);
  file = fopen(marker, "r"); TF_ASSERT_TRUE(file != NULL);
  TF_ASSERT_TRUE(fgets(paths, sizeof paths, file) != NULL); (void)fclose(file);
  paths[strcspn(paths, "\n")] = '\0';
  TF_ASSERT_TRUE(access(paths, F_OK) != 0);
  TF_ASSERT_TRUE(tf_process_run(multiple_args, 1000, &result) == 0);
  TF_ASSERT_PROCESS(&result, 0); tf_process_result_destroy(&result);
  file = fopen(marker, "r"); TF_ASSERT_TRUE(file != NULL);
  size_t length = fread(paths, 1, sizeof paths - 1, file); paths[length] = '\0'; (void)fclose(file);
  char *second = strchr(paths, '\n'); TF_ASSERT_TRUE(second != NULL); *second++ = '\0';
  TF_ASSERT_TRUE(access(paths, F_OK) != 0); TF_ASSERT_TRUE(access(second, F_OK) != 0);
  (void)unsetenv("TF_FIXTURE_MARKER");
  tf_fixture_cleanup(&fixture);
}

static void fixture_path_validation(void) {
  TF_Fixture fixture;
  char path[4096];
  tf_fixture_init(&fixture);
  TF_ASSERT_TRUE(tf_fixture_file(&fixture, "", path, sizeof path) != 0);
  TF_ASSERT_TRUE(tf_fixture_file(&fixture, "/tmp", path, sizeof path) != 0);
  TF_ASSERT_TRUE(tf_fixture_file(&fixture, ".", path, sizeof path) != 0);
  TF_ASSERT_TRUE(tf_fixture_file(&fixture, "..", path, sizeof path) != 0);
  TF_ASSERT_TRUE(tf_fixture_file(&fixture, "../escape", path, sizeof path) != 0);
  TF_ASSERT_TRUE(tf_fixture_file(&fixture, "nested/../escape", path, sizeof path) != 0);
  TF_ASSERT_TRUE(tf_fixture_file(&fixture, "nested//escape", path, sizeof path) != 0);
  TF_ASSERT_TRUE(tf_fixture_file(&fixture, "nested/file", path, sizeof path) == 0);
  tf_fixture_cleanup(&fixture);
}

static void output_replay(void) {
  char *args[] = {(char *)getenv("TF_FRAMEWORK_NEGATIVE"), "--run", "output_helper", NULL};
  TF_ProcessResult result;
  TF_ASSERT_TRUE(tf_process_run(args, 1000, &result) == 0);
  TF_ASSERT_TRUE(result.exited && result.exit_status == 0);
  TF_ASSERT_FALSE(strstr(result.stderr_data ? result.stderr_data : "", "visible stdout") != NULL);
  TF_ASSERT_FALSE(strstr(result.stderr_data ? result.stderr_data : "", "visible stderr") != NULL);
  tf_process_result_destroy(&result);
  TF_ASSERT_TRUE(setenv("TF_VERBOSE", "1", 1) == 0);
  TF_ASSERT_TRUE(tf_process_run(args, 1000, &result) == 0);
  TF_ASSERT_DIAGNOSTIC("visible stdout", result.stderr_data);
  TF_ASSERT_DIAGNOSTIC("visible stderr", result.stderr_data);
  tf_process_result_destroy(&result);
  (void)unsetenv("TF_VERBOSE");
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
  static const TF_TestDescriptor bad_id[] = {{"bad id", assertion_equal, "", 100, "contract"}};
  static const TF_TestDescriptor null_id[] = {{NULL, assertion_equal, "", 100, "contract"}};
  static const TF_TestDescriptor bad_contract[] = {{"valid", assertion_equal, "", 100, "bad contract"}};
  static const TF_TestDescriptor null_contract[] = {{"valid", assertion_equal, "", 100, NULL}};
  static const TF_TestDescriptor comma_tags[] = {{"valid", assertion_equal, ",tag", 100, "contract"}};
  static const TF_TestDescriptor trailing_tags[] = {{"valid", assertion_equal, "tag,", 100, "contract"}};
  static const TF_TestDescriptor doubled_tags[] = {{"valid", assertion_equal, "tag,,tag", 100, "contract"}};
  static const TF_TestDescriptor comma_contracts[] = {{"valid", assertion_equal, "", 100, ",contract"}};
  static const TF_TestDescriptor trailing_contracts[] = {{"valid", assertion_equal, "", 100, "contract,"}};
  static const TF_TestDescriptor doubled_contracts[] = {{"valid", assertion_equal, "", 100, "a,,b"}};
#define ASSERT_BAD(metadata) do { memset(detail, 0, sizeof detail); TF_ASSERT_TRUE(tf_validate_descriptors((metadata), 1, detail, sizeof detail) != 0); TF_ASSERT_TRUE(detail[0] != '\0'); } while (0)
  TF_ASSERT_TRUE(tf_validate_descriptors(duplicate, 2, detail, sizeof detail) != 0);
  TF_ASSERT_TRUE(tf_validate_descriptors(empty_id, 1, detail, sizeof detail) != 0);
  TF_ASSERT_TRUE(tf_validate_descriptors(empty_tag, 1, detail, sizeof detail) != 0);
  TF_ASSERT_TRUE(tf_validate_descriptors(null_tag, 1, detail, sizeof detail) != 0);
  TF_ASSERT_TRUE(tf_validate_descriptors(empty_contract, 1, detail, sizeof detail) != 0);
  TF_ASSERT_TRUE(tf_validate_descriptors(null_fn, 1, detail, sizeof detail) != 0);
  ASSERT_BAD(bad_id); ASSERT_BAD(null_id); ASSERT_BAD(bad_contract); ASSERT_BAD(null_contract);
  ASSERT_BAD(comma_tags); ASSERT_BAD(trailing_tags); ASSERT_BAD(doubled_tags);
  ASSERT_BAD(comma_contracts); ASSERT_BAD(trailing_contracts); ASSERT_BAD(doubled_contracts);
#undef ASSERT_BAD
}

static void tagged_result_record(void) {
  char *args[] = {(char *)tf_program_path(), "--run", "serial_exclusive", NULL};
  TF_ProcessResult result;
  TF_ASSERT_TRUE(tf_process_run(args, 1000, &result) == 0);
  TF_ASSERT_PROCESS(&result, 0);
  TF_ASSERT_DIAGNOSTIC("TF|RESULT|serial_exclusive|PASS", result.stdout_data);
  TF_ASSERT_DIAGNOSTIC("|exclusive", result.stdout_data);
  tf_process_result_destroy(&result);
}

static void hooks_reset(void) {
  void *allocation;
  tf_alloc_fail_after(1);
  allocation = alloc_malloc(8); TF_ASSERT_TRUE(allocation == NULL);
  allocation = alloc_calloc(1, 8); TF_ASSERT_TRUE(allocation == NULL);
  tf_reset_hooks();
  allocation = alloc_malloc(8); TF_ASSERT_TRUE(allocation != NULL); free(allocation);
  allocation = alloc_calloc(1, 8); TF_ASSERT_TRUE(allocation != NULL); free(allocation);
  tf_reset_hooks();
}

static void hooks_io_behavior(void) {
  TF_Fixture fixture;
  ITEMSTORE_t *store;
  ITEM_MUTATION_RESULT_t mutation;
  uint8_t *bytecode = malloc(1);
  char source_root[4096], save_path[4096];
  TF_ASSERT_TRUE(bytecode != NULL);
  bytecode[0] = 0;
  tf_fixture_init(&fixture);
  TF_ASSERT_TRUE(tf_fixture_file(&fixture, "src", source_root, sizeof source_root) == 0);
  TF_ASSERT_TRUE(tf_fixture_file(&fixture, "store.item", save_path, sizeof save_path) == 0);
  store = itemstore_create("root"); TF_ASSERT_TRUE(store != NULL);
  mutation = item_set_code(itemstore_root(store), "code", 1, bytecode);
  TF_ASSERT_TRUE(item_mutation_succeeded(mutation));
  tf_io_failures(true, false, false);
  TF_ASSERT_FALSE(save_itemsource_in_srcroot(mutation.item, "source", source_root));
  tf_reset_hooks();
  TF_ASSERT_TRUE(save_itemsource_in_srcroot(mutation.item, "source", source_root));
  tf_io_failures(false, true, false);
  for (int attempt = 0; attempt < 256; attempt++) {
    TF_ASSERT_FALSE(save_itemsource_in_srcroot(mutation.item, "source", source_root));
  }
  tf_reset_hooks();
  TF_ASSERT_TRUE(save_itemsource_in_srcroot(mutation.item, "source", source_root));
  tf_io_failures(false, false, true);
  TF_ASSERT_FALSE(itemstore_save_with_options(save_path, store, ITEMSTORE_DURABLE_FULL));
  tf_reset_hooks();
  TF_ASSERT_TRUE(itemstore_save_with_options(save_path, store, ITEMSTORE_DURABLE_FULL));
  itemstore_destroy(store);
  tf_fixture_cleanup(&fixture);
}

static void runner_discovery_and_jobs(void) {
  const char *runner = getenv("TF_FRAMEWORK_RUNNER");
  TF_ProcessResult result;
  TF_Fixture fixture;
  char log_path[4096], lock_path[4096], log[8192];
  FILE *log_file;
  if (!runner || !runner[0] || getenv("TF_RUNNER_NESTED")) return;
  {
    tf_fixture_init(&fixture);
    TF_ASSERT_TRUE(tf_fixture_file(&fixture, "schedule.log", log_path, sizeof log_path) == 0);
    TF_ASSERT_TRUE(tf_fixture_file(&fixture, "schedule.lock", lock_path, sizeof lock_path) == 0);
    TF_ASSERT_TRUE(setenv("TF_SCHEDULE_LOG", log_path, 1) == 0);
    TF_ASSERT_TRUE(setenv("TF_SCHEDULE_LOCK", lock_path, 1) == 0);
    char *runner_args[] = {(char *)runner, "--jobs", "2", (char *)tf_program_path(), NULL};
    (void)setenv("TEST_JOBS", "2", 1);
    (void)setenv("TF_RUNNER_NESTED", "1", 1);
    TF_ASSERT_TRUE(tf_process_run(runner_args, 10000, &result) == 0);
    (void)unsetenv("TF_RUNNER_NESTED");
    (void)unsetenv("TF_SCHEDULE_LOG"); (void)unsetenv("TF_SCHEDULE_LOCK");
  }
  TF_ASSERT_PROCESS(&result, 0);
  TF_ASSERT_DIAGNOSTIC("TF|TOTAL|all|22|22|0", result.stdout_data);
  tf_process_result_destroy(&result);
  log_file = fopen(log_path, "r");
  TF_ASSERT_TRUE(log_file != NULL);
  size_t length = fread(log, 1, sizeof log - 1, log_file); log[length] = '\0'; (void)fclose(log_file);
  TF_ASSERT_DIAGNOSTIC("parallel-overlap", log);
  TF_ASSERT_FALSE(strstr(log, "serial-overlap") != NULL);
  TF_ASSERT_DIAGNOSTIC("serial_exclusive", log);
  TF_ASSERT_DIAGNOSTIC("serial_network", log);
  TF_ASSERT_DIAGNOSTIC("serial_benchmark", log);
  tf_fixture_cleanup(&fixture);
}

static void schedule_probe(void) {
  const char *log_path = getenv("TF_SCHEDULE_LOG");
  const char *lock_path = getenv("TF_SCHEDULE_LOCK");
  const char *id = getenv("TF_TEST_ID");
  int lock_fd;
  if (!log_path || !lock_path || !id) return;
  lock_fd = mkdir(lock_path, 0700) == 0 ? 1 : 0;
  int log_fd = open(log_path, O_WRONLY | O_CREAT | O_APPEND, 0600);
  if (log_fd >= 0) {
    char event[128];
    int length = snprintf(event, sizeof event, "%s%s\n", lock_fd ? "" :
                          (strncmp(id, "parallel", 8) == 0 ? "parallel-overlap " : "serial-overlap "), id);
    if (length > 0) (void)write(log_fd, event, (size_t)length);
    (void)close(log_fd);
  }
  struct timespec delay = {0, 150000000L}; (void)nanosleep(&delay, NULL);
  if (lock_fd) (void)rmdir(lock_path);
}

static const TF_TestDescriptor tests[] = {
  {"assertion_equal", assertion_equal, "", 2000, "framework.assertions"},
  {"assertion_diagnostics", assertion_diagnostics, "", 5000, "framework.assertions"},
  {"process_capture", process_capture, "", 2000, "framework.process"},
  {"timeout_group_cleanup", timeout_and_group_cleanup, "exclusive", 3000, "framework.process"},
  {"exited_descendant_cleanup", exited_descendant_cleanup, "exclusive", 3000, "framework.process"},
  {"normal_group_cleanup", normal_group_cleanup, "exclusive", 3000, "framework.process"},
  {"crash_isolation", crash_isolation, "", 2000, "framework.process"},
  {"fixture_cleanup", fixture_cleanup, "", 2000, "framework.fixtures"},
  {"fixture_failure_cleanup", fixture_failure_cleanup, "", 3000, "framework.fixtures"},
  {"fixture_implicit_and_multiple_cleanup", fixture_implicit_and_multiple_cleanup, "", 4000, "framework.fixtures"},
  {"fixture_path_validation", fixture_path_validation, "", 2000, "framework.fixtures"},
  {"output_replay", output_replay, "", 3000, "framework.output"},
  {"malformed_metadata", malformed_metadata, "", 2000, "framework.metadata"},
  {"hooks_reset", hooks_reset, "exclusive", 2000, "framework.hooks"},
  {"hooks_io_behavior", hooks_io_behavior, "exclusive", 5000, "framework.hooks"},
  {"runner_discovery_and_jobs", runner_discovery_and_jobs, "", 12000, "framework.runner"},
  {"tagged_result_record", tagged_result_record, "", 3000, "framework.output"},
  {"parallel_delay_a", schedule_probe, "", 2000, "framework.runner"},
  {"parallel_delay_b", schedule_probe, "", 2000, "framework.runner"},
  {"serial_exclusive", schedule_probe, "exclusive", 2000, "framework.runner"},
  {"serial_network", schedule_probe, "network", 2000, "framework.runner"},
  {"serial_benchmark", schedule_probe, "benchmark", 2000, "framework.runner"},
};

int main(int argc, char **argv) {
  return tf_main(argc, argv, tests, sizeof tests / sizeof tests[0]);
}
