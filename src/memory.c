// The memory manager

// Licensed under the MIT License - see LICENSE file for details.

#include <stdlib.h>
#include <stdint.h>

#include "memory.h"

static long g_alloc_counter = 0;
static long g_fail_after = -1;

bool alloc_mul_overflow(size_t a, size_t b, size_t *out) {
  if (a != 0 && b > SIZE_MAX / a) return true;
  if (out) *out = a * b;
  return false;
}

bool alloc_add_overflow(size_t a, size_t b, size_t *out) {
  if (a > SIZE_MAX - b) return true;
  if (out) *out = a + b;
  return false;
}

bool alloc_grow_capacity(size_t oldcap, size_t needed, size_t *newcap) {
  size_t cap = oldcap < 8 ? 8 : oldcap;
  while (cap < needed) {
    if (cap > SIZE_MAX / 2) return false;
    cap *= 2;
  }
  if (newcap) *newcap = cap;
  return true;
}

bool alloc_grow_array(void **ptr, size_t oldcap, size_t newcap, size_t elem_size) {
  size_t oldbytes = 0, newbytes = 0;
  void *res;
  if (alloc_mul_overflow(oldcap, elem_size, &oldbytes)) return false;
  if (alloc_mul_overflow(newcap, elem_size, &newbytes)) return false;
  res = reallocate(*ptr, oldbytes, newbytes);
  if (!res && newbytes != 0) return false;
  *ptr = res;
  return true;
}

void alloc_test_fail_after(long nth_allocation) { g_fail_after = nth_allocation; g_alloc_counter = 0; }

void* reallocate(void* ptr, size_t oldcount, size_t newcount) {
  // This function handles all memory allocation, reallocation, and release.
  // oldcount 	newcount 	  action
  //   0 	      not 0       allocate new block of memory (malloc)
  // not 0       	0 	      free memory (free)
  // not 0      < oldcount 	shrink existing allocation (realloc)
  // not 0      > oldcount 	grow existing allocation (realloc)
  // everything else is undefined

  (void)oldcount;

  if (newcount == 0) {
    free(ptr);
    return NULL;
  }

  g_alloc_counter++;
  if (g_fail_after >= 0 && g_alloc_counter >= g_fail_after) return NULL;

  void *res;
  if (ptr) {
    res = realloc(ptr, newcount);
  } else {
    res = calloc(1, newcount); // zero all bytes
  }

  return res;

  // We don't use oldcount yet.  But we will.
}
