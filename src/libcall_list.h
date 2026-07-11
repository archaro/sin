// Canonical libcall registration list.

// Licensed under the MIT License - see LICENSE file for details.

#pragma once

// One source of truth for libcall metadata and handler symbols.
// Columns: libname, callname, lib_index, call_index, args, handler
#define LIBCALL_LIST(X) \
  X("sys",  "backup",      1, 0, 0, lc_sys_backup) \
  X("sys",  "log",         1, 1, 1, lc_sys_log) \
  X("sys",  "shutdown",    1, 2, 0, lc_sys_shutdown) \
  X("sys",  "abort",       1, 3, 0, lc_sys_abort) \
  X("sys",  "compile",     1, 4, 1, lc_sys_compile) \
  X("task", "newgametask", 2, 0, 3, lc_task_newgametask) \
  X("task", "killtask",    2, 1, 1, lc_task_killtask) \
  X("net",  "input",       3, 0, 0, lc_net_input) \
  X("net",  "write",       3, 1, 2, lc_net_write) \
  X("str",  "capitalise",  4, 0, 1, lc_str_capitalise) \
  X("str",  "upper",       4, 1, 1, lc_str_upper) \
  X("str",  "lower",       4, 2, 1, lc_str_lower)
