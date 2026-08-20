#include "test_framework.h"

void test_compiler_diag_pipeline(void);
void test_compiler_diag_rejects_deep_foreach_with_dedicated_detail(void);
void test_compiler_diag_allows_sequential_foreach_hidden_local_reuse(void);
void test_cli_metadata_stdout_stderr_and_status(void);

static const TF_TestDescriptor tests[] = {
    {"rewrite.compiler.test_compiler_diag_pipeline", test_compiler_diag_pipeline, "exclusive", 30000,
     "api.compiler.diagnostics,api.entrypoint.scomp,baseline.legacy.unified.compiler.test_compiler_diag_pipeline,executable.scomp.command-line,executable.scomp.errors,executable.scomp.exit-status,executable.scomp.input-output,executable.scomp.persistence"},
    {"rewrite.compiler.test_compiler_diag_rejects_deep_foreach_with_dedicated_detail", test_compiler_diag_rejects_deep_foreach_with_dedicated_detail, "exclusive", 30000,
     "api.entrypoint.scomp,baseline.legacy.unified.compiler.test_compiler_diag_rejects_deep_foreach_with_dedicated_detail,executable.scomp.command-line,executable.scomp.errors,executable.scomp.exit-status,executable.scomp.input-output,executable.scomp.persistence,language.statement.foreach"},
    {"rewrite.compiler.test_compiler_diag_allows_sequential_foreach_hidden_local_reuse", test_compiler_diag_allows_sequential_foreach_hidden_local_reuse, "exclusive", 30000,
     "api.entrypoint.scomp,baseline.legacy.unified.compiler.test_compiler_diag_allows_sequential_foreach_hidden_local_reuse,executable.scomp.command-line,executable.scomp.errors,executable.scomp.exit-status,executable.scomp.input-output,executable.scomp.persistence,language.statement.foreach"},
    {"rewrite.compiler.test_cli_metadata_stdout_stderr_and_status", test_cli_metadata_stdout_stderr_and_status, "exclusive", 30000,
     "api.entrypoint.scomp,baseline.legacy.unified.compiler.test_cli_metadata_stdout_stderr_and_status,executable.scomp.command-line,executable.scomp.errors,executable.scomp.exit-status,executable.scomp.input-output,executable.scomp.persistence"},
};

int main(int argc, char **argv) {
  return tf_main(argc, argv, tests, sizeof tests / sizeof tests[0]);
}
