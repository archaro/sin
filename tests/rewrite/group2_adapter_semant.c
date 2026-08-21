#include "test_framework.h"

void test_sem_check_locals_reusable_context(void);
void test_sem_local_table_growth_oom_preserves_source_span(void);
void test_sem_foreach_semantics(void);
void test_sem_locals_in_lists_and_itemrefs(void);
void test_sem_duplicate_local_keeps_original_index(void);
void test_sem_code_params_are_treated_as_defined_locals(void);
void test_sem_seed_params_duplicate_name_only_marks_target_symbol(void);
void test_sem_code_params_duplicate_after_unrelated_locals_no_param_corruption(void);
void test_sem_embedded_scope_error_detail_includes_provenance(void);
void test_sem_many_locals_deterministic_indices(void);
void test_sem_local_limit_255_is_accepted(void);
void test_sem_local_limit_over_255_fails_deterministically(void);
void test_sem_embedded_local_limit_boundaries(void);

static const TF_TestDescriptor tests[] = {
    {"rewrite.core.test_sem_check_locals_reusable_context", test_sem_check_locals_reusable_context, "exclusive", 30000,
     "language.semantic-rule.local-definition,language.semantic-rule.local-before-definition"},
    {"rewrite.core.test_sem_local_table_growth_oom_preserves_source_span", test_sem_local_table_growth_oom_preserves_source_span, "exclusive", 30000,
     "test.core.test_sem_local_table_growth_oom_preserves_source_span"},
    {"rewrite.core.test_sem_foreach_semantics", test_sem_foreach_semantics, "exclusive", 30000,
     "language.statement.foreach,language.semantic-rule.break-context,language.semantic-rule.continue-context"},
    {"rewrite.core.test_sem_locals_in_lists_and_itemrefs", test_sem_locals_in_lists_and_itemrefs, "exclusive", 30000,
     "language.expression.list"},
    {"rewrite.core.test_sem_duplicate_local_keeps_original_index", test_sem_duplicate_local_keeps_original_index, "exclusive", 30000,
     "api.compiler.semantic-analysis"},
    {"rewrite.core.test_sem_code_params_are_treated_as_defined_locals", test_sem_code_params_are_treated_as_defined_locals, "exclusive", 30000,
     "test.core.test_sem_code_params_are_treated_as_defined_locals"},
    {"rewrite.core.test_sem_seed_params_duplicate_name_only_marks_target_symbol", test_sem_seed_params_duplicate_name_only_marks_target_symbol, "exclusive", 30000,
     "test.core.test_sem_seed_params_duplicate_name_only_marks_target_symbol"},
    {"rewrite.core.test_sem_code_params_duplicate_after_unrelated_locals_no_param_corruption", test_sem_code_params_duplicate_after_unrelated_locals_no_param_corruption, "exclusive", 30000,
     "test.core.test_sem_code_params_duplicate_after_unrelated_locals_no_param_corruption"},
    {"rewrite.core.test_sem_embedded_scope_error_detail_includes_provenance", test_sem_embedded_scope_error_detail_includes_provenance, "exclusive", 30000,
     "api.compiler.semantic-analysis"},
    {"rewrite.core.test_sem_many_locals_deterministic_indices", test_sem_many_locals_deterministic_indices, "exclusive", 30000,
     "test.core.test_sem_many_locals_deterministic_indices"},
    {"rewrite.core.test_sem_local_limit_255_is_accepted", test_sem_local_limit_255_is_accepted, "exclusive", 30000,
     "test.core.test_sem_local_limit_255_is_accepted"},
    {"rewrite.core.test_sem_local_limit_over_255_fails_deterministically", test_sem_local_limit_over_255_fails_deterministically, "exclusive", 30000,
     "language.semantic-rule.local-definition,language.semantic-rule.local-before-definition,api.compiler.semantic-analysis"},
    {"rewrite.core.test_sem_embedded_local_limit_boundaries", test_sem_embedded_local_limit_boundaries, "exclusive", 30000,
     "test.core.test_sem_embedded_local_limit_boundaries"},
};

int main(int argc, char **argv) {
  return tf_main(argc, argv, tests, sizeof tests / sizeof tests[0]);
}
