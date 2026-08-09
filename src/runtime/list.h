#pragma once

#include <stddef.h>
#include <stdbool.h>
#include "bytecode/bytecode_abi.h"
#include "value.h"

#define SIN_LIST_MAX_ELEMENTS SIN_BYTECODE_LIST_ELEMENT_LIMIT
#define SIN_LIST_MAX_DEPTH SIN_BYTECODE_LIST_DEPTH_LIMIT

typedef struct SIN_LIST SIN_LIST_t;

/*
 * Build from owned values in source order. For a valid non-empty input, every
 * element is consumed and replaced with VALUE_NIL on both success and
 * failure; the caller still owns the array storage. A returned list is owned
 * by the caller. NULL with a non-zero count and over-limit counts are
 * rejected before consuming the input.
 */
SIN_LIST_t *sin_list_build_owned(VALUE_t *elements, size_t count);
/* Retain/release list handles; all accessors below borrow their results. */
SIN_LIST_t *sin_list_retain(SIN_LIST_t *list);
void sin_list_release(SIN_LIST_t *list);
size_t sin_list_count(const SIN_LIST_t *list);
size_t sin_list_depth(const SIN_LIST_t *list);
const VALUE_t *sin_list_get(const SIN_LIST_t *list, size_t index);
/* Inputs are borrowed; failures leave the original list unchanged. */
SIN_LIST_t *sin_list_append(const SIN_LIST_t *list, const VALUE_t *value);
SIN_LIST_t *sin_list_set(const SIN_LIST_t *list, size_t index, const VALUE_t *value);
/* Borrowed inputs; returned lists are owned by the caller. */
SIN_LIST_t *sin_list_concat(const SIN_LIST_t *left, const SIN_LIST_t *right);
SIN_LIST_t *sin_list_slice(const SIN_LIST_t *list, size_t start, size_t length);
bool sin_list_equal(const SIN_LIST_t *left, const SIN_LIST_t *right);
