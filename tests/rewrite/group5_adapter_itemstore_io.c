#include "test_framework.h"

void test_get_itemname_root_item(void);
void test_loaded_zero_child_item_can_gain_runtime_child(void);
void test_itemstore_value_and_code_roundtrip(void);
void test_item_value_accessor_rejects_code_items(void);
void test_itemstore_v2_lists_and_itemrefs_roundtrip(void);
void test_itemstore_v2_all_values_fixture(void);
void test_itemstore_v2_malformed_value_table(void);
void test_itemstore_v2_budget_and_malformed_save(void);
void test_itemstore_whole_file_budgets(void);
void test_itemstore_decode_budget_allocation_boundaries(void);
void test_itemstore_v1_lossy_path_budget_aborts_record(void);
void test_itemstore_v1_rejects_invalid_boolean_payload(void);
void test_itemstore_rejects_production_record_limit(void);
void test_itemstore_save_preflight_budget_boundaries(void);
void test_loaded_itemstore_mutation_roundtrip(void);
void test_load_itemstore_handles_constructor_failure_with_children(void);
void test_item_set_code_rejects_inuse_replacement(void);
void test_item_execution_pins_are_balanced_and_zero_safe(void);
void test_item_delete_rejects_pinned_descendant(void);
void test_itemstore_payload_replacement_contracts(void);
void test_itemstore_path_creation_rolls_back_on_failure(void);
void test_itemstore_boot_rejects_overlong_root_name(void);
void test_itemstore_nested_depth_roundtrip(void);
void test_itemstore_item_name_contract_boundaries_roundtrip(void);
void test_itemstore_item_name_rejection_is_atomic(void);
void test_itemstore_item_name_relative_depth_contract(void);
void test_save_itemstore_rejects_manually_invalid_item_names(void);
void test_itemstore_loads_generated_v1_wire_fixture(void);
void test_load_itemstore_rejects_bad_headers(void);
void test_load_itemstore_rejects_malformed_code_bytecode(void);
void test_load_itemstore_allows_malformed_code_when_strict_validation_disabled(void);
void test_load_itemstore_rejects_invalid_wire_tags(void);
void test_load_itemstore_rejects_structural_corruption(void);
void test_load_itemstore_rejects_resource_limit_violations(void);
void test_save_itemstore_preserves_existing_file_on_failure(void);
void test_save_itemsource_reports_write_and_close_failure(void);
void test_itemsource_paths_are_validated_and_contained(void);
void test_itemstore_durability_modes(void);
void test_itemstore_large_load_presizes_child_storage(void);

