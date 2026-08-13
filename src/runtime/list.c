#include "list.h"

#include "list_internal.h"

#include <stdint.h>
#include <stdlib.h>

#include "memory.h"

#define LIST_BRANCH 32u

struct SIN_LIST_NODE {
  size_t refs;
  size_t count; /* Number of values below this node. */
  size_t depth;
  unsigned slots;
  unsigned height; /* Zero is a value leaf. */
  bool leaf;
  union {
    VALUE_t values[LIST_BRANCH];
    struct SIN_LIST_NODE *children[LIST_BRANCH];
  } data;
};

static SIN_LIST_TRAVERSAL_STATS_t traversal_stats;

static void traversal_stat_inc(size_t *counter) {
  if (*counter != SIZE_MAX) ++*counter;
}

static void traversal_stat_add(size_t *counter, size_t amount) {
  if (amount > SIZE_MAX - *counter) *counter = SIZE_MAX;
  else *counter += amount;
}

void sin_list_test_reset_traversal_stats(void) {
  traversal_stats = (SIN_LIST_TRAVERSAL_STATS_t){0};
}

SIN_LIST_TRAVERSAL_STATS_t sin_list_test_traversal_stats(void) {
  return traversal_stats;
}

struct SIN_LIST {
  size_t refs;
  size_t count;
  size_t depth;
  size_t tail_count;
  unsigned height;
  SIN_LIST_NODE *root; /* Complete 32-value blocks, excluding tail. */
  SIN_LIST_NODE *tail; /* One to 32 values. */
};

static size_t value_depth(const VALUE_t *value) {
  if (!value || value->type != VALUE_list || !value->list) return 1u;
  if (sin_list_depth(value->list) == SIZE_MAX) return SIZE_MAX;
  return sin_list_depth(value->list) + 1u;
}

static bool node_retain(SIN_LIST_NODE *node) {
  if (!node || node->refs == 0 || node->refs == SIZE_MAX) return false;
  node->refs++;
  return true;
}

static void node_release(SIN_LIST_NODE *node) {
  if (!node || node->refs == 0 || --node->refs != 0) return;
  if (node->leaf) {
    for (size_t i = 0; i < node->slots; ++i) value_free(&node->data.values[i]);
  } else {
    for (size_t i = 0; i < node->slots; ++i)
      node_release(node->data.children[i]);
  }
  free(node);
}

static size_t max_tree_count(unsigned height) {
  size_t result = LIST_BRANCH;
  for (unsigned level = 0; level < height; ++level) {
    if (result > SIZE_MAX / LIST_BRANCH) return SIZE_MAX;
    result *= LIST_BRANCH;
  }
  return result;
}

static size_t max_depth(size_t left, size_t right) {
  return left > right ? left : right;
}

/* The node takes ownership of elements only after allocation succeeds. */
static SIN_LIST_NODE *leaf_from_owned(VALUE_t *elements, size_t count) {
  SIN_LIST_NODE *leaf;
  if (!elements || count == 0 || count > LIST_BRANCH) return NULL;
  leaf = alloc_calloc(1, sizeof(*leaf));
  if (!leaf) return NULL;
  leaf->refs = 1;
  leaf->leaf = true;
  leaf->slots = (unsigned)count;
  leaf->height = 0;
  leaf->count = count;
  leaf->depth = 1;
  for (size_t i = 0; i < count; ++i) {
    leaf->data.values[i] = elements[i];
    elements[i] = VALUE_NIL;
    leaf->depth = max_depth(leaf->depth, value_depth(&leaf->data.values[i]));
  }
  if (leaf->depth > SIN_LIST_MAX_DEPTH) {
    node_release(leaf);
    return NULL;
  }
  return leaf;
}

static SIN_LIST_NODE *leaf_clone_append(const SIN_LIST_NODE *old,
                                        const VALUE_t *value) {
  SIN_LIST_NODE *leaf;
  size_t count;
  if (!old || !old->leaf || old->slots >= LIST_BRANCH || !value) return NULL;
  count = old->slots + 1u;
  leaf = alloc_calloc(1, sizeof(*leaf));
  if (!leaf) return NULL;
  leaf->refs = 1;
  leaf->leaf = true;
  leaf->slots = (unsigned)count;
  leaf->count = count;
  leaf->depth = 1;
  for (size_t i = 0; i < old->slots; ++i) {
    if (!value_clone_fallible(&old->data.values[i], &leaf->data.values[i])) {
      node_release(leaf);
      return NULL;
    }
    leaf->depth = max_depth(leaf->depth, value_depth(&leaf->data.values[i]));
  }
  if (!value_clone_fallible(value, &leaf->data.values[old->slots])) {
    node_release(leaf);
    return NULL;
  }
  leaf->depth = max_depth(leaf->depth,
                          value_depth(&leaf->data.values[old->slots]));
  if (leaf->depth > SIN_LIST_MAX_DEPTH) {
    node_release(leaf);
    return NULL;
  }
  return leaf;
}

