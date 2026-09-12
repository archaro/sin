#include "test_framework.h"

void test_text_split_registry_contract(void);
void test_text_split_literal_fields_and_utf8(void);
void test_text_split_empty_and_unmatched_behavior(void);
void test_text_split_invalid_types_and_stack_contract(void);
void test_text_split_invalid_context_root_and_return_pointer(void);
void test_text_split_distinct_allocation_failures(void);
void test_text_split_list_limit_boundaries(void);
void test_text_split_failures_preserve_diagnostic_and_cleanup(void);
void test_text_split_source_integration_and_arity(void);
void test_text_words_registry_contract(void);
void test_text_words_literal_whitespace_and_utf8(void);
void test_text_words_empty_single_and_ownership(void);
void test_text_words_invalid_types_and_stack_contract(void);
void test_text_words_invalid_context_root_and_return_pointer(void);
void test_text_words_distinct_allocation_failures(void);
void test_text_words_string_limit_boundaries(void);
void test_text_words_source_integration_and_arity(void);
void test_text_lines_registry_contract(void);
void test_text_lines_literal_terminators_and_bytes(void);
void test_text_lines_empty_and_ownership(void);
void test_text_lines_invalid_types_and_stack_contract(void);
void test_text_lines_invalid_context_root_and_return_pointer(void);
void test_text_lines_distinct_allocation_failures(void);
void test_text_lines_string_and_list_boundaries(void);
void test_text_lines_source_integration_and_arity(void);
void test_text_join_registry_contract(void);
void test_text_join_literal_fields_and_utf8(void);
void test_text_join_empty_and_separator_behavior(void);
void test_text_join_invalid_types_and_stack_contract(void);
void test_text_join_invalid_context_root_and_return_pointer(void);
void test_text_join_string_limit_boundaries(void);
void test_text_join_distinct_allocation_failures(void);
void test_text_join_failures_preserve_diagnostic_and_cleanup(void);
void test_text_join_source_integration_and_arity(void);

