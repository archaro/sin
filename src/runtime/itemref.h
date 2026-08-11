#pragma once

#include <stddef.h>
#include <stdbool.h>

typedef struct SIN_ITEMREF SIN_ITEMREF_t;

/* Creates an owning reference from an already-canonical path. */
SIN_ITEMREF_t *sin_itemref_create(const char *path);
bool sin_itemref_allocation_bytes(size_t path_length, size_t *bytes);
/* Retain/release ownership. Retain returns NULL on count overflow. */
SIN_ITEMREF_t *sin_itemref_retain(SIN_ITEMREF_t *ref);
void sin_itemref_release(SIN_ITEMREF_t *ref);
/* Accessors borrow storage owned by ref; it remains valid until release. */
const char *sin_itemref_path(const SIN_ITEMREF_t *ref);
size_t sin_itemref_path_length(const SIN_ITEMREF_t *ref);
