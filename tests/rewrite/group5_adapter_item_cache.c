#include "test_framework.h"

void test_find_item_cached_hit_and_negative_cache(void);
void test_find_item_cached_rejects_invalid_names_without_counters(void);
void test_find_item_cached_relative_invalid_name_preserves_counters(void);
void test_find_item_cached_invalidation_on_delete_and_reinsert(void);
void test_find_item_cached_distinguishes_roots(void);
void test_itemstore_cache_state_is_store_local(void);
void test_find_item_cached_root_lifecycle_invalidates_entries(void);
void test_item_hashtable_resize_preserves_entries_and_count(void);
void test_murmur3_32_alignment_and_vectors(void);
void test_itemstore_benchmarks(void);

static const TF_TestDescriptor tests[] = {
    {"rewrite.core.test_find_item_cached_hit_and_negative_cache",
     test_find_item_cached_hit_and_negative_cache, "", 30000,
     "api.itemstore.registry-and-lookup,baseline.legacy.unified.core.test_find_item_cached_hit_and_negative_cache"},
    {"rewrite.core.test_find_item_cached_rejects_invalid_names_without_counters",
     test_find_item_cached_rejects_invalid_names_without_counters, "", 30000,
     "api.itemstore.registry-and-lookup,baseline.legacy.unified.core.test_find_item_cached_rejects_invalid_names_without_counters"},
    {"rewrite.core.test_find_item_cached_relative_invalid_name_preserves_counters",
     test_find_item_cached_relative_invalid_name_preserves_counters, "", 30000,
     "api.itemstore.registry-and-lookup,baseline.legacy.unified.core.test_find_item_cached_relative_invalid_name_preserves_counters"},
    {"rewrite.core.test_find_item_cached_invalidation_on_delete_and_reinsert",
     test_find_item_cached_invalidation_on_delete_and_reinsert, "", 30000,
     "api.itemstore.registry-and-lookup,baseline.legacy.unified.core.test_find_item_cached_invalidation_on_delete_and_reinsert"},
    {"rewrite.core.test_find_item_cached_distinguishes_roots",
     test_find_item_cached_distinguishes_roots, "", 30000,
     "api.itemstore.registry-and-lookup,baseline.legacy.unified.core.test_find_item_cached_distinguishes_roots"},
    {"rewrite.core.test_itemstore_cache_state_is_store_local",
     test_itemstore_cache_state_is_store_local, "", 30000,
     "api.itemstore.registry-and-lookup,baseline.legacy.unified.core.test_itemstore_cache_state_is_store_local"},
    {"rewrite.core.test_find_item_cached_root_lifecycle_invalidates_entries",
     test_find_item_cached_root_lifecycle_invalidates_entries, "", 30000,
     "api.itemstore.registry-and-lookup,baseline.legacy.unified.core.test_find_item_cached_root_lifecycle_invalidates_entries"},
    {"rewrite.core.test_item_hashtable_resize_preserves_entries_and_count",
     test_item_hashtable_resize_preserves_entries_and_count, "", 30000,
     "api.itemstore.tree-and-values,baseline.legacy.unified.core.test_item_hashtable_resize_preserves_entries_and_count"},
    {"rewrite.core.test_murmur3_32_alignment_and_vectors",
     test_murmur3_32_alignment_and_vectors, "", 30000,
     "api.itemstore.registry-and-lookup,baseline.legacy.unified.core.test_murmur3_32_alignment_and_vectors"},
    {"rewrite.core.test_itemstore_benchmarks", test_itemstore_benchmarks,
     "benchmark,exclusive", 30000,
     "api.itemstore.registry-and-lookup,baseline.legacy.unified.core.test_itemstore_benchmarks"},
};

int main(int argc, char **argv) {
  return tf_main(argc, argv, tests, sizeof tests / sizeof tests[0]);
}
