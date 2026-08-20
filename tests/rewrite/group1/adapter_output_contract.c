#include "test_framework.h"

static void run_quiet_output_case(const char *name) {
  char *args[] = {"tests/test_quiet_output.sh", (char *)name, NULL};
  TF_ProcessResult result;
  TF_ASSERT_TRUE(tf_process_run(args, 30000, &result) == 0);
  TF_ASSERT_PROCESS(&result, 0);
  tf_process_result_destroy(&result);
}

static void success_capture(void) { run_quiet_output_case("success_capture"); }
static void one_wrapper(void) { run_quiet_output_case("one_wrapper"); }
static void controlled_failure(void) { run_quiet_output_case("controlled_failure"); }
static void abnormal_exit(void) { run_quiet_output_case("abnormal_exit"); }
static void make_success(void) { run_quiet_output_case("make_success"); }
static void make_failure(void) { run_quiet_output_case("make_failure"); }
static void aggregate_success(void) { run_quiet_output_case("aggregate_success"); }
static void aggregate_failure(void) { run_quiet_output_case("aggregate_failure"); }

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
