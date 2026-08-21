#include "test_framework.h"

void test_sconv_v2_canonical_and_invocation_modes(void);
void test_sconv_v1_to_v2_migrates_legacy_code(void);
void test_sconv_conversion_work_budget_is_atomic(void);
void test_sconv_decode_budget_failures_are_atomic(void);
void test_sconv_v1_embedded_nul_warns_with_full_path(void);
void test_sconv_mixed_code_tree_and_failure_atomicity(void);
void test_sconv_collisions_aliases_and_replace(void);
void test_sconv_rejects_bad_inputs_and_durability_failure(void);

static const TF_TestDescriptor tests[] = {
    {"rewrite.core.test_sconv_v1_to_v2_migrates_legacy_code",
     test_sconv_v1_to_v2_migrates_legacy_code, "exclusive", 30000,
     "api.entrypoint.sconv,executable.sconv.command-line,executable.sconv.input-output,executable.sconv.exit-status,executable.sconv.persistence,executable.sconv.errors"},
    {"rewrite.core.test_sconv_conversion_work_budget_is_atomic",
     test_sconv_conversion_work_budget_is_atomic, "exclusive", 30000,
     "api.entrypoint.sconv,executable.sconv.command-line,executable.sconv.input-output,executable.sconv.exit-status,executable.sconv.persistence,executable.sconv.errors"},
    {"rewrite.core.test_sconv_decode_budget_failures_are_atomic",
     test_sconv_decode_budget_failures_are_atomic, "exclusive", 30000,
     "api.entrypoint.sconv,executable.sconv.command-line,executable.sconv.input-output,executable.sconv.exit-status,executable.sconv.persistence,executable.sconv.errors"},
    {"rewrite.core.test_sconv_v1_embedded_nul_warns_with_full_path",
     test_sconv_v1_embedded_nul_warns_with_full_path, "exclusive", 30000,
     "api.entrypoint.sconv,executable.sconv.command-line,executable.sconv.input-output,executable.sconv.exit-status,executable.sconv.persistence,executable.sconv.errors"},
    {"rewrite.core.test_sconv_mixed_code_tree_and_failure_atomicity",
     test_sconv_mixed_code_tree_and_failure_atomicity, "exclusive", 30000,
     "api.entrypoint.sconv,executable.sconv.command-line,executable.sconv.input-output,executable.sconv.exit-status,executable.sconv.persistence,executable.sconv.errors"},
    {"rewrite.core.test_sconv_v2_canonical_and_invocation_modes",
     test_sconv_v2_canonical_and_invocation_modes, "exclusive", 30000,
     "api.common.paths,executable.sconv.command-line,executable.sconv.input-output,executable.sconv.exit-status,executable.sconv.persistence,executable.sconv.errors"},
    {"rewrite.core.test_sconv_collisions_aliases_and_replace",
     test_sconv_collisions_aliases_and_replace, "exclusive", 30000,
     "api.entrypoint.sconv,executable.sconv.command-line,executable.sconv.input-output,executable.sconv.exit-status,executable.sconv.persistence,executable.sconv.errors"},
    {"rewrite.core.test_sconv_rejects_bad_inputs_and_durability_failure",
     test_sconv_rejects_bad_inputs_and_durability_failure, "exclusive", 30000,
     "api.entrypoint.sconv,executable.sconv.command-line,executable.sconv.input-output,executable.sconv.exit-status,executable.sconv.persistence,executable.sconv.errors"},
};

int main(int argc, char **argv) {
  return tf_main(argc, argv, tests, sizeof tests / sizeof tests[0]);
}
