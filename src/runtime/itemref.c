#include <stdlib.h>
#include <string.h>

#include "itemref.h"
#include "itemstore/item.h"
#include "memory.h"

struct SIN_ITEMREF {
  size_t refs;
  size_t length;
  char path[];
};

SIN_ITEMREF_t *sin_itemref_create(const char *path) {
  size_t length;
  SIN_ITEMREF_t *ref;
  if (!path || path[0] == '\0') return NULL;
  length = strlen(path);
  if (length >= MAX_ITEM_NAME) return NULL;
  if (length > SIZE_MAX - sizeof(*ref) - 1u) return NULL;
  ref = alloc_malloc(sizeof(*ref) + length + 1u);
  if (!ref) return NULL;
  memcpy(ref->path, path, length + 1u);
  ref->refs = 1u;
  ref->length = length;
  return ref;
}

SIN_ITEMREF_t *sin_itemref_retain(SIN_ITEMREF_t *ref) {
  if (ref && ref->refs < SIZE_MAX) ref->refs++;
  else if (ref) return NULL;
  return ref;
}

void sin_itemref_release(SIN_ITEMREF_t *ref) {
  if (!ref || ref->refs == 0u) return;
  ref->refs--;
  if (ref->refs == 0u) {
    free(ref);
  }
}

const char *sin_itemref_path(const SIN_ITEMREF_t *ref) {
  return ref ? ref->path : NULL;
}

size_t sin_itemref_path_length(const SIN_ITEMREF_t *ref) {
  return ref ? ref->length : 0u;
}