/* Clone an existing tail and a borrowed source range into one leaf. */
static SIN_LIST_NODE *leaf_clone_append_range(const SIN_LIST_NODE *old,
                                              const SIN_LIST_t *source,
                                              size_t start, size_t count) {
  SIN_LIST_NODE *leaf;
  size_t old_count = old ? old->slots : 0;
  if (count == 0 || old_count + count > LIST_BRANCH ||
      (old && !old->leaf) || !source)
    return NULL;
  leaf = alloc_calloc(1, sizeof(*leaf));
  if (!leaf) return NULL;
  leaf->refs = 1;
  leaf->leaf = true;
  leaf->slots = (unsigned)(old_count + count);
  leaf->count = old_count + count;
  leaf->depth = 1;
  for (size_t i = 0; i < old_count; ++i) {
    if (!value_clone_fallible(&old->data.values[i], &leaf->data.values[i])) {
      node_release(leaf);
      return NULL;
    }
    leaf->depth = max_depth(leaf->depth, value_depth(&leaf->data.values[i]));
  }
  for (size_t i = 0; i < count; ++i) {
    const VALUE_t *value = sin_list_get(source, start + i);
    if (!value || !value_clone_fallible(value, &leaf->data.values[old_count + i])) {
      node_release(leaf);
      return NULL;
    }
    leaf->depth = max_depth(leaf->depth,
                            value_depth(&leaf->data.values[old_count + i]));
  }
  if (leaf->depth > SIN_LIST_MAX_DEPTH) {
    node_release(leaf);
    return NULL;
  }
  return leaf;
}

static SIN_LIST_NODE *leaf_clone_single(const VALUE_t *value) {
  SIN_LIST_NODE *leaf;
  if (!value) return NULL;
  leaf = alloc_calloc(1, sizeof(*leaf));
  if (!leaf) return NULL;
  leaf->refs = 1;
  leaf->leaf = true;
  leaf->slots = 1;
  leaf->count = 1;
  leaf->depth = value_depth(value);
  if (!value_clone_fallible(value, &leaf->data.values[0]) ||
      leaf->depth > SIN_LIST_MAX_DEPTH) {
    node_release(leaf);
    return NULL;
  }
  return leaf;
}

static SIN_LIST_NODE *leaf_clone_set(const SIN_LIST_NODE *old, size_t index,
                                     const VALUE_t *value) {
  SIN_LIST_NODE *leaf;
  if (!old || !old->leaf || index >= old->slots || !value) return NULL;
  leaf = alloc_calloc(1, sizeof(*leaf));
  if (!leaf) return NULL;
  leaf->refs = 1;
  leaf->leaf = true;
  leaf->slots = old->slots;
  leaf->count = old->count;
  leaf->depth = 1;
  for (size_t i = 0; i < old->slots; ++i) {
    const VALUE_t *source = i == index ? value : &old->data.values[i];
    if (!value_clone_fallible(source, &leaf->data.values[i])) {
      node_release(leaf);
      return NULL;
    }
    leaf->depth = max_depth(leaf->depth, value_depth(&leaf->data.values[i]));
  }
  if (leaf->depth > SIN_LIST_MAX_DEPTH) {
    node_release(leaf);
    return NULL;
  }
  return leaf;
}

/* On success, all children are moved into the new branch. */
static SIN_LIST_NODE *branch_from_owned(SIN_LIST_NODE **children,
                                        unsigned slots, unsigned height) {
  SIN_LIST_NODE *branch;
  size_t count = 0;
  size_t depth = 1;
  if (!children || slots == 0 || slots > LIST_BRANCH || height == 0) return NULL;
  branch = alloc_calloc(1, sizeof(*branch));
  if (!branch) return NULL;
  for (unsigned i = 0; i < slots; ++i) {
    if (!children[i] || children[i]->count > SIZE_MAX - count) {
      free(branch);
      return NULL;
    }
    count += children[i]->count;
    depth = max_depth(depth, children[i]->depth);
  }
  branch->refs = 1;
  branch->leaf = false;
  branch->slots = slots;
  branch->height = height;
  branch->count = count;
  branch->depth = depth;
  for (unsigned i = 0; i < slots; ++i) {
    branch->data.children[i] = children[i];
    children[i] = NULL;
  }
  return branch;
}

