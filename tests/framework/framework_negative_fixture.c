#include "test_framework.h"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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
static void output_helper(void) { (void)printf("visible stdout\n"); (void)fprintf(stderr, "visible stderr\n"); }
static void fixture_failure_helper(void) {
  const char *marker = getenv("TF_FIXTURE_MARKER");
  TF_Fixture fixture;
  tf_fixture_init(&fixture);
  if (marker) {
    FILE *file = fopen(marker, "w");
    if (file) { (void)fprintf(file, "%s", tf_fixture_path(&fixture)); (void)fclose(file); }
  }
  TF_ASSERT_TRUE(false);
}
static void fixture_implicit_helper(void) {
  const char *marker = getenv("TF_FIXTURE_MARKER");
  TF_Fixture fixture;
  tf_fixture_init(&fixture);
  if (marker) {
    FILE *file = fopen(marker, "w");
    if (file) { (void)fprintf(file, "%s", tf_fixture_path(&fixture)); (void)fclose(file); }
  }
}
static void fixture_multiple_helper(void) {
  const char *marker = getenv("TF_FIXTURE_MARKER");
  TF_Fixture first, second;
  tf_fixture_init(&first); tf_fixture_init(&second);
  if (marker) {
    FILE *file = fopen(marker, "w");
    if (file) { (void)fprintf(file, "%s\n%s", tf_fixture_path(&first), tf_fixture_path(&second)); (void)fclose(file); }
  }
}
static void assert_process_capture_failure(void) {
  TF_ProcessResult result = {.exited = true, .exit_status = 0, .capture_failed = true};
  TF_ASSERT_PROCESS(&result, 0);
}
static void legacy_failf_long(void) {
  static const char sentinel[] = "LEGACY_FAILF_SENTINEL_AFTER_BYTE_512";
  char diagnostic[1024];
  memset(diagnostic, 'Z', sizeof diagnostic - 1u);
  memcpy(diagnostic + 768u, sentinel, sizeof sentinel - 1u);
  diagnostic[sizeof diagnostic - 1u] = '\0';
  tf_legacy_failf(__FILE__, __LINE__, "%s", diagnostic);
}

static const TF_TestDescriptor tests[] = {
  {"assert_fail_bool", assert_fail_bool, "", 500, "framework.assertions"},
  {"assert_fail_i64", assert_fail_i64, "", 500, "framework.assertions"},
  {"assert_fail_u64", assert_fail_u64, "", 500, "framework.assertions"},
  {"assert_fail_str", assert_fail_str, "", 500, "framework.assertions"},
  {"assert_fail_bytes", assert_fail_bytes, "", 500, "framework.assertions"},
  {"assert_fail_float", assert_fail_float, "", 500, "framework.assertions"},
  {"assert_fail_diag", assert_fail_diag, "", 500, "framework.assertions"},
  {"crash", crash_test, "", 5000, "framework.process"},
  {"output_helper", output_helper, "", 500, "framework.output"},
  {"fixture_failure_helper", fixture_failure_helper, "", 1000, "framework.fixtures"},
  {"fixture_implicit_helper", fixture_implicit_helper, "", 1000, "framework.fixtures"},
  {"fixture_multiple_helper", fixture_multiple_helper, "", 1000, "framework.fixtures"},
  {"assert_process_capture_failure", assert_process_capture_failure, "", 500, "framework.process"},
  {"legacy_failf_long", legacy_failf_long, "", 500, "framework.assertions"},
  {"hang", hang_test, "", 500, "framework.process"}
};

int main(int argc, char **argv) {
  return tf_main(argc, argv, tests, sizeof tests / sizeof tests[0]);
}
