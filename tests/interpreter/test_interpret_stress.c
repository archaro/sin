#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

#include "test_assert.h"
#include "test_helpers.h"

typedef struct {
  const char *name;
  const char *src_path;
  const char *expected_path;
} StressCase;

enum { STRESS_CASE_COUNT = 2 };

/* Each descriptor runs in an isolated child, and tf_fail terminates that child
 * with _exit, so atexit handlers cannot provide assertion-failure cleanup.
 * Keep the small, bounded set of process results in static storage and release
 * them explicitly on the normal success path below. */
typedef struct {
  TestProcessResult current;
  TestProcessResult baselines[STRESS_CASE_COUNT];
} StressCleanup;

static StressCleanup stress_cleanup;

static void stress_cleanup_results(void) {
  test_process_result_free(&stress_cleanup.current);
  for (size_t i = 0; i < STRESS_CASE_COUNT; ++i) {
    test_process_result_free(&stress_cleanup.baselines[i]);
  }
}

static void normalize_runtime_path(char *text, const char *path,
                                   const char *replacement) {
  if (!text || !path || !replacement) return;
  size_t path_len = strlen(path);
  size_t replacement_len = strlen(replacement);
  char *match = NULL;
  while ((match = strstr(text, path)) != NULL) {
    memcpy(match, replacement, replacement_len);
    memmove(match + replacement_len, match + path_len,
            strlen(match + path_len) + 1u);
  }
}

void test_interpret_stress(void) {
  const StressCase cases[] = {
      {"chat_boot", "examples/chat-boot.src", "tests/fixtures/interpret/chat-boot.expected.txt"},
      {"echo_boot", "examples/echo-boot.src", "tests/fixtures/interpret/echo-boot.expected.txt"},
  };
  const int iterations = 30;
  memset(&stress_cleanup, 0, sizeof(stress_cleanup));
  char obj_paths[STRESS_CASE_COUNT][128];
  for (size_t i = 0; i < STRESS_CASE_COUNT; ++i) {
    ASSERT_EQ_INT(0, test_make_temp_path("sin-interp-stress", obj_paths[i],
                                         sizeof(obj_paths[i])));
  }

  for (int iter = 0; iter < iterations; ++iter) {
    for (size_t i = 0; i < STRESS_CASE_COUNT; ++i) {
      char *const compile_argv[] = {
        TEST_SCOMP, (char *)cases[i].src_path, obj_paths[i], NULL
      };
      TestProcessResult compile_result = {0};
      int capture_rc = test_run_argv_capture(compile_argv, 0, &compile_result);
      int compile_exit = compile_result.exit_code;
      test_process_result_free(&compile_result);
      if (capture_rc != 0) remove(obj_paths[i]);
      ASSERT_EQ_INT(0, capture_rc);
      if (compile_exit != 0) remove(obj_paths[i]);
      ASSERT_EQ_INT(0, compile_exit);

      char run_dir[4096];

      ASSERT_EQ_INT(0, test_temp_template(run_dir, sizeof run_dir, "sin-interp-stress-run"));
      ASSERT_NOT_NULL(mkdtemp(run_dir));
      char itemstore_path[sizeof(run_dir) + sizeof("/items.dat")];
      char srcroot_path[sizeof(run_dir) + sizeof("/srcroot")];
      ASSERT_TRUE(snprintf(itemstore_path, sizeof(itemstore_path),
                           "%s/items.dat", run_dir) > 0);
      ASSERT_TRUE(snprintf(srcroot_path, sizeof(srcroot_path), "%s/srcroot",
                           run_dir) > 0);
      ASSERT_EQ_INT(0, mkdir(srcroot_path, 0700));
      char *const run_argv[] = {
        TEST_SIN, "--loadonly", "-i", itemstore_path, "-s", srcroot_path,
        "-o", obj_paths[i], NULL
      };
      int run_capture_rc = test_run_argv_capture(run_argv, 2000,
                                                 &stress_cleanup.current);
      ASSERT_EQ_INT(0, remove(obj_paths[i]));
      normalize_runtime_path(stress_cleanup.current.stdout_text, srcroot_path,
                             "srcroot");
      normalize_runtime_path(stress_cleanup.current.stdout_text, itemstore_path,
                             "items.dat");
      test_normalize_text(stress_cleanup.current.stderr_text);
      ASSERT_EQ_INT(0, remove(itemstore_path));
      ASSERT_EQ_INT(0, rmdir(srcroot_path));
      ASSERT_EQ_INT(0, rmdir(run_dir));
      ASSERT_EQ_INT(0, run_capture_rc);

      char *fixture = test_read_text_file(cases[i].expected_path);
      ASSERT_NOT_NULL(fixture);
      char *expected_stdout = test_extract_fixture_block(fixture, "===stdout===\n");
      char *expected_stderr = test_extract_fixture_block(fixture, "===stderr===\n");
      char *expected_exit_text = test_extract_fixture_block(fixture, "===exit===\n");
      ASSERT_NOT_NULL(expected_stdout);
      ASSERT_NOT_NULL(expected_stderr);
      ASSERT_NOT_NULL(expected_exit_text);
      test_normalize_text(expected_stdout);
      test_normalize_text(expected_stderr);
      int expected_exit = atoi(expected_exit_text);

      int missing_line = -1;
      if (expected_exit >= 0) {
        ASSERT_EQ_INT(expected_exit, stress_cleanup.current.exit_code);
      } else {
        ASSERT_TRUE(stress_cleanup.current.exit_code != 0);
      }
      ASSERT_TRUE(test_contains_all_lines(expected_stdout,
                                          stress_cleanup.current.stdout_text,
                                          &missing_line));
      missing_line = -1;
      if (expected_stderr[0] == '\0') {
        ASSERT_EQ_INT(0, strcmp("", stress_cleanup.current.stderr_text));
      } else {
        ASSERT_TRUE(test_contains_all_lines(expected_stderr,
                                            stress_cleanup.current.stderr_text,
                                            &missing_line));
      }

      free(expected_stdout);
      free(expected_stderr);
      free(expected_exit_text);
      free(fixture);

      if (iter == 0) {
        stress_cleanup.baselines[i] = stress_cleanup.current;
        stress_cleanup.current = (TestProcessResult){0};
      } else {
        ASSERT_EQ_INT(stress_cleanup.baselines[i].exit_code,
                      stress_cleanup.current.exit_code);
        ASSERT_EQ_INT(0, strcmp(stress_cleanup.baselines[i].stdout_text,
                                stress_cleanup.current.stdout_text));
        ASSERT_EQ_INT(0, strcmp(stress_cleanup.baselines[i].stderr_text,
                                stress_cleanup.current.stderr_text));
        test_process_result_free(&stress_cleanup.current);
      }
    }
  }

  /* The framework runner exits successful children with _exit(), so this
   * explicit success-path release is required. */
  stress_cleanup_results();
  ASSERT_TRUE(stress_cleanup.current.stdout_text == NULL);
  ASSERT_TRUE(stress_cleanup.current.stderr_text == NULL);
  for (size_t i = 0; i < STRESS_CASE_COUNT; ++i) {
    ASSERT_TRUE(stress_cleanup.baselines[i].stdout_text == NULL);
    ASSERT_TRUE(stress_cleanup.baselines[i].stderr_text == NULL);
  }
}