static SIN_LIST_NODE *new_path_owned(unsigned height, SIN_LIST_NODE *leaf) {
  SIN_LIST_NODE *child;
  SIN_LIST_NODE *branch;
  SIN_LIST_NODE *children[1];
  if (!leaf) return NULL;
  if (height == 0) return leaf;
  child = new_path_owned(height - 1u, leaf);
  if (!child) return NULL;
  children[0] = child;
  branch = branch_from_owned(children, 1, height);
  if (!branch) node_release(child);
  return branch;
}

/* Build a complete tree from full leaves; consumes the pointer array. */
static SIN_LIST_NODE *build_tree(SIN_LIST_NODE **nodes, size_t node_count) {
  size_t count = node_count;
  if (!nodes || count == 0) {
    free(nodes);
    return NULL;
  }
  unsigned height = 0;
  while (count > 1) {
    size_t output_count = (count + LIST_BRANCH - 1u) / LIST_BRANCH;
    SIN_LIST_NODE **next = alloc_calloc(output_count, sizeof(*next));
    if (!next) {
      for (size_t i = 0; i < count; ++i) node_release(nodes[i]);
      free(nodes);
      return NULL;
    }
    size_t produced = 0;
    for (size_t start = 0; start < count; start += LIST_BRANCH) {
      size_t slots = count - start;
      if (slots > LIST_BRANCH) slots = LIST_BRANCH;
      next[produced] = branch_from_owned(nodes + start, (unsigned)slots,
                                         height + 1u);
      if (!next[produced]) {
        for (size_t i = 0; i < produced; ++i) node_release(next[i]);
        for (size_t i = start; i < count; ++i) node_release(nodes[i]);
        free(next);
        free(nodes);
        return NULL;
      }
      ++produced;
    }
    free(nodes);
    nodes = next;
    count = produced;
    ++height;
  }
  SIN_LIST_NODE *root = nodes[0];
  free(nodes);
  return root;
}

bool sin_list_decode_allocation_bytes(size_t count, size_t *bytes) {
  size_t total = sizeof(SIN_LIST_t);
  size_t tail_count;
  size_t full_leaves;
  size_t nodes;
  size_t part;
  if (count > SIN_LIST_MAX_ELEMENTS) return false;
  if (count == 0) {
    if (bytes) *bytes = total;
    return true;
  }
  tail_count = count <= LIST_BRANCH ? count : ((count - 1u) % LIST_BRANCH) + 1u;
  full_leaves = (count - tail_count) / LIST_BRANCH;
  if (full_leaves != 0) {
    if (alloc_mul_overflow(full_leaves, sizeof(SIN_LIST_NODE *), &part) ||
        alloc_add_overflow(total, part, &total) ||
        alloc_mul_overflow(full_leaves, sizeof(SIN_LIST_NODE), &part) ||
        alloc_add_overflow(total, part, &total)) return false;
    nodes = full_leaves;
    while (nodes > 1u) {
      size_t output = (nodes + LIST_BRANCH - 1u) / LIST_BRANCH;
      if (alloc_mul_overflow(output, sizeof(SIN_LIST_NODE *), &part) ||
          alloc_add_overflow(total, part, &total) ||
          alloc_mul_overflow(output, sizeof(SIN_LIST_NODE), &part) ||
          alloc_add_overflow(total, part, &total)) return false;
      nodes = output;
    }
  }
  if (alloc_add_overflow(total, sizeof(SIN_LIST_NODE), &total)) return false;
  if (bytes) *bytes = total;
  return true;
}

static void consume_owned_values(VALUE_t *elements, size_t count) {
  if (!elements) return;
  for (size_t i = 0; i < count; ++i) value_free(&elements[i]);
}

static SIN_LIST_t *list_new(size_t count, size_t tail_count,
                            SIN_LIST_NODE *root, SIN_LIST_NODE *tail) {
  SIN_LIST_t *list = alloc_calloc(1, sizeof(*list));
  if (!list) {
    node_release(root);
    node_release(tail);
    return NULL;
  }
  list->refs = 1;
  list->count = count;
  list->tail_count = tail_count;
  list->root = root;
  list->tail = tail;
  list->height = root ? root->height : 0;
  list->depth = max_depth(root ? root->depth : 1u, tail ? tail->depth : 1u);
  return list;
}

