// Registered libcall table.

// Licensed under the MIT License - see LICENSE file for details.

#include <stddef.h>

#include "libcall_registry.h"
#include "libcall_handlers.h"
#include "libcall_list.h"

#define LIBCALL_TABLE_ENTRY(libname, callname, lib_index, call_index, args, handler) \
  {libname, callname, lib_index, call_index, args, handler},

const LIBCALL_t libcalls[] = {
  LIBCALL_LIST(LIBCALL_TABLE_ENTRY)
  {NULL, NULL, -1, -1, 0, NULL}  // End marker
};

#undef LIBCALL_TABLE_ENTRY

#define LIBCALL_FUNC_SIGNATURE_GUARD(name) \
  _Static_assert(__builtin_types_compatible_p(__typeof__(&(name)), OP_t), \
                 "libcall function must match OP_t signature")

#define LIBCALL_FUNC_SIGNATURE_GUARD_ENTRY(libname, callname, lib_index, call_index, args, handler) \
  LIBCALL_FUNC_SIGNATURE_GUARD(handler);

LIBCALL_LIST(LIBCALL_FUNC_SIGNATURE_GUARD_ENTRY)

#undef LIBCALL_FUNC_SIGNATURE_GUARD_ENTRY
