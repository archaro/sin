#include "test_framework.h"

void test_relative_item_leading_dot_parse_accepts_deref_chain(void);
void test_relative_item_leading_dot_nested_relative_deref_layers(void);
void test_relative_item_leading_dot_nested_deref_nil_or_empty_leading_allowed(void);
void test_relative_item_leading_dot_nil_or_empty_non_leading_rejected(void);
void test_relative_item_leading_dot_boundary_max_name_after_prefix_expansion_compiles(void);
void test_relative_item_leading_dot_existing_absolute_item_unchanged(void);
void test_relative_item_leading_dot_rejects_whitespace_after_separator(void);
void test_keywords_as_layer_names_after_dot(void);
void test_float_item_literal_layer_rejected_at_compile_time(void);
void test_float_local_deref_layer_returns_nil_and_does_not_save_item(void);
void test_item_deref_value_layer_resolves_normally(void);
void test_item_deref_code_layer_is_rejected(void);
void test_item_deref_missing_layer_is_rejected(void);
void test_item_deref_invalid_result_layer_is_rejected(void);

static const TF_TestDescriptor tests[] = {
    {"rewrite.core.test_relative_item_leading_dot_parse_accepts_deref_chain", test_relative_item_leading_dot_parse_accepts_deref_chain, "exclusive", 30000,
     "language.expression.item,language.item-syntax.relative-layer"},
    {"rewrite.core.test_relative_item_leading_dot_nested_relative_deref_layers", test_relative_item_leading_dot_nested_relative_deref_layers, "exclusive", 30000,
     "language.item-syntax.relative-layer,language.item-syntax.layer-chain"},
    {"rewrite.core.test_relative_item_leading_dot_nested_deref_nil_or_empty_leading_allowed", test_relative_item_leading_dot_nested_deref_nil_or_empty_leading_allowed, "exclusive", 30000,
     "test.core.test_relative_item_leading_dot_nested_deref_nil_or_empty_leading_allowed"},
    {"rewrite.core.test_relative_item_leading_dot_nil_or_empty_non_leading_rejected", test_relative_item_leading_dot_nil_or_empty_non_leading_rejected, "exclusive", 30000,
     "test.core.test_relative_item_leading_dot_nil_or_empty_non_leading_rejected"},
    {"rewrite.core.test_relative_item_leading_dot_boundary_max_name_after_prefix_expansion_compiles", test_relative_item_leading_dot_boundary_max_name_after_prefix_expansion_compiles, "exclusive", 30000,
     "test.core.test_relative_item_leading_dot_boundary_max_name_after_prefix_expansion_compiles"},
    {"rewrite.core.test_relative_item_leading_dot_existing_absolute_item_unchanged", test_relative_item_leading_dot_existing_absolute_item_unchanged, "exclusive", 30000,
     "test.core.test_relative_item_leading_dot_existing_absolute_item_unchanged"},
    {"rewrite.core.test_relative_item_leading_dot_rejects_whitespace_after_separator", test_relative_item_leading_dot_rejects_whitespace_after_separator, "exclusive", 30000,
     "test.core.test_relative_item_leading_dot_rejects_whitespace_after_separator"},
    {"rewrite.core.test_keywords_as_layer_names_after_dot", test_keywords_as_layer_names_after_dot, "exclusive", 30000,
     "test.core.test_keywords_as_layer_names_after_dot"},
    {"rewrite.core.test_float_item_literal_layer_rejected_at_compile_time", test_float_item_literal_layer_rejected_at_compile_time, "exclusive", 30000,
     "test.core.test_float_item_literal_layer_rejected_at_compile_time"},
    {"rewrite.core.test_float_local_deref_layer_returns_nil_and_does_not_save_item", test_float_local_deref_layer_returns_nil_and_does_not_save_item, "exclusive", 30000,
     "test.core.test_float_local_deref_layer_returns_nil_and_does_not_save_item"},
    {"rewrite.core.test_item_deref_value_layer_resolves_normally", test_item_deref_value_layer_resolves_normally, "exclusive", 30000,
     "language.item-syntax.absolute-layer,language.item-syntax.dereference"},
    {"rewrite.core.test_item_deref_code_layer_is_rejected", test_item_deref_code_layer_is_rejected, "exclusive", 30000,
     "test.core.test_item_deref_code_layer_is_rejected"},
    {"rewrite.core.test_item_deref_missing_layer_is_rejected", test_item_deref_missing_layer_is_rejected, "exclusive", 30000,
     "language.item-syntax.dereference"},
    {"rewrite.core.test_item_deref_invalid_result_layer_is_rejected", test_item_deref_invalid_result_layer_is_rejected, "exclusive", 30000,
     "test.core.test_item_deref_invalid_result_layer_is_rejected"},
};

int main(int argc, char **argv) {
  return tf_main(argc, argv, tests, sizeof tests / sizeof tests[0]);
}