SIN_LIST_t *sin_list_build_owned(VALUE_t *elements, size_t count) {
  size_t tail_count;
  size_t full_count;
  size_t full_leaves;
  SIN_LIST_NODE **leaves = NULL;
  SIN_LIST_NODE *root = NULL;
  SIN_LIST_NODE *tail = NULL;
  SIN_LIST_t *list;

  /* The limit check intentionally precedes all array dereferences. */
  if (count > SIN_LIST_MAX_ELEMENTS || (!elements && count != 0)) return NULL;
  if (count == 0) return list_new(0, 0, NULL, NULL);

  tail_count = count <= LIST_BRANCH ? count : ((count - 1u) % LIST_BRANCH) + 1u;
  full_count = count - tail_count;
  full_leaves = full_count / LIST_BRANCH;
  if (full_leaves != 0) {
    leaves = alloc_calloc(full_leaves, sizeof(*leaves));
    if (!leaves) {
      consume_owned_values(elements, count);
      return NULL;
    }
  }
  for (size_t i = 0; i < full_leaves; ++i) {
    leaves[i] = leaf_from_owned(elements + i * LIST_BRANCH, LIST_BRANCH);
    if (!leaves[i]) {
      for (size_t j = 0; j < i; ++j) node_release(leaves[j]);
      free(leaves);
      consume_owned_values(elements, count);
      return NULL;
    }
  }
  tail = leaf_from_owned(elements + full_count, tail_count);
  if (!tail) {
    for (size_t i = 0; i < full_leaves; ++i) node_release(leaves[i]);
    free(leaves);
    consume_owned_values(elements, count);
    return NULL;
  }
  if (full_leaves != 0) {
    root = build_tree(leaves, full_leaves);
    leaves = NULL;
    if (!root) {
      node_release(tail);
      consume_owned_values(elements, count);
      return NULL;
    }
  }
  list = list_new(count, tail_count, root, tail);
  if (!list) consume_owned_values(elements, count);
  return list;
}

SIN_LIST_t *sin_list_retain(SIN_LIST_t *list) {
  if (!list || list->refs == 0 || list->refs == SIZE_MAX) return NULL;
  ++list->refs;
  return list;
}

void sin_list_release(SIN_LIST_t *list) {
  if (!list || list->refs == 0 || --list->refs != 0) return;
  node_release(list->root);
  node_release(list->tail);
  free(list);
}

size_t sin_list_count(const SIN_LIST_t *list) { return list ? list->count : 0; }

size_t sin_list_depth(const SIN_LIST_t *list) { return list ? list->depth : 0; }

static const SIN_LIST_NODE *iter_descend(SIN_LIST_ITER_t *iter) {
  const SIN_LIST_NODE *node = iter->node;
  while (node != NULL && !node->leaf) {
    if (iter->depth >= SIN_LIST_ITER_MAX_LEVELS) return NULL;
    iter->stack[iter->depth] = node;
    iter->slots[iter->depth] = 0;
    ++iter->depth;
    traversal_stat_inc(&traversal_stats.node_visits);
    node = node->data.children[0];
  }
  if (node != NULL) traversal_stat_inc(&traversal_stats.leaf_visits);
  return node;
}

bool sin_list_iter_init(SIN_LIST_ITER_t *iter, const SIN_LIST_t *list) {
  if (iter == NULL) return false;
  *iter = (SIN_LIST_ITER_t){
    .list = list,
    .node = list == NULL ? NULL : list->root,
    .tail_pending = list != NULL && list->tail != NULL
  };
  return true;
}

bool sin_list_iter_next(SIN_LIST_ITER_t *iter, const VALUE_t **values,
                        size_t *count, const SIN_LIST_NODE **leaf) {
  const SIN_LIST_NODE *current;
  if (iter == NULL || values == NULL || count == NULL || leaf == NULL) {
    return false;
  }
  *values = NULL;
  *count = 0;
  *leaf = NULL;

  if (iter->current_leaf != NULL) {
    iter->current_leaf = NULL;
    while (iter->depth != 0) {
      const SIN_LIST_NODE *parent = iter->stack[iter->depth - 1u];
      unsigned slot = iter->slots[iter->depth - 1u] + 1u;
      if (slot < parent->slots) {
        iter->slots[iter->depth - 1u] = slot;
        iter->node = parent->data.children[slot];
        current = iter_descend(iter);
        if (current == NULL) return false;
        goto yielded_root_leaf;
      }
      --iter->depth;
    }
    iter->node = NULL;
  }

  if (iter->node != NULL) {
    current = iter_descend(iter);
    if (current == NULL) return false;
  yielded_root_leaf:
    iter->current_leaf = current;
    *values = current->data.values;
    *count = current->slots;
    *leaf = current;
    traversal_stat_add(&traversal_stats.values_yielded, current->slots);
    return true;
  }

  if (iter->tail_pending) {
    iter->tail_pending = false;
    current = iter->list->tail;
    if (current == NULL || !current->leaf) return false;
    iter->current_leaf = current;
    traversal_stat_inc(&traversal_stats.leaf_visits);
    traversal_stat_add(&traversal_stats.values_yielded, current->slots);
    *values = current->data.values;
    *count = current->slots;
    *leaf = current;
    return true;
  }
  return false;
}

