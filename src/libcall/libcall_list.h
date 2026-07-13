// Canonical libcall registration list.

// Licensed under the MIT License - see LICENSE file for details.

#pragma once

// One source of truth for libcall metadata and handler symbols.
// Columns: libname, callname, lib_index, call_index, args, handler
#define LIBCALL_LIST(X) \
  X("sys",  "backup",      1,  0, 0, lc_sys_backup) \
  X("sys",  "log",         1,  1, 1, lc_sys_log) \
  X("sys",  "shutdown",    1,  2, 0, lc_sys_shutdown) \
  X("sys",  "abort",       1,  3, 0, lc_sys_abort) \
  X("sys",  "compile",     1,  4, 1, lc_sys_compile) \
  X("task", "newgametask", 2,  0, 3, lc_task_newgametask) \
  X("task", "killtask",    2,  1, 1, lc_task_killtask) \
  X("net",  "input",       3,  0, 0, lc_net_input) \
  X("net",  "write",       3,  1, 2, lc_net_write) \
  X("str",  "capitalise",  4,  0, 1, lc_str_capitalise) \
  X("str",  "upper",       4,  1, 1, lc_str_upper) \
  X("str",  "lower",       4,  2, 1, lc_str_lower) \
  X("str",  "len",         4,  3, 1, lc_str_len) \
  X("str",  "trim",        4,  4, 1, lc_str_trim) \
  X("str",  "ltrim",       4,  5, 1, lc_str_ltrim) \
  X("str",  "rtrim",       4,  6, 1, lc_str_rtrim) \
  X("str",  "substr",      4,  7, 3, lc_str_substr) \
  X("str",  "find",        4,  8, 2, lc_str_find) \
  X("str",  "contains",    4,  9, 2, lc_str_contains) \
  X("str",  "startswith",  4, 10, 2, lc_str_startswith) \
  X("str",  "endswith",    4, 11, 2, lc_str_endswith) \
  X("str",  "eqcasei",     4, 12, 2, lc_str_eqcasei)
