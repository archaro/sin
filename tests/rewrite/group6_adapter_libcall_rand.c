#include "test_framework.h"

void test_rand_registry_contract(void);
void test_rand_generator_vector_and_context_lifetime(void);
void test_rand_entropy_failures(void);
void test_rand_int_boundaries_and_rejection(void);
void test_rand_invalid_arguments(void);
void test_rand_float_and_chance_boundaries(void);
void test_rand_choice_ownership_and_selection(void);
void test_rand_preserves_errors_and_clone_failure(void);
void test_rand_source_integration_and_arity(void);

static const TF_TestDescriptor tests[] = {
    {"rewrite.runtime.test_rand_registry_contract", test_rand_registry_contract, "exclusive", 30000, "api.libcall.rand,api.libcall.table,libcall.rand.int,libcall.rand.float,libcall.rand.chance,libcall.rand.choice"},
    {"rewrite.runtime.test_rand_generator_vector_and_context_lifetime", test_rand_generator_vector_and_context_lifetime, "exclusive", 30000, "api.libcall.rand,libcall.rand.int"},
    {"rewrite.runtime.test_rand_entropy_failures", test_rand_entropy_failures, "exclusive", 30000, "api.libcall.rand,libcall.rand.int,libcall.rand.float,libcall.rand.chance,libcall.rand.choice"},
    {"rewrite.runtime.test_rand_int_boundaries_and_rejection", test_rand_int_boundaries_and_rejection, "exclusive", 30000, "api.libcall.rand,libcall.rand.int"},
    {"rewrite.runtime.test_rand_invalid_arguments", test_rand_invalid_arguments, "exclusive", 30000, "api.libcall.rand,libcall.rand.int,libcall.rand.chance,libcall.rand.choice"},
    {"rewrite.runtime.test_rand_float_and_chance_boundaries", test_rand_float_and_chance_boundaries, "exclusive", 30000, "api.libcall.rand,libcall.rand.float,libcall.rand.chance"},
    {"rewrite.runtime.test_rand_choice_ownership_and_selection", test_rand_choice_ownership_and_selection, "exclusive", 30000, "api.libcall.rand,libcall.rand.choice"},
    {"rewrite.runtime.test_rand_preserves_errors_and_clone_failure", test_rand_preserves_errors_and_clone_failure, "exclusive", 30000, "api.libcall.rand,libcall.rand.int,libcall.rand.float,libcall.rand.chance,libcall.rand.choice"},
    {"rewrite.runtime.test_rand_source_integration_and_arity", test_rand_source_integration_and_arity, "exclusive", 30000, "api.libcall.rand,language.token.tlibname,libcall.rand.int,libcall.rand.float,libcall.rand.chance,libcall.rand.choice"},
};

int main(int argc, char **argv) {
  return tf_main(argc, argv, tests, sizeof tests / sizeof tests[0]);
}
