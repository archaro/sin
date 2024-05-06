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

bool libcall_lookup(const char *libname, const char *callname,
                    uint8_t *lib_index, uint8_t *call_index, uint8_t *args);
void *libcall_func(uint8_t lib, uint8_t call);

