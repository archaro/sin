#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "list.h"

/*
 * A list has at most ceil(log_32(SIN_LIST_MAX_ELEMENTS)) branch levels.  The
 * extra slot keeps the state valid for a one-leaf root and makes this bound
 * independent of the current list size.  With the current 1,048,576 element
 * limit this is four fixed slots (the deepest root has three branch levels).
 */
#define SIN_LIST_ITER_MAX_LEVELS \
  (1u + (SIN_LIST_MAX_ELEMENTS > 32u ? 1u : 0u) + \
   (SIN_LIST_MAX_ELEMENTS > 1024u ? 1u : 0u) + \
   (SIN_LIST_MAX_ELEMENTS > 32768u ? 1u : 0u) + \
   (SIN_LIST_MAX_ELEMENTS > 1048576u ? 1u : 0u))

typedef struct SIN_LIST_NODE SIN_LIST_NODE;

typedef struct {
  const SIN_LIST_t *list;
  const SIN_LIST_NODE *node;
  const SIN_LIST_NODE *current_leaf;
  const SIN_LIST_NODE *stack[SIN_LIST_ITER_MAX_LEVELS];
  unsigned slots[SIN_LIST_ITER_MAX_LEVELS];
  unsigned depth;
  bool tail_pending;
} SIN_LIST_ITER_t;

typedef struct {
  size_t node_visits;
  size_t leaf_visits;
  size_t values_yielded;
  size_t shared_leaf_skips;
  size_t value_comparisons;
} SIN_LIST_TRAVERSAL_STATS_t;

/* Internal consumers receive borrowed contiguous values and leaf identity. */
bool sin_list_iter_init(SIN_LIST_ITER_t *iter, const SIN_LIST_t *list);
bool sin_list_iter_next(SIN_LIST_ITER_t *iter, const VALUE_t **values,
                        size_t *count, const SIN_LIST_NODE **leaf);

/* Test-only observability; this header is not part of the runtime API. */
void sin_list_test_reset_traversal_stats(void);
SIN_LIST_TRAVERSAL_STATS_t sin_list_test_traversal_stats(void);
