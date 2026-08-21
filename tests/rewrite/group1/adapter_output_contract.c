#include "test_framework.h"

#include <stdio.h>
#include <stdlib.h>

static const char *text_or_empty(const char *text) {
  return text ? text : "";
}

static void run_process(char *const argv[], int expected_status,
                        TF_ProcessResult *result) {
  TF_ASSERT_TRUE(tf_process_run(argv, 30000, result) == 0);
  TF_ASSERT_PROCESS(result, expected_status);
}

static void destroy_process(TF_ProcessResult *result) {
  tf_process_result_destroy(result);
}

static void success_capture(void) {
  const char *self = getenv("TF_FRAMEWORK_SELF");
  char *args[] = {(char *)self, "--run", "process_capture", NULL};
  TF_ProcessResult result;
  TF_ASSERT_TRUE(self && self[0]);
  run_process(args, 0, &result);
  TF_ASSERT_DIAGNOSTIC("TF|RESULT|process_capture|PASS|", result.stdout_data);
  TF_ASSERT_DIAGNOSTIC("TF|TOTAL|selected|1|1|0", result.stdout_data);
  TF_ASSERT_FALSE(strstr(text_or_empty(result.stdout_data), "out") != NULL);
  TF_ASSERT_FALSE(strstr(text_or_empty(result.stdout_data), "err") != NULL);
  TF_ASSERT_STR("", text_or_empty(result.stderr_data));
  destroy_process(&result);
}

static void one_wrapper(void) {
  const char *self = getenv("TF_FRAMEWORK_SELF");
  const char *first_result;
  char *args[] = {(char *)self, "--run", "tagged_result_record", NULL};
  TF_ProcessResult result;
  TF_ASSERT_TRUE(self && self[0]);
  run_process(args, 0, &result);
  TF_ASSERT_DIAGNOSTIC("TF|RESULT|tagged_result_record|PASS|", result.stdout_data);
  TF_ASSERT_DIAGNOSTIC("TF|TOTAL|selected|1|1|0", result.stdout_data);
  first_result = strstr(text_or_empty(result.stdout_data), "TF|RESULT|");
  TF_ASSERT_TRUE(first_result != NULL);
  TF_ASSERT_FALSE(strstr(first_result + sizeof("TF|RESULT|") - 1,
                         "TF|RESULT|") != NULL);
  destroy_process(&result);
}

static void controlled_failure(void) {
  const char *negative = getenv("TF_FRAMEWORK_NEGATIVE");
  char *args[] = {(char *)negative, "--run", "assert_fail_bool", NULL};
  TF_ProcessResult result;
  TF_ASSERT_TRUE(negative && negative[0]);
  run_process(args, 1, &result);
  TF_ASSERT_DIAGNOSTIC("TF|RESULT|assert_fail_bool|FAIL|", result.stdout_data);
  TF_ASSERT_DIAGNOSTIC("TF|TOTAL|selected|1|0|1", result.stdout_data);
  TF_ASSERT_DIAGNOSTIC("assertion failed at", result.stderr_data);
  TF_ASSERT_DIAGNOSTIC("expected:", result.stderr_data);
  TF_ASSERT_DIAGNOSTIC("actual:", result.stderr_data);
  destroy_process(&result);
}

static void abnormal_exit(void) {
  const char *negative = getenv("TF_FRAMEWORK_NEGATIVE");
  char *args[] = {(char *)negative, "--run", "crash", NULL};
  TF_ProcessResult result;
  TF_ASSERT_TRUE(negative && negative[0]);
  run_process(args, 1, &result);
  TF_ASSERT_DIAGNOSTIC("TF|RESULT|crash|FAIL|", result.stdout_data);
  TF_ASSERT_DIAGNOSTIC("TF|TOTAL|selected|1|0|1", result.stdout_data);
  TF_ASSERT_DIAGNOSTIC("terminated by signal 6", result.stderr_data);
  destroy_process(&result);
}

static void write_makefile(const char *path, const char *body) {
  FILE *file = fopen(path, "w");
  TF_ASSERT_TRUE(file != NULL);
  TF_ASSERT_TRUE(fputs(body, file) >= 0);
  TF_ASSERT_TRUE(fclose(file) == 0);
}

static void make_success(void) {
  const char *runner = getenv("TF_FRAMEWORK_RUNNER");
  const char *self = getenv("TF_FRAMEWORK_SELF");
  TF_Fixture fixture;
  char makefile[4096];
  char body[8192];
  char *args[] = {"make", "--no-print-directory", "-f", makefile, "all", NULL};
  TF_ProcessResult result;
  TF_ASSERT_TRUE(runner && runner[0] && self && self[0]);
  tf_fixture_init(&fixture);
  TF_ASSERT_TRUE(tf_fixture_file(&fixture, "Makefile.success", makefile,
                                 sizeof makefile) == 0);
  TF_ASSERT_TRUE(snprintf(body, sizeof body,
      ".PHONY: all\n"
      "all:\n"
      "\t@printf 'MAKE|compile|fixture\\n'\n"
      "\t@%s %s\n"
      "\t@printf 'MAKE|build|success\\n'\n", runner, self) > 0);
  write_makefile(makefile, body);
  run_process(args, 0, &result);
  TF_ASSERT_DIAGNOSTIC("MAKE|compile|fixture", result.stdout_data);
  TF_ASSERT_DIAGNOSTIC("TF|TOTAL|all|22|22|0", result.stdout_data);
  TF_ASSERT_DIAGNOSTIC("MAKE|build|success", result.stdout_data);
  TF_ASSERT_STR("", text_or_empty(result.stderr_data));
  destroy_process(&result);
  tf_fixture_cleanup(&fixture);
}

