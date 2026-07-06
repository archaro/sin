#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "item.h"
#include "item_internal.h"
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

  bool found = true;
  ITEM_t *missing = find_item_cached(root, "cache.created", &found);
  ASSERT_TRUE(missing == NULL);
  ASSERT_TRUE(!found);

  ITEM_t *created = insert_item(
      root, "cache.created", (VALUE_t){.type = VALUE_int, .i = 55});
  ASSERT_NOT_NULL(created);
  ITEM_t *after_create = find_item_cached(root, "cache.created", &found);
  ASSERT_TRUE(found);
  ASSERT_TRUE(after_create == created);
  ASSERT_EQ_INT(55, after_create->value.i);

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
  ASSERT_TRUE(after_reinsert == second);
  ASSERT_EQ_INT(99, after_reinsert->value.i);

  destroy_item(root);
}

void test_find_item_cached_distinguishes_roots(void) {
  ITEM_t *first_root = make_root_item("first_root");
  ITEM_t *second_root = make_root_item("second_root");
  ASSERT_NOT_NULL(first_root);
  ASSERT_NOT_NULL(second_root);

  ITEM_t *first_item = insert_item(
      first_root, "shared.name", (VALUE_t){.type = VALUE_int, .i = 11});
  ITEM_t *second_item = insert_item(
      second_root, "shared.name", (VALUE_t){.type = VALUE_int, .i = 22});
  ASSERT_NOT_NULL(first_item);
  ASSERT_NOT_NULL(second_item);

  ITEM_t *cached = find_item_cached(first_root, "shared.name", NULL);
  ASSERT_TRUE(cached == first_item);
  ASSERT_EQ_INT(11, cached->value.i);

  cached = find_item_cached(second_root, "shared.name", NULL);
  ASSERT_TRUE(cached == second_item);
  ASSERT_EQ_INT(22, cached->value.i);

  cached = find_item_cached(first_root, "shared.name", NULL);
  ASSERT_TRUE(cached == first_item);
  ASSERT_EQ_INT(11, cached->value.i);

  destroy_item(first_root);
  destroy_item(second_root);
}

void test_item_hashtable_resize_preserves_entries_and_count(void) {
  ITEM_t *root = make_root_item("root");
  ASSERT_NOT_NULL(root);
  ASSERT_EQ_INT(16, root->children->size);
  ASSERT_EQ_INT(ITEM_ARRAY_INIT_CAPACITY, root->ordered_capacity);

  enum { SIBLING_COUNT = 40, COLLISION_COUNT = 2, DELETE_COUNT = 10 };
  ITEM_t *first_child = insert_item(
      root, "aa", (VALUE_t){.type = VALUE_int, .i = 100});
  ASSERT_NOT_NULL(first_child);
  ASSERT_EQ_INT(16, first_child->children->size);
  ASSERT_EQ_INT(ITEM_ARRAY_INIT_CAPACITY, first_child->ordered_capacity);
  ASSERT_NOT_NULL(insert_item(root, "qc",
                              (VALUE_t){.type = VALUE_int, .i = 101}));
  for (int i = 0; i < SIBLING_COUNT; i++) {
    char name[32];
    ASSERT_TRUE(snprintf(name, sizeof(name), "sibling_%02d", i) > 0);
    ITEM_t *item = insert_item(root, name,
                               (VALUE_t){.type = VALUE_int, .i = i});
    ASSERT_NOT_NULL(item);
  }

  ASSERT_TRUE(root->children->size > 16);
  ASSERT_EQ_INT(SIBLING_COUNT + COLLISION_COUNT,
                root->children->entry_count);
  ASSERT_EQ_INT(SIBLING_COUNT + COLLISION_COUNT, root->ordered_size);
  ASSERT_TRUE(root->ordered_capacity >= root->ordered_size);
  ASSERT_EQ_INT(80, root->ordered_capacity);
  ITEM_t *collision_a = find_item(root, "aa");
  ITEM_t *collision_b = find_item(root, "qc");
  ASSERT_NOT_NULL(collision_a);
  ASSERT_NOT_NULL(collision_b);
  ASSERT_EQ_INT(100, collision_a->value.i);
  ASSERT_EQ_INT(101, collision_b->value.i);
  for (int i = 0; i < SIBLING_COUNT; i++) {
    char name[32];
    ASSERT_TRUE(snprintf(name, sizeof(name), "sibling_%02d", i) > 0);
    ITEM_t *item = find_item(root, name);
    ASSERT_NOT_NULL(item);
    ASSERT_EQ_INT(i, item->value.i);
  }
  ASSERT_TRUE(find_item_by_index(root, 0) == collision_a);
  ASSERT_TRUE(find_item_by_index(root, 1) == collision_b);
  for (int i = 0; i < SIBLING_COUNT; i++) {
    ITEM_t *item = find_item_by_index(root, (size_t)i + COLLISION_COUNT);
    ASSERT_NOT_NULL(item);
    ASSERT_EQ_INT(i, item->value.i);
  }

  for (int i = 0; i < DELETE_COUNT; i++) {
    char name[32];
    ASSERT_TRUE(snprintf(name, sizeof(name), "sibling_%02d", i) > 0);
    delete_item(root, name);
    ASSERT_TRUE(find_item(root, name) == NULL);
  }

  ASSERT_EQ_INT(SIBLING_COUNT + COLLISION_COUNT - DELETE_COUNT,
                root->children->entry_count);
  ASSERT_EQ_INT(SIBLING_COUNT + COLLISION_COUNT - DELETE_COUNT,
                root->ordered_size);
  collision_a = find_item(root, "aa");
  collision_b = find_item(root, "qc");
  ASSERT_NOT_NULL(collision_a);
  ASSERT_NOT_NULL(collision_b);
  ASSERT_EQ_INT(100, collision_a->value.i);
  ASSERT_EQ_INT(101, collision_b->value.i);
  for (int i = DELETE_COUNT; i < SIBLING_COUNT; i++) {
    char name[32];
    ASSERT_TRUE(snprintf(name, sizeof(name), "sibling_%02d", i) > 0);
    ITEM_t *item = find_item(root, name);
    ASSERT_NOT_NULL(item);
    ASSERT_EQ_INT(i, item->value.i);
  }

  destroy_item(root);
}
