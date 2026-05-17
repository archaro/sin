#include <stdbool.h>
#include <string.h>

#include "item.h"
#include "test_assert.h"

void test_find_item_cached_hit_and_negative_cache(void) {
  ITEM_t *root = make_root_item("root");
  ASSERT_NOT_NULL(root);

  VALUE_t value = {.type = VALUE_int, .i = 42};
  ITEM_t *inserted = insert_item(root, "alpha.beta", value);
  ASSERT_NOT_NULL(inserted);

  bool found = false;
  ITEM_t *cached = find_item_cached(root, "alpha.beta", &found);
  ASSERT_NOT_NULL(cached);
  ASSERT_TRUE(found);
  ASSERT_EQ_INT(VALUE_int, cached->value.type);
  ASSERT_EQ_INT(42, cached->value.i);

  found = true;
  ITEM_t *missing = find_item_cached(root, "alpha.gamma", &found);
  ASSERT_TRUE(missing == NULL);
  ASSERT_TRUE(!found);

  found = false;
  missing = find_item_cached(root, "alpha.gamma", &found);
  ASSERT_TRUE(missing == NULL);
  ASSERT_TRUE(!found);

  destroy_item(root);
}

void test_find_item_cached_invalidation_on_delete_and_reinsert(void) {
  ITEM_t *root = make_root_item("root");
  ASSERT_NOT_NULL(root);

  VALUE_t value = {.type = VALUE_int, .i = 7};
  ITEM_t *first = insert_item(root, "cache.target", value);
  ASSERT_NOT_NULL(first);

  ITEM_t *cached_before = find_item_cached(root, "cache.target", NULL);
  ASSERT_NOT_NULL(cached_before);
  ASSERT_TRUE(cached_before == first);

  delete_item(root, "cache.target");
  ITEM_t *after_delete = find_item_cached(root, "cache.target", NULL);
  ASSERT_TRUE(after_delete == NULL);

  uint64_t gen_after_delete = get_itemstore_generation();
  VALUE_t value2 = {.type = VALUE_int, .i = 99};
  ITEM_t *second = insert_item(root, "cache.target", value2);
  ASSERT_NOT_NULL(second);
  ASSERT_TRUE(get_itemstore_generation() > gen_after_delete);

  ITEM_t *after_reinsert = find_item_cached(root, "cache.target", NULL);
  ASSERT_NOT_NULL(after_reinsert);
  ASSERT_TRUE(after_reinsert == second || after_reinsert == first);
  ASSERT_EQ_INT(99, after_reinsert->value.i);

  destroy_item(root);
}