static const TF_TestDescriptor tests[] = {
    {"rewrite.core.test_get_itemname_root_item", test_get_itemname_root_item, "exclusive", 30000, "api.itemstore.tree-and-values,executable.sin.command-line,executable.sin.input-output,executable.sin.exit-status,executable.sin.persistence,executable.sin.errors"},
    {"rewrite.core.test_loaded_zero_child_item_can_gain_runtime_child", test_loaded_zero_child_item_can_gain_runtime_child, "exclusive", 30000, "api.itemstore.tree-and-values,api.entrypoint.sin,executable.sin.command-line,executable.sin.input-output,executable.sin.exit-status,executable.sin.persistence,executable.sin.errors"},
    {"rewrite.core.test_itemstore_value_and_code_roundtrip", test_itemstore_value_and_code_roundtrip, "exclusive", 30000, "api.itemstore.persistence-format"},
    {"rewrite.core.test_item_value_accessor_rejects_code_items", test_item_value_accessor_rejects_code_items, "exclusive", 30000, "api.itemstore.tree-and-values"},
    {"rewrite.core.test_itemstore_v2_lists_and_itemrefs_roundtrip", test_itemstore_v2_lists_and_itemrefs_roundtrip, "exclusive", 30000, "api.itemstore.persistence-format"},
    {"rewrite.core.test_itemstore_v2_all_values_fixture", test_itemstore_v2_all_values_fixture, "exclusive", 30000, "api.itemstore.persistence-format"},
    {"rewrite.core.test_itemstore_v2_malformed_value_table", test_itemstore_v2_malformed_value_table, "exclusive", 30000, "api.itemstore.persistence-format"},
    {"rewrite.core.test_itemstore_v2_budget_and_malformed_save", test_itemstore_v2_budget_and_malformed_save, "exclusive", 30000, "api.itemstore.persistence-save"},
    {"rewrite.core.test_itemstore_whole_file_budgets", test_itemstore_whole_file_budgets, "exclusive", 30000, "api.itemstore.persistence-load"},
    {"rewrite.core.test_itemstore_decode_budget_allocation_boundaries", test_itemstore_decode_budget_allocation_boundaries, "exclusive", 30000, "api.itemstore.persistence-load"},
    {"rewrite.core.test_itemstore_v1_lossy_path_budget_aborts_record", test_itemstore_v1_lossy_path_budget_aborts_record, "exclusive", 30000, "api.itemstore.persistence-load"},
    {"rewrite.core.test_itemstore_v1_rejects_invalid_boolean_payload", test_itemstore_v1_rejects_invalid_boolean_payload, "exclusive", 30000, "api.itemstore.persistence-load"},
    {"rewrite.core.test_itemstore_rejects_production_record_limit", test_itemstore_rejects_production_record_limit, "exclusive", 30000, "api.itemstore.persistence-load"},
    {"rewrite.core.test_itemstore_save_preflight_budget_boundaries", test_itemstore_save_preflight_budget_boundaries, "exclusive", 30000, "api.itemstore.persistence-save"},
    {"rewrite.core.test_loaded_itemstore_mutation_roundtrip", test_loaded_itemstore_mutation_roundtrip, "exclusive", 30000, "api.itemstore.tree-and-values"},
    {"rewrite.core.test_load_itemstore_handles_constructor_failure_with_children", test_load_itemstore_handles_constructor_failure_with_children, "exclusive", 30000, "api.itemstore.persistence-load"},
    {"rewrite.core.test_item_set_code_rejects_inuse_replacement", test_item_set_code_rejects_inuse_replacement, "exclusive", 30000, "api.itemstore.tree-and-values"},
    {"rewrite.core.test_item_execution_pins_are_balanced_and_zero_safe", test_item_execution_pins_are_balanced_and_zero_safe, "exclusive", 30000, "api.itemstore.tree-and-values,executable.sin.command-line,executable.sin.input-output,executable.sin.exit-status,executable.sin.persistence,executable.sin.errors"},
    {"rewrite.core.test_item_delete_rejects_pinned_descendant", test_item_delete_rejects_pinned_descendant, "exclusive", 30000, "api.itemstore.tree-and-values,executable.sin.command-line,executable.sin.input-output,executable.sin.exit-status,executable.sin.persistence,executable.sin.errors"},
    {"rewrite.core.test_itemstore_payload_replacement_contracts", test_itemstore_payload_replacement_contracts, "exclusive", 30000, "api.itemstore.tree-and-values"},
    {"rewrite.core.test_itemstore_path_creation_rolls_back_on_failure", test_itemstore_path_creation_rolls_back_on_failure, "exclusive", 30000, "api.itemstore.tree-and-values"},
    {"rewrite.core.test_itemstore_boot_rejects_overlong_root_name", test_itemstore_boot_rejects_overlong_root_name, "exclusive", 30000, "api.itemstore.persistence-load"},
    {"rewrite.core.test_itemstore_nested_depth_roundtrip", test_itemstore_nested_depth_roundtrip, "exclusive", 30000, "api.itemstore.tree-and-values"},
    {"rewrite.core.test_itemstore_item_name_contract_boundaries_roundtrip", test_itemstore_item_name_contract_boundaries_roundtrip, "exclusive", 30000, "api.itemstore.tree-and-values"},
    {"rewrite.core.test_itemstore_item_name_rejection_is_atomic", test_itemstore_item_name_rejection_is_atomic, "exclusive", 30000, "api.itemstore.tree-and-values"},
    {"rewrite.core.test_itemstore_item_name_relative_depth_contract", test_itemstore_item_name_relative_depth_contract, "exclusive", 30000, "api.runtime.itemref"},
    {"rewrite.core.test_save_itemstore_rejects_manually_invalid_item_names", test_save_itemstore_rejects_manually_invalid_item_names, "exclusive", 30000, "api.itemstore.persistence-save"},
    {"rewrite.core.test_itemstore_loads_generated_v1_wire_fixture", test_itemstore_loads_generated_v1_wire_fixture, "exclusive", 30000, "api.itemstore.persistence-load"},
    {"rewrite.core.test_load_itemstore_rejects_bad_headers", test_load_itemstore_rejects_bad_headers, "exclusive", 30000, "api.itemstore.persistence-load"},
    {"rewrite.core.test_load_itemstore_rejects_malformed_code_bytecode", test_load_itemstore_rejects_malformed_code_bytecode, "exclusive", 30000, "api.itemstore.persistence-format"},
    {"rewrite.core.test_load_itemstore_allows_malformed_code_when_strict_validation_disabled", test_load_itemstore_allows_malformed_code_when_strict_validation_disabled, "exclusive", 30000, "api.itemstore.persistence-format"},
    {"rewrite.core.test_load_itemstore_rejects_invalid_wire_tags", test_load_itemstore_rejects_invalid_wire_tags, "exclusive", 30000, "api.itemstore.persistence-format"},
    {"rewrite.core.test_load_itemstore_rejects_structural_corruption", test_load_itemstore_rejects_structural_corruption, "exclusive", 30000, "api.itemstore.persistence-load"},
    {"rewrite.core.test_load_itemstore_rejects_resource_limit_violations", test_load_itemstore_rejects_resource_limit_violations, "exclusive", 30000, "api.itemstore.persistence-load"},
    {"rewrite.core.test_save_itemstore_preserves_existing_file_on_failure", test_save_itemstore_preserves_existing_file_on_failure, "exclusive", 30000, "api.itemstore.persistence-save"},
    {"rewrite.core.test_save_itemsource_reports_write_and_close_failure", test_save_itemsource_reports_write_and_close_failure, "exclusive", 30000, "api.itemstore.source-persistence"},
    {"rewrite.core.test_itemsource_paths_are_validated_and_contained", test_itemsource_paths_are_validated_and_contained, "exclusive", 30000, "api.itemstore.source-persistence"},
    {"rewrite.core.test_itemstore_durability_modes", test_itemstore_durability_modes, "exclusive", 30000, "language.item-syntax.item-save,api.itemstore.persistence-save"},
    {"rewrite.core.test_itemstore_large_load_presizes_child_storage", test_itemstore_large_load_presizes_child_storage, "exclusive", 30000, "api.itemstore.persistence-load"},
};

int main(int argc, char **argv) {
  return tf_main(argc, argv, tests, sizeof tests / sizeof tests[0]);
}
