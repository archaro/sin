// Registered libcall table.

// Licensed under the MIT License - see LICENSE file for details.

#include <stddef.h>

#include "libcall_registry.h"
#include "libcall_handlers.h"

const LIBCALL_t libcalls[] = {
  {"sys",  "backup",       1, 0, 0, lc_sys_backup},
  {"sys",  "log",          1, 1, 1, lc_sys_log},
  {"sys",  "shutdown",     1, 2, 0, lc_sys_shutdown},
  {"sys",  "abort",        1, 3, 0, lc_sys_abort},
  {"sys",  "compile",      1, 4, 1, lc_sys_compile},
  {"task", "newgametask",  2, 0, 3, lc_task_newgametask},
  {"task", "killtask",     2, 1, 1, lc_task_killtask},
  {"net",  "input",        3, 0, 0, lc_net_input},
  {"net",  "write",        3, 1, 2, lc_net_write},
  {"str",  "capitalise",   4, 0, 1, lc_str_capitalise},
  {"str",  "upper",        4, 1, 1, lc_str_upper},
  {"str",  "lower",        4, 2, 1, lc_str_lower},
  {NULL,   NULL,          -1, -1, 0, NULL}  // End marker
};

#define LIBCALL_FUNC_SIGNATURE_GUARD(name) \
  _Static_assert(__builtin_types_compatible_p(__typeof__(&(name)), OP_t), \
                 "libcall function must match OP_t signature")

LIBCALL_FUNC_SIGNATURE_GUARD(lc_sys_backup);
LIBCALL_FUNC_SIGNATURE_GUARD(lc_sys_log);
LIBCALL_FUNC_SIGNATURE_GUARD(lc_sys_shutdown);
LIBCALL_FUNC_SIGNATURE_GUARD(lc_sys_abort);
LIBCALL_FUNC_SIGNATURE_GUARD(lc_sys_compile);
LIBCALL_FUNC_SIGNATURE_GUARD(lc_task_newgametask);
LIBCALL_FUNC_SIGNATURE_GUARD(lc_task_killtask);
LIBCALL_FUNC_SIGNATURE_GUARD(lc_net_input);
LIBCALL_FUNC_SIGNATURE_GUARD(lc_net_write);
LIBCALL_FUNC_SIGNATURE_GUARD(lc_str_capitalise);
LIBCALL_FUNC_SIGNATURE_GUARD(lc_str_upper);
LIBCALL_FUNC_SIGNATURE_GUARD(lc_str_lower);

