// The library call lookup
// Library calls are pseudo items, that are always of the form:
//   libname.callname{args}
// and always return a value.

// Licensed under the MIT License - see LICENSE file for details.

#pragma once

#include <stdint.h>
#include <stdbool.h>

#include "interpret.h"

typedef struct {
  const char *libname;
  const char *callname;
  int8_t lib_index;
  int8_t call_index;
  uint8_t args;
  OP_t func;
} LIBCALL_t;

extern const LIBCALL_t libcalls[];

bool libcall_lookup_token(const char *libname, const char *callname, uint8_t *token, uint8_t *args);
bool libcall_init_registry(void);
void libcall_registry_free_all(void);
bool libcall_validate_registry(void);
bool libcall_registry_self_check(const LIBCALL_t *calls, bool fail_fast);
bool libcall_names_unique(const LIBCALL_t *calls);
OP_t libcall_func_token(uint8_t token);
