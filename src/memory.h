// A basic memory management module.
// It will get cleverer.

// Licensed under the MIT License - see LICENSE file for details.

#pragma once

#include <stddef.h>

// This macro makes numbers bigger.  It is used to double the size of an
// array, but by putting it into a macro the scale factor can be adjusted
// across the project with one stroke.
#define GROW_CAPACITY(capacity) ((capacity) < 8 ? 8 : (capacity) * 2)

// A pretty wrapper to reallocate(), which makes the cast neater
#define GROW_ARRAY(type, ptr, oldsize, newsize) \
  (type*)reallocate(ptr, sizeof(type) * (oldsize), sizeof(type) * (newsize))

// One stop shop for freeing arrays
#define FREE_ARRAY(type, ptr, oldsize) \
    reallocate(ptr, sizeof(type) * (oldsize), 0)

void* reallocate(void* pointer, size_t oldSize, size_t newSize);