const VALUE_t *sin_list_get(const SIN_LIST_t *list, size_t index) {
  if (!list || index >= list->count) return NULL;
  if (!list->root || index >= list->root->count)
    return &list->tail->data.values[index - (list->root ? list->root->count : 0)];

  const SIN_LIST_NODE *node = list->root;
  size_t offset = index;
  while (!node->leaf) {
    unsigned slot = 0;
    while (slot + 1u < node->slots &&
           offset >= node->data.children[slot]->count) {
      offset -= node->data.children[slot]->count;
      ++slot;
    }
    node = node->data.children[slot];
  }
  return &node->data.values[offset];
}

/* The returned node owns the appended leaf, or releases it on every failure. */
static SIN_LIST_NODE *append_rec(const SIN_LIST_NODE *node,
                                 SIN_LIST_NODE *leaf) {
  SIN_LIST_NODE *replacement;
  SIN_LIST_NODE *children[LIST_BRANCH] = {0};
  unsigned slots;
  unsigned last;
  size_t child_capacity;

  if (!node || node->leaf || !leaf) {
    node_release(leaf);
    return NULL;
  }
  slots = node->slots;
  last = slots - 1u;
  child_capacity = max_tree_count(node->height - 1u);
  if (node->data.children[last]->count < child_capacity) {
    replacement = append_rec(node->data.children[last], leaf);
    if (!replacement) return NULL;
    children[last] = replacement;
  } else if (slots < LIST_BRANCH) {
    replacement = new_path_owned(node->height - 1u, leaf);
    if (!replacement) return NULL;
    children[last + 1u] = replacement;
    slots++;
  } else {
    node_release(leaf);
    return NULL;
  }
  for (unsigned i = 0; i < node->slots; ++i) {
    if (children[i]) continue;
    if (!node_retain(node->data.children[i])) {
      for (unsigned j = 0; j < slots; ++j) node_release(children[j]);
      return NULL;
    }
    children[i] = node->data.children[i];
  }
  replacement = branch_from_owned(children, slots, node->height);
  if (!replacement) {
    for (unsigned i = 0; i < slots; ++i) node_release(children[i]);
  }
  return replacement;
}

static SIN_LIST_NODE *append_tree(const SIN_LIST_NODE *root,
                                  SIN_LIST_NODE *leaf) {
  SIN_LIST_NODE *old_root;
  SIN_LIST_NODE *path;
  SIN_LIST_NODE *children[2];
  if (!leaf) return NULL;
  if (!root) return leaf;
  if (root->count < max_tree_count(root->height))
    return append_rec(root, leaf);

  if (!node_retain((SIN_LIST_NODE *)root)) {
    node_release(leaf);
    return NULL;
  }
  old_root = (SIN_LIST_NODE *)root;
  path = new_path_owned(root->height, leaf);
  if (!path) {
    node_release(old_root);
    return NULL;
  }
  children[0] = old_root;
  children[1] = path;
  SIN_LIST_NODE *new_root = branch_from_owned(children, 2, root->height + 1u);
  if (!new_root) {
    node_release(old_root);
    node_release(path);
  }
  return new_root;
}

/* Wrap an owned tree until it reaches the requested branch height. */
static SIN_LIST_NODE *tree_raise_owned(SIN_LIST_NODE *tree,
                                       unsigned height) {
  if (!tree || tree->height > height) return NULL;
  while (tree->height < height) {
    SIN_LIST_NODE *children[1] = {tree};
    SIN_LIST_NODE *parent = branch_from_owned(children, 1,
                                              tree->height + 1u);
    if (!parent) {
      node_release(tree);
      return NULL;
    }
    tree = parent;
  }
  return tree;
}

