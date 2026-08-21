#include "test_framework.h"

void test_list_basic_ownership_and_access(void);
void test_list_rendering_contract(void);
void test_list_leaf_iterator_boundaries_and_observability(void);
void test_list_equality_iterator_fast_paths_and_early_exit(void);
void test_list_concat_shares_rhs_leaves(void);
void test_list_slice_shares_aligned_leaves_and_boundaries(void);
void test_list_boundaries_persistence_and_equality(void);
void test_list_limits_invalid_inputs_and_failures(void);

static const TF_TestDescriptor tests[] = {
    {"rewrite.core.test_list_basic_ownership_and_access", test_list_basic_ownership_and_access, "", 30000,
     "api.runtime.list"},
    {"rewrite.core.test_list_rendering_contract", test_list_rendering_contract, "", 30000,
     "api.runtime.list"},
    {"rewrite.core.test_list_leaf_iterator_boundaries_and_observability", test_list_leaf_iterator_boundaries_and_observability, "", 30000,
     "api.runtime.list"},
    {"rewrite.core.test_list_equality_iterator_fast_paths_and_early_exit", test_list_equality_iterator_fast_paths_and_early_exit, "", 30000,
     "api.runtime.list"},
    {"rewrite.core.test_list_concat_shares_rhs_leaves", test_list_concat_shares_rhs_leaves, "", 30000,
     "api.runtime.list"},
    {"rewrite.core.test_list_slice_shares_aligned_leaves_and_boundaries", test_list_slice_shares_aligned_leaves_and_boundaries, "", 30000,
     "api.runtime.list"},
    {"rewrite.core.test_list_boundaries_persistence_and_equality", test_list_boundaries_persistence_and_equality, "exclusive", 30000,
     "api.runtime.list"},
    {"rewrite.core.test_list_limits_invalid_inputs_and_failures", test_list_limits_invalid_inputs_and_failures, "exclusive", 30000,
     "api.common.memory,api.runtime.list"},
};

int main(int argc, char **argv) {
  return tf_main(argc, argv, tests, sizeof tests / sizeof tests[0]);
}
