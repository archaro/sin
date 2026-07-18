#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "test_assert.h"
#include "test_helpers.h"

typedef struct {
  const char *name;
  const char *src_path;
  const char *expected_path;
} StressCase;

void test_interpret_stress(void) {
  const StressCase cases[] = {
      {"chat_boot", "examples/chat-boot.src", "tests/fixtures/interpret/chat-boot.expected.txt"},
      {"echo_boot", "examples/echo-boot.src", "tests/fixtures/interpret/echo-boot.expected.txt"},
  };
  const int iterations = 30;
  char obj_paths[sizeof(cases) / sizeof(cases[0])][128];
  for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
    ASSERT_EQ_INT(0, test_make_temp_path("sin-interp-stress", obj_paths[i],
                                         sizeof(obj_paths[i])));
  }

  TestProcessResult baselines[sizeof(cases) / sizeof(cases[0])];
  memset(baselines, 0, sizeof(baselines));

  for (int iter = 0; iter < iterations; ++iter) {
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
      char *const compile_argv[] = {
        "./scomp", (char *)cases[i].src_path, obj_paths[i], NULL
      };
      TestProcessResult compile_result = {0};
      int capture_rc = test_run_argv_capture(compile_argv, 0, &compile_result);
      int compile_exit = compile_result.exit_code;
      test_process_result_free(&compile_result);
      if (capture_rc != 0) remove(obj_paths[i]);
      ASSERT_EQ_INT(0, capture_rc);
      if (compile_exit != 0) remove(obj_paths[i]);
      ASSERT_EQ_INT(0, compile_exit);

      char *const run_argv[] = {"./sin", "-o", obj_paths[i], NULL};
      TestProcessResult current = {0};
      int run_capture_rc = test_run_argv_capture(run_argv, 0, &current);
      remove(obj_paths[i]);
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
        ASSERT_EQ_INT(expected_exit, current.exit_code);
      } else {
        ASSERT_TRUE(current.exit_code != 0);
      }
      ASSERT_TRUE(test_contains_all_lines(expected_stdout, current.stdout_text,
                                          &missing_line));
      ASSERT_TRUE(test_contains_all_lines(expected_stderr, current.stderr_text,
                                          &missing_line));

      free(expected_stdout);
      free(expected_stderr);
      free(expected_exit_text);
      free(fixture);

      if (iter == 0) {
        baselines[i] = current;
      } else {
        ASSERT_EQ_INT(baselines[i].exit_code, current.exit_code);
        ASSERT_EQ_INT(0, strcmp(baselines[i].stdout_text, current.stdout_text));
        ASSERT_EQ_INT(0, strcmp(baselines[i].stderr_text, current.stderr_text));
        test_process_result_free(&current);
      }
    }
  }

  for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
    test_process_result_free(&baselines[i]);
  }
}