static const TF_TestDescriptor tests[] = {
    {"rewrite.runtime.test_text_split_registry_contract", test_text_split_registry_contract, "exclusive", 30000, "api.libcall.text,libcall.text.split"},
    {"rewrite.runtime.test_text_split_literal_fields_and_utf8", test_text_split_literal_fields_and_utf8, "exclusive", 30000, "api.libcall.text,libcall.text.split"},
    {"rewrite.runtime.test_text_split_empty_and_unmatched_behavior", test_text_split_empty_and_unmatched_behavior, "exclusive", 30000, "api.libcall.text,libcall.text.split"},
    {"rewrite.runtime.test_text_split_invalid_types_and_stack_contract", test_text_split_invalid_types_and_stack_contract, "exclusive", 30000, "api.libcall.text,libcall.text.split"},
    {"rewrite.runtime.test_text_split_invalid_context_root_and_return_pointer", test_text_split_invalid_context_root_and_return_pointer, "exclusive", 30000, "api.libcall.text,libcall.text.split"},
    {"rewrite.runtime.test_text_split_distinct_allocation_failures", test_text_split_distinct_allocation_failures, "exclusive", 30000, "api.libcall.text,libcall.text.split"},
    {"rewrite.runtime.test_text_split_list_limit_boundaries", test_text_split_list_limit_boundaries, "exclusive", 30000, "api.libcall.text,libcall.text.split"},
    {"rewrite.runtime.test_text_split_failures_preserve_diagnostic_and_cleanup", test_text_split_failures_preserve_diagnostic_and_cleanup, "exclusive", 30000, "api.libcall.text,libcall.text.split"},
    {"rewrite.runtime.test_text_split_source_integration_and_arity", test_text_split_source_integration_and_arity, "exclusive", 30000, "api.libcall.text,libcall.text.split"},
    {"rewrite.runtime.test_text_words_registry_contract", test_text_words_registry_contract, "exclusive", 30000, "api.libcall.table,api.libcall.text.words,libcall.text.words"},
    {"rewrite.runtime.test_text_words_literal_whitespace_and_utf8", test_text_words_literal_whitespace_and_utf8, "exclusive", 30000, "api.libcall.text.words,libcall.text.words"},
    {"rewrite.runtime.test_text_words_empty_single_and_ownership", test_text_words_empty_single_and_ownership, "exclusive", 30000, "api.libcall.text.words,libcall.text.words"},
    {"rewrite.runtime.test_text_words_invalid_types_and_stack_contract", test_text_words_invalid_types_and_stack_contract, "exclusive", 30000, "api.libcall.text.words,libcall.text.words"},
    {"rewrite.runtime.test_text_words_invalid_context_root_and_return_pointer", test_text_words_invalid_context_root_and_return_pointer, "exclusive", 30000, "api.libcall.text.words,libcall.text.words"},
    {"rewrite.runtime.test_text_words_distinct_allocation_failures", test_text_words_distinct_allocation_failures, "exclusive", 30000, "api.libcall.text.words,libcall.text.words"},
    {"rewrite.runtime.test_text_words_string_limit_boundaries", test_text_words_string_limit_boundaries, "exclusive", 30000, "api.libcall.text.words,libcall.text.words"},
    {"rewrite.runtime.test_text_words_source_integration_and_arity", test_text_words_source_integration_and_arity, "exclusive", 30000, "api.libcall.text.words,libcall.text.words"},
    {"rewrite.runtime.test_text_lines_registry_contract", test_text_lines_registry_contract, "exclusive", 30000, "api.libcall.table,api.libcall.text.lines,libcall.text.lines"},
    {"rewrite.runtime.test_text_lines_literal_terminators_and_bytes", test_text_lines_literal_terminators_and_bytes, "exclusive", 30000, "api.libcall.text.lines,libcall.text.lines"},
    {"rewrite.runtime.test_text_lines_empty_and_ownership", test_text_lines_empty_and_ownership, "exclusive", 30000, "api.libcall.text.lines,libcall.text.lines"},
    {"rewrite.runtime.test_text_lines_invalid_types_and_stack_contract", test_text_lines_invalid_types_and_stack_contract, "exclusive", 30000, "api.libcall.text.lines,libcall.text.lines"},
    {"rewrite.runtime.test_text_lines_invalid_context_root_and_return_pointer", test_text_lines_invalid_context_root_and_return_pointer, "exclusive", 30000, "api.libcall.text.lines,libcall.text.lines"},
    {"rewrite.runtime.test_text_lines_distinct_allocation_failures", test_text_lines_distinct_allocation_failures, "exclusive", 30000, "api.libcall.text.lines,libcall.text.lines"},
    {"rewrite.runtime.test_text_lines_string_and_list_boundaries", test_text_lines_string_and_list_boundaries, "exclusive", 30000, "api.libcall.text.lines,libcall.text.lines"},
    {"rewrite.runtime.test_text_lines_source_integration_and_arity", test_text_lines_source_integration_and_arity, "exclusive", 30000, "api.libcall.text.lines,libcall.text.lines"},
    {"rewrite.runtime.test_text_join_registry_contract", test_text_join_registry_contract, "exclusive", 30000, "api.libcall.text,libcall.text.join"},
    {"rewrite.runtime.test_text_join_literal_fields_and_utf8", test_text_join_literal_fields_and_utf8, "exclusive", 30000, "api.libcall.text,libcall.text.join"},
    {"rewrite.runtime.test_text_join_empty_and_separator_behavior", test_text_join_empty_and_separator_behavior, "exclusive", 30000, "api.libcall.text,libcall.text.join"},
    {"rewrite.runtime.test_text_join_invalid_types_and_stack_contract", test_text_join_invalid_types_and_stack_contract, "exclusive", 30000, "api.libcall.text,libcall.text.join"},
    {"rewrite.runtime.test_text_join_invalid_context_root_and_return_pointer", test_text_join_invalid_context_root_and_return_pointer, "exclusive", 30000, "api.libcall.text,libcall.text.join"},
    {"rewrite.runtime.test_text_join_string_limit_boundaries", test_text_join_string_limit_boundaries, "exclusive", 30000, "api.libcall.text,libcall.text.join"},
    {"rewrite.runtime.test_text_join_distinct_allocation_failures", test_text_join_distinct_allocation_failures, "exclusive", 30000, "api.libcall.text,libcall.text.join"},
    {"rewrite.runtime.test_text_join_failures_preserve_diagnostic_and_cleanup", test_text_join_failures_preserve_diagnostic_and_cleanup, "exclusive", 30000, "api.libcall.text,libcall.text.join"},
    {"rewrite.runtime.test_text_join_source_integration_and_arity", test_text_join_source_integration_and_arity, "exclusive", 30000, "api.libcall.text,libcall.text.join"},
};

int main(int argc, char **argv) {
  return tf_main(argc, argv, tests, sizeof tests / sizeof tests[0]);
}
