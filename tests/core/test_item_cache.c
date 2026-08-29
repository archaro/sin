#include <stdbool.h>
#include "test_helpers.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "item.h"
#include "item_internal.h"
#define itemstore_default_context() (&itemstore_owner(root)->context)
#define get_itemstore_topology_revision() itemstore_topology_revision(itemstore_owner(root))
#define get_itemstore_payload_revision() itemstore_payload_revision(itemstore_owner(root))
#include "test_assert.h"

static bool fail_resize_calloc;
static bool resize_calloc_was_forced;

void *__real_calloc(size_t count, size_t size);

void *__wrap_calloc(size_t count, size_t size) {
  if (fail_resize_calloc && count == 33u && size == sizeof(void *)) {
    fail_resize_calloc = false;
    resize_calloc_was_forced = true;
    return NULL;
  }
  return __real_calloc(count, size);
}

static uint64_t itemstore_bench_now_ns(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

void test_itemstore_benchmarks(void) {
  ITEM_t *root = make_root_item("root");
  ASSERT_NOT_NULL(root);
  for (int i = 0; i < 64; i++) {
    char name[32];
    ASSERT_TRUE(snprintf(name, sizeof(name), "sibling_%02d", i) > 0);
    ASSERT_NOT_NULL(test_item_set_value(root, name, (VALUE_t){.type = VALUE_int, .i = i}));
  }
  char deep[ITEM_MAX_FULL_NAME_LENGTH + 1u];
  size_t deep_len = 0;
  for (size_t i = 0; i < ITEM_MAX_DEPTH; i++) {
    if (i > 0) deep[deep_len++] = '.';
    memset(deep + deep_len, 'd', ITEM_MAX_LAYER_NAME_LENGTH);
    deep_len += ITEM_MAX_LAYER_NAME_LENGTH;
  }
  deep[deep_len] = '\0';
  ASSERT_NOT_NULL(test_item_set_value(root, deep, (VALUE_t){.type = VALUE_int, .i = 7}));

  ITEMSTORE_CONTEXT_t *ctx = itemstore_default_context();
  ITEM_t *small_root = make_root_item("small");
  ASSERT_NOT_NULL(small_root);
  ASSERT_NOT_NULL(test_item_set_value(
      small_root, "only_a", (VALUE_t){.type = VALUE_int, .i = 1}));
  ASSERT_NOT_NULL(test_item_set_value(
      small_root, "only_b", (VALUE_t){.type = VALUE_int, .i = 2}));
  uint64_t hits = ctx->fetchitem_cache_hits, misses = ctx->fetchitem_cache_misses;
  ASSERT_NOT_NULL(find_item_cached(root, "sibling_00", NULL));
  ASSERT_NOT_NULL(find_item_cached(root, "sibling_00", NULL));
  ASSERT_TRUE(find_item_cached(root, "missing", NULL) == NULL);
  ASSERT_TRUE(find_item_cached(root, "missing", NULL) == NULL);
  ASSERT_EQ_INT(hits + 2, ctx->fetchitem_cache_hits);
  ASSERT_EQ_INT(misses + 2, ctx->fetchitem_cache_misses);
  ASSERT_NOT_NULL(find_item_cached(root, deep, NULL));

  ITEM_t *replacement = find_item_cached(root, "sibling_01", NULL);
  ASSERT_NOT_NULL(replacement);
  uint64_t misses_before_replace = ctx->fetchitem_cache_misses;
  ASSERT_TRUE(test_item_set_value(
      root, "sibling_01",
      (VALUE_t){.type = VALUE_int, .i = 101}) == replacement);
  ASSERT_EQ_INT(101, find_item_cached(root, "sibling_01", NULL)->value.i);
  ASSERT_EQ_INT(misses_before_replace, ctx->fetchitem_cache_misses);

  ASSERT_EQ_INT(0, item_child_at(root, 0)->value.i);
  ASSERT_EQ_INT(63, item_child_at(root, 63)->value.i);
  test_item_delete(root, "sibling_00");
  ASSERT_EQ_INT(64, item_child_count(root));
  ASSERT_EQ_INT(101, item_child_at(root, 0)->value.i);

  char path[4096];

  ASSERT_EQ_INT(0, test_temp_template(path, sizeof path, "sin-itemstore-bench"));
  int fd = mkstemp(path);
  ASSERT_TRUE(fd >= 0);
  ASSERT_EQ_INT(0, close(fd));
  ASSERT_TRUE(save_itemstore(path, root));
  ITEM_t *loaded = load_itemstore(path);
  ASSERT_NOT_NULL(loaded);
  ASSERT_EQ_INT(item_child_count(root), item_child_count(loaded));
  ASSERT_EQ_INT(7, find_item(loaded, deep)->value.i);
  ASSERT_EQ_INT(101, find_item(loaded, "sibling_01")->value.i);
  unlink(path);

  volatile int sink = 0;
  const int iterations = 5000;
  const char *shallow_name = "sibling_01";
  const char *large_name = "sibling_63";
  const char *missing_name = "never_present";
  uint64_t start = itemstore_bench_now_ns();
  for (int i = 0; i < iterations; i++) {
    sink ^= find_item_cached(root, shallow_name, NULL) != NULL;
  }
  uint64_t cached_positive_ns = itemstore_bench_now_ns() - start;
  start = itemstore_bench_now_ns();
  for (int i = 0; i < iterations; i++) {
    sink ^= find_item_cached(root, missing_name, NULL) == NULL;
  }
  uint64_t cached_negative_ns = itemstore_bench_now_ns() - start;
  start = itemstore_bench_now_ns();
  for (int i = 0; i < iterations; i++) {
    sink ^= find_item(root, shallow_name) != NULL;
  }
  uint64_t uncached_shallow_ns = itemstore_bench_now_ns() - start;
  start = itemstore_bench_now_ns();
  for (int i = 0; i < iterations; i++) {
    sink ^= find_item(root, deep) != NULL;
  }
  uint64_t uncached_deep_ns = itemstore_bench_now_ns() - start;
  start = itemstore_bench_now_ns();
  for (int i = 0; i < iterations; i++) {
    sink ^= find_item(small_root, "only_a") != NULL;
  }
  uint64_t small_sibling_ns = itemstore_bench_now_ns() - start;
  start = itemstore_bench_now_ns();
  for (int i = 0; i < iterations; i++) {
    sink ^= find_item(root, large_name) != NULL;
  }
  uint64_t large_sibling_ns = itemstore_bench_now_ns() - start;
  start = itemstore_bench_now_ns();
  for (int i = 0; i < iterations; i++) sink ^= find_item(loaded, deep) != NULL;
  uint64_t loaded_deep_ns = itemstore_bench_now_ns() - start;
  const int mutation_groups = 20;
  const int mutation_leaves_per_group = iterations / mutation_groups;
  char mutation_names[iterations][32];
  for (int i = 0; i < iterations; i++) {
    ASSERT_TRUE(snprintf(mutation_names[i], sizeof mutation_names[i],
                         "g%02d.i%03d",
                         i / mutation_leaves_per_group,
                         i % mutation_leaves_per_group) > 0);
  }
  uint64_t hits_before = ctx->fetchitem_cache_hits;
  uint64_t misses_before = ctx->fetchitem_cache_misses;
  ASSERT_NOT_NULL(find_item_cached(root, shallow_name, NULL));
  hits_before = ctx->fetchitem_cache_hits;
  misses_before = ctx->fetchitem_cache_misses;
  start = itemstore_bench_now_ns();
  for (int i = 0; i < iterations; i++) {
    (void)find_item_cached(root, shallow_name, NULL);
    (void)test_item_set_value(root, shallow_name, (VALUE_t){.type = VALUE_int, .i = i});
  }
  uint64_t replacement_ns = itemstore_bench_now_ns() - start;
  uint64_t replacement_hits = ctx->fetchitem_cache_hits - hits_before;
  uint64_t replacement_misses = ctx->fetchitem_cache_misses - misses_before;
  ASSERT_EQ_INT(4999, find_item(root, shallow_name)->value.i);
  ASSERT_EQ_INT(iterations, replacement_hits);
  ASSERT_EQ_INT(0, replacement_misses);
  ITEM_t *mut = make_root_item("mut");
  start = itemstore_bench_now_ns();
  for (int i = 0; i < iterations; i++) {
    (void)test_item_set_value(
        mut, mutation_names[i], (VALUE_t){.type = VALUE_int, .i = i});
  }
  uint64_t insertion_ns = itemstore_bench_now_ns() - start;
  ASSERT_EQ_INT(mutation_groups, item_child_count(mut));
  ASSERT_EQ_INT(mutation_leaves_per_group,
                item_child_count(find_item(mut, "g00")));
  ASSERT_EQ_INT(0, find_item(mut, mutation_names[0])->value.i);
  ASSERT_EQ_INT(iterations - 1,
                find_item(mut, mutation_names[iterations - 1])->value.i);
  start = itemstore_bench_now_ns();
  for (int i = 0; i < iterations; i++) {
    test_item_delete(mut, mutation_names[i]);
  }
  uint64_t deletion_ns = itemstore_bench_now_ns() - start;
  for (int i = 0; i < mutation_groups; i++) {
    char group_name[8];
    ASSERT_TRUE(snprintf(group_name, sizeof group_name, "g%02d", i) > 0);
    test_item_delete(mut, group_name);
  }
  ASSERT_EQ_INT(0, item_child_count(mut));

  for (size_t i = 0; i < 63u; i++) {
    char expected_name[32];
    ASSERT_TRUE(snprintf(expected_name, sizeof expected_name, "sibling_%02zu",
                         i + 1u) > 0);
    ASSERT_TRUE(strcmp(expected_name, item_child_at(root, i)->name) == 0);
  }
  char first_deep_layer[ITEM_MAX_LAYER_NAME_LENGTH + 1u];
  memcpy(first_deep_layer, deep, ITEM_MAX_LAYER_NAME_LENGTH);
  first_deep_layer[ITEM_MAX_LAYER_NAME_LENGTH] = '\0';
  ASSERT_TRUE(item_child_at(root, 63u) ==
              find_item(root, first_deep_layer));
  start = itemstore_bench_now_ns();
  for (int i = 0; i < iterations; i++) {
    ITEM_t *child =
        item_child_at(root, (size_t)(i % (int)item_child_count(root)));
    sink ^= (int)child->value.i;
  }
  uint64_t iteration_ns = itemstore_bench_now_ns() - start;
  destroy_item(mut);
  printf("[bench] itemstore total-ns iters=%d cached+%llu cached-%llu "
         "uncached-shallow=%llu uncached-deep=%llu sibling-small=%llu "
         "sibling-large=%llu loaded-deep=%llu replacement=%llu "
         "insertion=%llu deletion=%llu iteration=%llu "
         "cache-delta=%llu/%llu\n",
         iterations, (unsigned long long)cached_positive_ns,
         (unsigned long long)cached_negative_ns,
         (unsigned long long)uncached_shallow_ns,
         (unsigned long long)uncached_deep_ns,
         (unsigned long long)small_sibling_ns,
         (unsigned long long)large_sibling_ns,
         (unsigned long long)loaded_deep_ns,
         (unsigned long long)replacement_ns,
         (unsigned long long)insertion_ns,
         (unsigned long long)deletion_ns,
         (unsigned long long)iteration_ns,
         (unsigned long long)replacement_hits,
         (unsigned long long)replacement_misses);
  destroy_item(loaded);
  destroy_item(small_root);
  destroy_item(root);
}

void test_find_item_cached_hit_and_negative_cache(void) {
  ITEM_t *root = make_root_item("root");
  ASSERT_NOT_NULL(root);

  VALUE_t value = {.type = VALUE_int, .i = 42};
  ITEM_t *inserted = test_item_set_value(root, "alpha.beta", value);
  ASSERT_NOT_NULL(inserted);

  ITEMSTORE_CONTEXT_t *ctx = itemstore_default_context();
  uint64_t hits_before = ctx->fetchitem_cache_hits;
  uint64_t misses_before = ctx->fetchitem_cache_misses;
  bool found = false;
  ITEM_t *cached = find_item_cached(root, "alpha.beta", &found);
  ASSERT_NOT_NULL(cached);
  ASSERT_TRUE(found);
  ASSERT_EQ_INT(VALUE_int, cached->value.type);
  ASSERT_EQ_INT(42, cached->value.i);
  ASSERT_EQ_INT(misses_before + 1, ctx->fetchitem_cache_misses);

  cached = find_item_cached(root, "alpha.beta", &found);
  ASSERT_TRUE(cached == inserted);
  ASSERT_TRUE(found);
  ASSERT_EQ_INT(hits_before + 1, ctx->fetchitem_cache_hits);

  found = true;
  ITEM_t *missing = find_item_cached(root, "alpha.gamma", &found);
  ASSERT_TRUE(missing == NULL);
  ASSERT_TRUE(!found);
  ASSERT_EQ_INT(misses_before + 2, ctx->fetchitem_cache_misses);

  found = false;
  missing = find_item_cached(root, "alpha.gamma", &found);
  ASSERT_TRUE(missing == NULL);
  ASSERT_TRUE(!found);
  ASSERT_EQ_INT(hits_before + 2, ctx->fetchitem_cache_hits);

  destroy_item(root);
}

void test_find_item_cached_rejects_invalid_names_without_counters(void) {
  ITEM_t *root = make_root_item("root");
  ASSERT_NOT_NULL(root);

  ITEMSTORE_CONTEXT_t *ctx = itemstore_default_context();
  ASSERT_NOT_NULL(test_item_set_value(root, "valid",
                              (VALUE_t){.type = VALUE_int, .i = 3}));
  bool found = false;
  ASSERT_NOT_NULL(find_item_cached(root, "valid", &found));
  ASSERT_TRUE(found);
  uint64_t hits_before = ctx->fetchitem_cache_hits;
  uint64_t misses_before = ctx->fetchitem_cache_misses;
  uint64_t topology_before = ctx->topology_revision;
  uint64_t payload_before = ctx->payload_revision;

  char long_name[34];
  memset(long_name, 'x', sizeof(long_name) - 1);
  long_name[sizeof(long_name) - 1] = '\0';
  char long_path[9 * (ITEM_MAX_LAYER_NAME_LENGTH + 1u)];
  size_t long_path_len = 0;
  for (size_t i = 0; i < 9; i++) {
    memset(long_path + long_path_len, 'y', ITEM_MAX_LAYER_NAME_LENGTH);
    long_path_len += ITEM_MAX_LAYER_NAME_LENGTH;
    if (i < 8) long_path[long_path_len++] = '.';
  }
  long_path[long_path_len] = '\0';
  const char *invalid_names[] = {NULL, "", "alpha.", ".alpha",
                                 "alpha..beta", long_name, long_path};
  for (size_t i = 0; i < sizeof(invalid_names) / sizeof(invalid_names[0]);
       i++) {
    found = true;
    ASSERT_TRUE(find_item_cached(root, invalid_names[i], &found) == NULL);
    ASSERT_TRUE(!found);
  }

  ASSERT_EQ_INT(hits_before, ctx->fetchitem_cache_hits);
  ASSERT_EQ_INT(misses_before, ctx->fetchitem_cache_misses);
  ASSERT_EQ_INT(topology_before, ctx->topology_revision);
  ASSERT_EQ_INT(payload_before, ctx->payload_revision);

  found = false;
  ASSERT_NOT_NULL(find_item_cached(root, "valid", &found));
  ASSERT_TRUE(found);
  ASSERT_EQ_INT(hits_before + 1, ctx->fetchitem_cache_hits);
  ASSERT_EQ_INT(misses_before, ctx->fetchitem_cache_misses);

  destroy_item(root);
}

void test_find_item_cached_relative_invalid_name_preserves_counters(void) {
  ITEM_t *root = make_root_item("root");
  ASSERT_NOT_NULL(root);
  ITEM_t *parent = test_item_set_value(root, "a.b.c.d.e.f.g",
                               (VALUE_t){.type = VALUE_nil, .i = 0});
  ASSERT_NOT_NULL(parent);
  ASSERT_NOT_NULL(test_item_set_value(parent, "h",
                              (VALUE_t){.type = VALUE_int, .i = 8}));
  ASSERT_NOT_NULL(find_item_cached(parent, "h", NULL));

  ITEMSTORE_CONTEXT_t *ctx = itemstore_default_context();
  uint64_t hits = ctx->fetchitem_cache_hits;
  uint64_t misses = ctx->fetchitem_cache_misses;
  bool found = true;
  ASSERT_TRUE(find_item_cached(parent, "i.j", &found) == NULL);
  ASSERT_TRUE(!found);
  ASSERT_EQ_INT(hits, ctx->fetchitem_cache_hits);
  ASSERT_EQ_INT(misses, ctx->fetchitem_cache_misses);

  destroy_item(root);
}

void test_find_item_cached_invalidation_on_delete_and_reinsert(void) {
  ITEM_t *root = make_root_item("root");
  ASSERT_NOT_NULL(root);

  bool found = true;
  ITEM_t *missing = find_item_cached(root, "cache.created", &found);
  ASSERT_TRUE(missing == NULL);
  ASSERT_TRUE(!found);

  ITEM_t *created = test_item_set_value(
      root, "cache.created", (VALUE_t){.type = VALUE_int, .i = 55});
  ASSERT_NOT_NULL(created);
  ITEM_t *after_create = find_item_cached(root, "cache.created", &found);
  ASSERT_TRUE(found);
  ASSERT_TRUE(after_create == created);
  ASSERT_EQ_INT(55, after_create->value.i);

  VALUE_t value = {.type = VALUE_int, .i = 7};
  ITEM_t *first = test_item_set_value(root, "cache.target", value);
  ASSERT_NOT_NULL(first);

  ITEM_t *cached_before = find_item_cached(root, "cache.target", NULL);
  ASSERT_NOT_NULL(cached_before);
  ASSERT_TRUE(cached_before == first);

  ITEMSTORE_t *store = itemstore_owner(root);
  uint64_t topology_before_delete = itemstore_topology_revision(store);
  uint64_t payload_before_delete = itemstore_payload_revision(store);
  test_item_delete(root, "cache.target");
  ITEM_t *after_delete = find_item_cached(root, "cache.target", NULL);
  ASSERT_TRUE(after_delete == NULL);

  ASSERT_EQ_INT(topology_before_delete + 1,
               itemstore_topology_revision(store));
  ASSERT_EQ_INT(payload_before_delete, itemstore_payload_revision(store));
  uint64_t topology_before_reinsert = itemstore_topology_revision(store);
  uint64_t payload_before_reinsert = itemstore_payload_revision(store);
  VALUE_t value2 = {.type = VALUE_int, .i = 99};
  ITEM_t *second = test_item_set_value(root, "cache.target", value2);
  ASSERT_NOT_NULL(second);
  ASSERT_EQ_INT(topology_before_reinsert + 1,
               itemstore_topology_revision(store));
  ASSERT_EQ_INT(payload_before_reinsert, itemstore_payload_revision(store));

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

  ITEM_t *first_item = test_item_set_value(
      first_root, "shared.name", (VALUE_t){.type = VALUE_int, .i = 11});
  ITEM_t *second_item = test_item_set_value(
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

void test_itemstore_cache_state_is_store_local(void) {
  ITEMSTORE_t *first = itemstore_create("first");
  ITEMSTORE_t *second = itemstore_create("second");
  ASSERT_NOT_NULL(first);
  ASSERT_NOT_NULL(second);
  ITEM_t *first_root = itemstore_root(first);
  ITEM_t *second_root = itemstore_root(second);
  ASSERT_NOT_NULL(test_item_set_value(first_root, "shared", (VALUE_t){VALUE_int, {.i = 1}}));
  ASSERT_NOT_NULL(test_item_set_value(second_root, "shared", (VALUE_t){VALUE_int, {.i = 2}}));
  ASSERT_NOT_NULL(find_item_cached(first_root, "shared", NULL));
  ASSERT_NOT_NULL(find_item_cached(second_root, "shared", NULL));
  uint64_t first_payload = itemstore_payload_revision(first);
  uint64_t first_topology = itemstore_topology_revision(first);
  uint64_t second_topology = itemstore_topology_revision(second);
  uint64_t second_payload = itemstore_payload_revision(second);
  uint64_t second_hits = itemstore_cache_hits(second);
  uint64_t second_misses = itemstore_cache_misses(second);
  ASSERT_NOT_NULL(find_item_cached(second_root, "shared", NULL));
  ASSERT_EQ_INT(second_hits + 1, itemstore_cache_hits(second));
  ASSERT_EQ_INT(second_misses, itemstore_cache_misses(second));
  uint64_t first_hits = itemstore_cache_hits(first);
  test_item_set_value(first_root, "shared", (VALUE_t){VALUE_int, {.i = 3}});
  ASSERT_EQ_INT(first_topology, itemstore_topology_revision(first));
  ASSERT_EQ_INT(first_payload + 1, itemstore_payload_revision(first));
  ASSERT_EQ_INT(second_topology, itemstore_topology_revision(second));
  ASSERT_EQ_INT(second_payload, itemstore_payload_revision(second));
  ASSERT_EQ_INT(3, item_value(find_item_cached(first_root, "shared", NULL))->i);
  ASSERT_EQ_INT(first_hits + 1, itemstore_cache_hits(first));
  ASSERT_EQ_INT(2, item_value(find_item_cached(second_root, "shared", NULL))->i);
  itemstore_destroy(first);
  ASSERT_EQ_INT(second_topology, itemstore_topology_revision(second));
  ASSERT_EQ_INT(second_payload, itemstore_payload_revision(second));
  ASSERT_EQ_INT(2, item_value(find_item_cached(second_root, "shared", NULL))->i);
  itemstore_destroy(second);
}

void test_find_item_cached_root_lifecycle_invalidates_entries(void) {
  ITEM_t *root = make_root_item("root");
  ASSERT_NOT_NULL(root);
  ITEM_t *old_item = test_item_set_value(
      root, "lifecycle.item", (VALUE_t){.type = VALUE_int, .i = 1});
  ASSERT_NOT_NULL(old_item);
  ASSERT_TRUE(find_item_cached(root, "lifecycle.item", NULL) == old_item);

  destroy_item(root);

  ITEM_t *replacement = make_root_item("root");
  ASSERT_NOT_NULL(replacement);
  ITEM_t *new_item = test_item_set_value(
      replacement, "lifecycle.item", (VALUE_t){.type = VALUE_int, .i = 2});
  ASSERT_NOT_NULL(new_item);

  ASSERT_TRUE(find_item_cached(replacement, "lifecycle.item", NULL)
              == new_item);
  ITEMSTORE_t *replacement_store = itemstore_owner(replacement);
  ASSERT_EQ_INT(0, itemstore_cache_hits(replacement_store));
  ASSERT_EQ_INT(1, itemstore_cache_misses(replacement_store));
  ASSERT_EQ_INT(2, new_item->value.i);

  destroy_item(replacement);
}

void test_item_hashtable_resize_preserves_entries_and_count(void) {
  ITEM_t *failure_root = make_root_item("failure_root");
  ASSERT_NOT_NULL(failure_root);
  for (int i = 0; i < 12; i++) {
    char name[32];
    ASSERT_TRUE(snprintf(name, sizeof(name), "child_%02d", i) > 0);
    ASSERT_NOT_NULL(test_item_set_value(
        failure_root, name, (VALUE_t){.type = VALUE_int, .i = i}));
  }
  ASSERT_NOT_NULL(find_item_cached(failure_root, "child_00", NULL));
  ASSERT_TRUE(find_item_cached(failure_root, "absent", NULL) == NULL);
  size_t count_before = item_child_count(failure_root);
  uint32_t buckets_before = item_children_bucket_count(failure_root->children);
  size_t capacity_before =
      item_children_ordered_capacity(failure_root->children);
  VALUE_t probe_value = {.type = VALUE_int, .i = 10};
  resize_calloc_was_forced = false;
  fail_resize_calloc = true;
  ITEM_t *probe = test_item_set_value(
      failure_root, "allocation_probe", probe_value);
  /* The targeted calloc(33, sizeof(void *)) is resize_children's table
     allocation. Publication happens before this best-effort resize, so the
     new entry remains visible while the old table/count state is preserved
     for all pre-existing entries. */
  ASSERT_NOT_NULL(probe);
  ASSERT_TRUE(resize_calloc_was_forced);
  ASSERT_TRUE(!fail_resize_calloc);
  ASSERT_EQ_INT(count_before + 1u, item_child_count(failure_root));
  ASSERT_EQ_INT(buckets_before,
                item_children_bucket_count(failure_root->children));
  ASSERT_EQ_INT(capacity_before,
                item_children_ordered_capacity(failure_root->children));
  ASSERT_NOT_NULL(find_item_cached(failure_root, "child_00", NULL));
  ASSERT_TRUE(find_item_cached(failure_root, "absent", NULL) == NULL);
  ASSERT_TRUE(find_item_cached(failure_root, "allocation_probe", NULL) == probe);
  ASSERT_NOT_NULL(test_item_set_value(
      failure_root, "resize_probe", (VALUE_t){.type = VALUE_int, .i = 12}));
  ASSERT_EQ_INT(33, item_children_bucket_count(failure_root->children));
  fail_resize_calloc = false;
  destroy_item(failure_root);

  ITEM_t *root = make_root_item("root");
  ASSERT_NOT_NULL(root);
  ASSERT_EQ_INT(16, item_children_bucket_count(root->children));
  ASSERT_EQ_INT(10, item_children_ordered_capacity(root->children));

  enum { SIBLING_COUNT = 40, COLLISION_COUNT = 2, DELETE_COUNT = 10 };
  ITEM_t *first_child = test_item_set_value(
      root, "aa", (VALUE_t){.type = VALUE_int, .i = 100});
  ASSERT_NOT_NULL(first_child);
  ASSERT_EQ_INT(16, item_children_bucket_count(first_child->children));
  ASSERT_EQ_INT(10, item_children_ordered_capacity(first_child->children));
  ASSERT_NOT_NULL(test_item_set_value(root, "qc",
                              (VALUE_t){.type = VALUE_int, .i = 101}));
  for (int i = 0; i < SIBLING_COUNT; i++) {
    char name[32];
    ASSERT_TRUE(snprintf(name, sizeof(name), "sibling_%02d", i) > 0);
    ITEM_t *item = test_item_set_value(root, name,
                               (VALUE_t){.type = VALUE_int, .i = i});
    ASSERT_NOT_NULL(item);
  }

  ASSERT_TRUE(item_children_bucket_count(root->children) > 16);
  ASSERT_EQ_INT(SIBLING_COUNT + COLLISION_COUNT, item_child_count(root));
  ASSERT_TRUE(item_children_ordered_capacity(root->children) >=
              item_child_count(root));
  ASSERT_EQ_INT(80, item_children_ordered_capacity(root->children));
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
  ASSERT_TRUE(item_child_at(root, 0) == collision_a);
  ASSERT_TRUE(item_child_at(root, 1) == collision_b);
  for (int i = 0; i < SIBLING_COUNT; i++) {
    ITEM_t *item = item_child_at(root, (size_t)i + COLLISION_COUNT);
    ASSERT_NOT_NULL(item);
    ASSERT_EQ_INT(i, item->value.i);
  }

  for (int i = 0; i < DELETE_COUNT; i++) {
    char name[32];
    ASSERT_TRUE(snprintf(name, sizeof(name), "sibling_%02d", i) > 0);
    test_item_delete(root, name);
    ASSERT_TRUE(find_item(root, name) == NULL);
  }

  ASSERT_EQ_INT(SIBLING_COUNT + COLLISION_COUNT - DELETE_COUNT,
                item_child_count(root));
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

void test_murmur3_32_alignment_and_vectors(void) {
  static const uint8_t empty[] = {0};
  static const uint8_t abc[] = {'a', 'b', 'c'};
  static const uint8_t abcd[] = {'a', 'b', 'c', 'd'};
  static const uint8_t hello[] = {'h', 'e', 'l', 'l', 'o'};
  _Alignas(uint32_t) static const uint8_t blocks[] =
      {0, 1, 2, 3, 4, 5, 6, 7};
  static const uint8_t seeded[] = {'a', 'b', 'c', 'd', 'e', 'f', 'g'};
  ASSERT_EQ_INT(0, murmur3_32((const char *)empty, 0, 0));
  ASSERT_EQ_INT(0xb3dd93fa, murmur3_32((const char *)abc, 3, 0));
  ASSERT_EQ_INT(0x43ed676a, murmur3_32((const char *)abcd, 4, 0));
  ASSERT_EQ_INT(0x248bfa47, murmur3_32((const char *)hello, 5, 0));
  ASSERT_EQ_INT(0xd161d673, murmur3_32((const char *)blocks, 8, 0));
  ASSERT_EQ_INT(0x42576ab7, murmur3_32((const char *)seeded, 7, 1234));

  _Alignas(uint32_t) uint8_t storage[sizeof blocks + 1u];
  memcpy(storage + 1u, blocks, sizeof blocks);
  ASSERT_EQ_INT(murmur3_32((const char *)blocks, sizeof blocks, 0),
                murmur3_32((const char *)(storage + 1u), sizeof blocks, 0));
}