/* Join two owned trees, rebuilding only the new top spine. */
static SIN_LIST_NODE *tree_join_owned(SIN_LIST_NODE *left,
                                      SIN_LIST_NODE *right) {
  SIN_LIST_NODE *children[2];
  unsigned height;
  SIN_LIST_NODE *result;
  if (!left) return right;
  if (!right) return left;
  height = left->height > right->height ? left->height : right->height;
  if (left->height < height) {
    left = tree_raise_owned(left, height);
    if (!left) {
      node_release(right);
      return NULL;
    }
  }
  if (right->height < height) {
    right = tree_raise_owned(right, height);
    if (!right) {
      node_release(left);
      return NULL;
    }
  }
  children[0] = left;
  children[1] = right;
  result = branch_from_owned(children, 2, height + 1u);
  if (!result) {
    node_release(left);
    node_release(right);
  }
  return result;
}

/* Append an owned leaf to an owned tree, releasing the old spine. */
static SIN_LIST_NODE *tree_append_owned(SIN_LIST_NODE *tree,
                                        SIN_LIST_NODE *leaf) {
  SIN_LIST_NODE *result;
  SIN_LIST_NODE *children[2];
  if (!tree) return leaf;
  if (tree->leaf) {
    children[0] = tree;
    children[1] = leaf;
    result = branch_from_owned(children, 2, 1);
    if (!result) {
      node_release(tree);
      node_release(leaf);
    }
    return result;
  }
  result = append_tree(tree, leaf);
  node_release(tree);
  return result;
}

/* Retain a tree after dropping its first complete leaf. */
static SIN_LIST_NODE *tree_suffix_owned(const SIN_LIST_NODE *node,
                                       bool *ok) {
  SIN_LIST_NODE *children[LIST_BRANCH] = {0};
  SIN_LIST_NODE *suffix;
  unsigned out_slots = 0;
  if (!ok) return NULL;
  *ok = false;
  if (!node) return NULL;
  if (node->leaf) {
    *ok = true;
    return NULL;
  }
  suffix = tree_suffix_owned(node->data.children[0], ok);
  if (!*ok) return NULL;
  if (suffix) children[out_slots++] = suffix;
  for (unsigned i = 1; i < node->slots; ++i) {
    if (!node_retain(node->data.children[i])) {
      for (unsigned j = 0; j < out_slots; ++j) node_release(children[j]);
      return NULL;
    }
    children[out_slots++] = node->data.children[i];
  }
  if (out_slots == 0) return NULL;
  suffix = branch_from_owned(children, out_slots, node->height);
  if (!suffix) {
    for (unsigned i = 0; i < out_slots; ++i) node_release(children[i]);
    return NULL;
  }
  return suffix;
}

/* Retain a list's complete prefix, promoting its tail into that prefix. */
static SIN_LIST_NODE *retain_full_tree(const SIN_LIST_t *list) {
  SIN_LIST_NODE *tree;
  SIN_LIST_NODE *tail;
  if (!list || !list->tail) return NULL;
  if (!list->root) return node_retain(list->tail) ? list->tail : NULL;
  if (!node_retain(list->root)) return NULL;
  tree = list->root;
  if (!node_retain(list->tail)) {
    node_release(tree);
    return NULL;
  }
  tail = list->tail;
  tree = tree_append_owned(tree, tail);
  if (!tree) return NULL;
  return tree;
}

SIN_LIST_t *sin_list_append(const SIN_LIST_t *list, const VALUE_t *value) {
  SIN_LIST_NODE *root = NULL;
  SIN_LIST_NODE *tail = NULL;
  SIN_LIST_t *result;
  size_t new_count;
  size_t new_tail_count;
  if (!list || !value || list->count >= SIN_LIST_MAX_ELEMENTS) return NULL;
  new_count = list->count + 1u;
  if (value_depth(value) > SIN_LIST_MAX_DEPTH) return NULL;
  if (!list->tail) {
    tail = leaf_clone_single(value);
    if (!tail) return NULL;
    new_tail_count = 1;
  } else if (list->tail_count < LIST_BRANCH) {
    tail = leaf_clone_append(list->tail, value);
    if (!tail) return NULL;
    if (list->root && !node_retain(list->root)) {
      node_release(tail);
      return NULL;
    }
    root = list->root;
    new_tail_count = list->tail_count + 1u;
  } else {
    SIN_LIST_NODE *promoted;
    if (!node_retain(list->tail)) return NULL;
    promoted = list->tail;
    tail = leaf_clone_single(value);
    if (!tail) {
      node_release(promoted);
      return NULL;
    }
    root = append_tree(list->root, promoted);
    if (!root) {
      node_release(tail);
      return NULL;
    }
    new_tail_count = 1;
  }
  result = list_new(new_count, new_tail_count, root, tail);
  if (!result) return NULL;
  return result;
}

