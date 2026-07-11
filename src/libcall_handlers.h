// Concrete libcall handler declarations.

// Licensed under the MIT License - see LICENSE file for details.

#pragma once

#include <stdint.h>

#include "interpret.h"
#include "item.h"

#include "libcall_list.h"

#define LIBCALL_DECLARE_HANDLER(libname, callname, lib_index, call_index, args, handler) \
  uint8_t *handler(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item);

LIBCALL_LIST(LIBCALL_DECLARE_HANDLER)

#undef LIBCALL_DECLARE_HANDLER
