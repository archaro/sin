// Libcall registry lookup and validation.

// Licensed under the MIT License - see LICENSE file for details.

#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#include "item.h"

typedef struct RuntimeContext RuntimeContext;
typedef uint8_t *(*OP_t)(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item);

typedef struct {
  const char *libname;
  const char *callname;
  uint8_t lib_index;
  uint8_t call_index;
  uint8_t args;
  OP_t func;
} LIBCALL_t;

typedef struct {
  OP_t func;
  uint8_t args;
} LIBCALL_REG_ENTRY_t;

typedef struct {
  const char *libname;
  const char *callname;
  uint8_t lib_index;
  uint8_t call_index;
  uint8_t args;
  char *lookup_key;
} LIBCALL_NAME_ENTRY_t;

typedef struct {
  LIBCALL_REG_ENTRY_t *entries;
  LIBCALL_NAME_ENTRY_t *names;
  size_t width;
  size_t height;
  size_t name_count;
  bool ready;
} LibcallRegistry;

extern const LIBCALL_t libcalls[];

// Registry objects and the process-default registry are unsynchronized.
// Lazy initialization, destruction, and test reset require process quiescence
// and a serial flow with all registry users.
bool libcall_registry_init(LibcallRegistry *registry);
void libcall_registry_destroy(LibcallRegistry *registry);
bool libcall_registry_validate(LibcallRegistry *registry);
bool libcall_registry_lookup_pair(LibcallRegistry *registry, const char *libname, const char *callname, uint8_t *lib_index, uint8_t *call_index, uint8_t *args);
OP_t libcall_registry_func_pair(LibcallRegistry *registry, uint8_t lib_index, uint8_t call_index);
bool libcall_registry_pair_arg_count(LibcallRegistry *registry, uint8_t lib_index, uint8_t call_index, uint8_t *args);

bool libcall_lookup_pair(const char *libname, const char *callname, uint8_t *lib_index, uint8_t *call_index, uint8_t *args);
bool libcall_pair_arg_count(uint8_t lib_index, uint8_t call_index, uint8_t *args);
bool libcall_init_registry(void);
void libcall_free_registry(void);
void libcall_reset_registry_for_tests(void);
bool libcall_validate_registry(void);
bool libcall_registry_self_check(const LIBCALL_t *calls, bool fail_fast);
OP_t libcall_func_pair(uint8_t lib_index, uint8_t call_index);