static SIN_LIST_NODE *set_rec(const SIN_LIST_NODE *node, size_t index,
                              const VALUE_t *value) {
  SIN_LIST_NODE *replacement;
  SIN_LIST_NODE *children[LIST_BRANCH] = {0};
  size_t offset = index;
  unsigned target = 0;
  if (!node || !value || index >= node->count) return NULL;
  if (node->leaf) return leaf_clone_set(node, index, value);
  while (target + 1u < node->slots &&
         offset >= node->data.children[target]->count) {
    offset -= node->data.children[target]->count;
    ++target;
  }
  replacement = set_rec(node->data.children[target], offset, value);
  if (!replacement) return NULL;
  children[target] = replacement;
  for (unsigned i = 0; i < node->slots; ++i) {
    if (children[i]) continue;
    if (!node_retain(node->data.children[i])) {
      for (unsigned j = 0; j < node->slots; ++j) node_release(children[j]);
      return NULL;
    }
    children[i] = node->data.children[i];
  }
  SIN_LIST_NODE *result = branch_from_owned(children, node->slots, node->height);
  if (!result) {
    for (unsigned i = 0; i < node->slots; ++i) node_release(children[i]);
  }
  return result;
}

SIN_LIST_t *sin_list_set(const SIN_LIST_t *list, size_t index,
                         const VALUE_t *value) {
  SIN_LIST_NODE *root = NULL;
  SIN_LIST_NODE *tail = NULL;
  SIN_LIST_t *result;
  if (!list || !value || index >= list->count ||
      value_depth(value) > SIN_LIST_MAX_DEPTH)
    return NULL;
  if (list->root && index < list->root->count) {
    root = set_rec(list->root, index, value);
    if (!root) return NULL;
    if (!node_retain(list->tail)) {
      node_release(root);
      return NULL;
    }
    tail = list->tail;
  } else {
    tail = leaf_clone_set(list->tail,
                          index - (list->root ? list->root->count : 0), value);
    if (!tail) return NULL;
    if (list->root && !node_retain(list->root)) {
      node_release(tail);
      return NULL;
    }
    root = list->root;
  }
  result = list_new(list->count, list->tail_count, root, tail);
  return result;
}

SIN_LIST_t *sin_list_concat(const SIN_LIST_t *left, const SIN_LIST_t *right) {
  SIN_LIST_NODE *root = NULL;
  SIN_LIST_NODE *tail = NULL;
  SIN_LIST_NODE *right_root = NULL;
  SIN_LIST_NODE *right_suffix = NULL;
  SIN_LIST_NODE *boundary = NULL;
  size_t left_tail_count;
  size_t right_tail_count;
  size_t tail_sum;
  size_t prefix_from_tail;
  size_t right_tail_start;
  bool left_aligned;
  SIN_LIST_t *result;
  if (!left || !right || left->count > SIN_LIST_MAX_ELEMENTS - right->count)
    return NULL;
  if (left->count == 0) return sin_list_retain((SIN_LIST_t *)right);
  if (right->count == 0) return sin_list_retain((SIN_LIST_t *)left);
  left_tail_count = left->tail_count;
  right_tail_count = right->tail_count;
  tail_sum = left_tail_count + right_tail_count;
  left_aligned = left_tail_count == LIST_BRANCH;
  right_tail_start = right->count - right_tail_count;

  /* An aligned left side can promote its full tail and retain the RHS tail. */
  if (left_aligned) {
    root = retain_full_tree(left);
    if (!root) return NULL;
    if (right->root && !node_retain(right->root)) {
      node_release(root);
      return NULL;
    }
    right_root = right->root;
    root = tree_join_owned(root, right_root);
    if (!root) return NULL;
    tail = node_retain(right->tail) ? right->tail : NULL;
    if (!tail) {
      node_release(root);
      return NULL;
    }
  } else if (tail_sum <= LIST_BRANCH) {
    /* The final tail spans both incomplete boundary leaves. */
    if (left->root && !node_retain(left->root)) return NULL;
    root = left->root;
    if (right->root && !node_retain(right->root)) {
      node_release(root);
      return NULL;
    }
    right_root = right->root;
    root = tree_join_owned(root, right_root);
    tail = leaf_clone_append_range(left->tail, right, right_tail_start,
                                   right_tail_count);
    if (!tail) {
      node_release(root);
      return NULL;
    }
  } else {
    /* Keep all complete RHS leaves.  Only the RHS tail crosses the boundary. */
    prefix_from_tail = LIST_BRANCH - left_tail_count;
    if (left->root && !node_retain(left->root)) return NULL;
    root = left->root;
    boundary = leaf_clone_append_range(left->tail, right, 0,
                                       prefix_from_tail);
    if (!boundary) {
      node_release(root);
      return NULL;
    }
    root = tree_append_owned(root, boundary);
    if (!root) return NULL;
    if (right->root) {
      bool suffix_ok = false;
      SIN_LIST_NODE *remainder = leaf_clone_append_range(
          NULL, right, prefix_from_tail, LIST_BRANCH - prefix_from_tail);
      if (!remainder) {
        node_release(root);
        return NULL;
      }
      right_suffix = tree_suffix_owned(right->root, &suffix_ok);
      if (!suffix_ok) {
        node_release(remainder);
        node_release(root);
        return NULL;
      }
      root = tree_append_owned(root, remainder);
      if (!root) {
        node_release(right_suffix);
        return NULL;
      }
      root = tree_join_owned(root, right_suffix);
      if (!root) return NULL;
      boundary = leaf_clone_append_range(NULL, right, right_tail_start,
                                         prefix_from_tail);
      if (!boundary) {
        node_release(root);
        return NULL;
      }
      root = tree_append_owned(root, boundary);
      if (!root) return NULL;
      tail = leaf_clone_append_range(NULL, right,
                                     right_tail_start + prefix_from_tail,
                                     right_tail_count - prefix_from_tail);
    } else {
      tail = leaf_clone_append_range(NULL, right, prefix_from_tail,
                                     right->count - prefix_from_tail);
    }
    if (!tail) {
      node_release(root);
      return NULL;
    }
  }
  result = list_new(left->count + right->count,
                    ((left->count + right->count - 1u) % LIST_BRANCH) + 1u,
                    root, tail);
  if (!result) return NULL;
  return result;
}

