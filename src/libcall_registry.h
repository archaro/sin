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
  int8_t lib_index;
  int8_t call_index;
  uint8_t args;
  OP_t func;
} LIBCALL_t;

typedef struct {
  OP_t func;
  uint8_t args;
  bool present;
} LIBCALL_REG_ENTRY_t;

typedef struct {
  const char *libname;
  const char *callname;
  uint8_t lib_index;
  uint8_t call_index;
  uint8_t args;
  uint8_t token;
  char *lookup_key;
} LIBCALL_NAME_ENTRY_t;

typedef struct {
  LIBCALL_REG_ENTRY_t *entries;
  LIBCALL_NAME_ENTRY_t *names;
  size_t width;
  size_t height;
  size_t name_count;
  OP_t token_funcs[256];
  bool token_present[256];
  bool ready;
} LibcallRegistry;

extern const LIBCALL_t libcalls[];

bool libcall_registry_init(LibcallRegistry *registry);
void libcall_registry_destroy(LibcallRegistry *registry);
bool libcall_registry_validate(LibcallRegistry *registry);
bool libcall_registry_lookup_token(LibcallRegistry *registry, const char *libname, const char *callname, uint8_t *token, uint8_t *args);
OP_t libcall_registry_func_token(LibcallRegistry *registry, uint8_t token);

bool libcall_lookup_token(const char *libname, const char *callname, uint8_t *token, uint8_t *args);
bool libcall_token_arg_count(uint8_t token, uint8_t *args);
bool libcall_init_registry(void);
void libcall_free_registry(void);
void libcall_reset_registry_for_tests(void);
bool libcall_validate_registry(void);
bool libcall_registry_self_check(const LIBCALL_t *calls, bool fail_fast);
bool libcall_names_unique(const LIBCALL_t *calls);
OP_t libcall_func_token(uint8_t token);
