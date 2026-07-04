// A basic memory management module.
// It will get cleverer.

// Licensed under the MIT License - see LICENSE file for details.

#pragma once

#include <stdbool.h>
#include <stddef.h>

// This macro makes numbers bigger.  It is used to double the size of an
// array, but by putting it into a macro the scale factor can be adjusted
// across the project with one stroke.
#define GROW_CAPACITY(capacity) ((capacity) < 8 ? 8 : (capacity) * 2)

// A pretty wrapper to reallocate(), which makes the cast neater
#define GROW_ARRAY(type, ptr, oldsize, newsize) \
  (type*)reallocate(ptr, sizeof(type) * (oldsize), sizeof(type) * (newsize))

void* reallocate(void* pointer, size_t oldSize, size_t newSize);

bool alloc_mul_overflow(size_t a, size_t b, size_t *out);
bool alloc_add_overflow(size_t a, size_t b, size_t *out);
bool alloc_grow_capacity(size_t oldcap, size_t needed, size_t *newcap);
bool alloc_grow_array(void **ptr, size_t oldcap, size_t newcap, size_t elem_size);
void alloc_test_fail_after(long nth_allocation);
