#include "test_framework.h"

void test_sin_itemstore_version_policy(void);
void test_sin_default_source_root_validation(void);
void test_sin_boot_frees_aggregate_return_values(void);

static const TF_TestDescriptor tests[] = {
    {"rewrite.core.test_sin_itemstore_version_policy",
     test_sin_itemstore_version_policy, "exclusive", 30000,
     "api.entrypoint.sin,baseline.legacy.unified.core.test_sin_itemstore_version_policy,executable.sin.command-line,executable.sin.errors,executable.sin.exit-status,executable.sin.input-output,executable.sin.persistence"},
    {"rewrite.core.test_sin_default_source_root_validation",
     test_sin_default_source_root_validation, "exclusive", 30000,
     "api.entrypoint.sin,baseline.legacy.unified.core.test_sin_default_source_root_validation,executable.sin.command-line,executable.sin.errors,executable.sin.exit-status,executable.sin.input-output,executable.sin.persistence"},
    {"rewrite.runtime.test_sin_boot_frees_aggregate_return_values",
     test_sin_boot_frees_aggregate_return_values, "exclusive", 30000,
     "api.entrypoint.sin,baseline.legacy.unified.runtime.test_sin_boot_frees_aggregate_return_values,executable.sin.command-line,executable.sin.errors,executable.sin.exit-status,executable.sin.input-output,executable.sin.persistence"},
};

int main(int argc, char **argv) {
  return tf_main(argc, argv, tests, sizeof tests / sizeof tests[0]);
}