static void make_failure(void) {
  TF_Fixture fixture;
  char makefile[4096];
  static const char body[] =
      ".PHONY: all\n"
      "all:\n"
      "\t@printf 'MAKE|compile|failure\\n'\n"
      "\t@printf 'MAKE|diagnostic|expected\\n' >&2\n"
      "\t@false\n";
  char *args[] = {"make", "--no-print-directory", "-f", makefile, "all", NULL};
  TF_ProcessResult result;
  tf_fixture_init(&fixture);
  TF_ASSERT_TRUE(tf_fixture_file(&fixture, "Makefile.failure", makefile,
                                 sizeof makefile) == 0);
  write_makefile(makefile, body);
  run_process(args, 2, &result);
  TF_ASSERT_DIAGNOSTIC("MAKE|compile|failure", result.stdout_data);
  TF_ASSERT_DIAGNOSTIC("MAKE|diagnostic|expected", result.stderr_data);
  TF_ASSERT_DIAGNOSTIC("Error 1", result.stderr_data);
  destroy_process(&result);
  tf_fixture_cleanup(&fixture);
}

static void aggregate_success(void) {
  const char *runner = getenv("TF_FRAMEWORK_RUNNER");
  const char *self = getenv("TF_FRAMEWORK_SELF");
  const char *conformance = getenv("TF_FRAMEWORK_CONFORMANCE");
  char *args[] = {(char *)runner, (char *)self, (char *)conformance, NULL};
  TF_ProcessResult result;
  TF_ASSERT_TRUE(runner && runner[0] && self && self[0] &&
                 conformance && conformance[0]);
  run_process(args, 0, &result);
  TF_ASSERT_DIAGNOSTIC("TF|TOTAL|all|32|32|0", result.stdout_data);
  TF_ASSERT_FALSE(strstr(text_or_empty(result.stdout_data), "|FAIL|") != NULL);
  TF_ASSERT_STR("", text_or_empty(result.stderr_data));
  destroy_process(&result);
}

static void aggregate_failure(void) {
  enum {
    FRAMEWORK_SELF_TESTS = 22,
    NEGATIVE_FIXTURE_TESTS = 15,
    NEGATIVE_FIXTURE_FAILURES = 12,
  };
  const char *runner = getenv("TF_FRAMEWORK_RUNNER");
  const char *self = getenv("TF_FRAMEWORK_SELF");
  const char *duplicate = getenv("TF_FRAMEWORK_DUPLICATE");
  const char *negative = getenv("TF_FRAMEWORK_NEGATIVE");
  char *duplicate_args[] = {(char *)runner, (char *)self, (char *)duplicate, NULL};
  char *failing_args[] = {(char *)runner, (char *)self, (char *)negative, NULL};
  char expected_total[64];
  TF_ProcessResult result;
  TF_ASSERT_TRUE(runner && runner[0] && self && self[0] && duplicate &&
                 duplicate[0] && negative && negative[0]);
  run_process(duplicate_args, 2, &result);
  TF_ASSERT_DIAGNOSTIC("TF|ERROR|duplicate ID across executables", result.stderr_data);
  destroy_process(&result);
  run_process(failing_args, 1, &result);
  /* Three negative-fixture helpers intentionally pass, so 22 + 3 pass and
   * 12 fail in the aggregate rather than every negative entry failing. */
  TF_ASSERT_TRUE(snprintf(expected_total, sizeof expected_total,
                          "TF|TOTAL|all|%d|%d|%d",
                          FRAMEWORK_SELF_TESTS + NEGATIVE_FIXTURE_TESTS,
                          FRAMEWORK_SELF_TESTS + NEGATIVE_FIXTURE_TESTS -
                            NEGATIVE_FIXTURE_FAILURES,
                          NEGATIVE_FIXTURE_FAILURES) > 0);
  TF_ASSERT_DIAGNOSTIC(expected_total, result.stdout_data);
  TF_ASSERT_DIAGNOSTIC("TF|RESULT|assert_fail_bool|FAIL|", result.stdout_data);
  TF_ASSERT_DIAGNOSTIC("assertion failed at", result.stderr_data);
  destroy_process(&result);
}

static const TF_TestDescriptor tests[] = {
    {"rewrite.output_contract.quiet_runner.success_capture",
     success_capture, "exclusive", 30000,
     "baseline.legacy.output_contract.quiet_runner.success_capture"},
    {"rewrite.output_contract.quiet_runner.one_wrapper",
     one_wrapper, "exclusive", 30000,
     "baseline.legacy.output_contract.quiet_runner.one_wrapper"},
    {"rewrite.output_contract.quiet_runner.controlled_failure",
     controlled_failure, "exclusive", 30000,
     "baseline.legacy.output_contract.quiet_runner.controlled_failure"},
    {"rewrite.output_contract.quiet_runner.abnormal_exit",
     abnormal_exit, "exclusive", 30000,
     "baseline.legacy.output_contract.quiet_runner.abnormal_exit"},
    {"rewrite.output_contract.quiet_runner.make_success",
     make_success, "exclusive", 30000,
     "baseline.legacy.output_contract.quiet_runner.make_success"},
    {"rewrite.output_contract.quiet_runner.make_failure",
     make_failure, "exclusive", 30000,
     "baseline.legacy.output_contract.quiet_runner.make_failure"},
    {"rewrite.output_contract.quiet_runner.aggregate_success",
     aggregate_success, "exclusive", 30000,
     "baseline.legacy.output_contract.quiet_runner.aggregate_success"},
    {"rewrite.output_contract.quiet_runner.aggregate_failure",
     aggregate_failure, "exclusive", 30000,
     "baseline.legacy.output_contract.quiet_runner.aggregate_failure"},
};

int main(int argc, char **argv) {
  return tf_main(argc, argv, tests, sizeof tests / sizeof tests[0]);
}