SIN_LIST_t *sin_list_slice(const SIN_LIST_t *list, size_t start, size_t length) {
  VALUE_t *values;
  SIN_LIST_t *result;
  if (!list || start > list->count || length > SIN_LIST_MAX_ELEMENTS ||
      length > list->count - start) return NULL;
  if (length == 0) return sin_list_build_owned(NULL, 0);
  values = alloc_calloc(length, sizeof(*values));
  if (!values) return NULL;
  for (size_t i = 0; i < length; ++i) {
    const VALUE_t *source = sin_list_get(list, start + i);
    if (!source || !value_clone_fallible(source, &values[i])) {
      for (size_t j = 0; j < i; ++j) value_free(&values[j]);
      free(values);
      return NULL;
    }
  }
  result = sin_list_build_owned(values, length);
  free(values);
  return result;
}

bool sin_list_equal(const SIN_LIST_t *left, const SIN_LIST_t *right) {
  SIN_LIST_ITER_t left_iter;
  SIN_LIST_ITER_t right_iter;
  const VALUE_t *left_values = NULL;
  const VALUE_t *right_values = NULL;
  const SIN_LIST_NODE *left_leaf = NULL;
  const SIN_LIST_NODE *right_leaf = NULL;
  size_t left_count = 0;
  size_t right_count = 0;
  size_t left_offset = 0;
  size_t right_offset = 0;
  bool have_left;
  bool have_right;
  if (!left || !right) return false;
  if (left == right) return true;
  if (left->count != right->count) return false;
  if (!sin_list_iter_init(&left_iter, left) ||
      !sin_list_iter_init(&right_iter, right)) return false;

  have_left = sin_list_iter_next(&left_iter, &left_values, &left_count,
                                 &left_leaf);
  have_right = sin_list_iter_next(&right_iter, &right_values, &right_count,
                                  &right_leaf);
  while (have_left && have_right) {
    size_t remaining_left = left_count - left_offset;
    size_t remaining_right = right_count - right_offset;
    size_t paired = remaining_left < remaining_right
        ? remaining_left : remaining_right;
    if (left_offset == 0 && right_offset == 0 && paired == left_count &&
        paired == right_count && left_leaf == right_leaf) {
      traversal_stat_inc(&traversal_stats.shared_leaf_skips);
    } else {
      for (size_t i = 0; i < paired; ++i) {
        traversal_stat_inc(&traversal_stats.value_comparisons);
        if (!value_equal(&left_values[left_offset + i],
                         &right_values[right_offset + i])) return false;
      }
    }
    left_offset += paired;
    right_offset += paired;
    if (left_offset == left_count) {
      left_offset = 0;
      have_left = sin_list_iter_next(&left_iter, &left_values, &left_count,
                                     &left_leaf);
    }
    if (right_offset == right_count) {
      right_offset = 0;
      have_right = sin_list_iter_next(&right_iter, &right_values, &right_count,
                                      &right_leaf);
    }
  }
  return !have_left && !have_right;
}
