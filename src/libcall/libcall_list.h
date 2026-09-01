// Canonical libcall registration list.

// Licensed under the MIT License - see LICENSE file for details.

#pragma once

// One source of truth for libcall metadata and handler symbols.
// Columns: libname, callname, lib_index, call_index, args, handler
// Keep sys first, then sort libraries by name and calls by call_index.
// Numeric (lib_index, call_index) pairs are permanent ABI identifiers. Retired
// pairs are reserved and must never be reused.
#define LIBCALL_LIST(X) \
  X("sys",  "backup",      1,  0, 0, lc_sys_backup) \
  X("sys",  "log",         1,  1, 1, lc_sys_log) \
  X("sys",  "shutdown",    1,  2, 0, lc_sys_shutdown) \
  X("sys",  "abort",       1,  3, 0, lc_sys_abort) \
  X("sys",  "compile",     1,  4, 1, lc_sys_compile) \
  X("sys",  "exists",      1,  5, 1, lc_sys_exists) \
  X("sys",  "delete",      1,  6, 1, lc_sys_delete) \
  X("sys",  "nthname",     1,  7, 2, lc_sys_nthname) \
  X("sys",  "rootname",    1,  8, 1, lc_sys_rootname) \
  X("sys",  "save",        1,  9, 0, lc_sys_save) \
  X("sys",  "thisitem",    1, 10, 0, lc_sys_thisitem) \
  X("sys",  "parentitem",  1, 11, 0, lc_sys_parentitem) \
  X("sys",  "itemtype",    1, 12, 1, lc_sys_itemtype) \
  X("sys",  "childcount",  1, 13, 1, lc_sys_childcount) \
  X("sys",  "rootcount",   1, 14, 0, lc_sys_rootcount) \
  X("sys",  "version",     1, 15, 0, lc_sys_version) \
  X("sys",  "now",         1, 16, 0, lc_sys_now) \
  X("sys",  "monotime",    1, 17, 0, lc_sys_monotime) \
  X("sys",  "calleritem",  1, 18, 0, lc_sys_calleritem) \
  X("sys",  "paramcount",  1, 19, 1, lc_sys_paramcount) \
  X("sys",  "source",      1, 20, 1, lc_sys_source) \
  X("sys",  "itemref",     1, 21, 1, lc_sys_itemref) \
  X("sys",  "itemname",    1, 22, 1, lc_sys_itemname) \
  X("sys",  "fetch",       1, 23, 1, lc_sys_fetch) \
  X("sys",  "call",        1, 24, 2, lc_sys_call) \
  X("list", "length",      5,  0, 1, lc_list_length) \
  X("list", "get",         5,  1, 2, lc_list_get) \
  X("list", "append",      5,  2, 2, lc_list_append) \
  X("list", "set",         5,  3, 3, lc_list_set) \
  X("list", "concat",      5,  4, 2, lc_list_concat) \
  X("list", "slice",       5,  5, 3, lc_list_slice) \
  X("list", "islist",      5,  6, 1, lc_list_islist) \
  X("math", "abs",         6,  0, 1, lc_math_abs) \
  X("net",  "input",       3,  0, 0, lc_net_input) \
  X("net",  "write",       3,  1, 2, lc_net_write) \
  X("net",  "ditch",       3,  2, 1, lc_net_ditch) \
  X("net",  "flush",       3,  3, 1, lc_net_flush) \
  X("net",  "echo",        3,  4, 1, lc_net_echo) \
  X("net",  "maxlines",    3,  5, 0, lc_net_maxlines) \
  X("net",  "connected",   3,  6, 1, lc_net_connected) \
  X("net",  "address",     3,  7, 1, lc_net_address) \
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
  X("str",  "eqcasei",     4, 12, 2, lc_str_eqcasei) \
  X("str",  "valtostr",    4, 13, 1, lc_str_valtostr) \
  X("str",  "replace",     4, 14, 3, lc_str_replace) \
  X("str",  "repeat",      4, 15, 2, lc_str_repeat) \
  X("str",  "padleft",     4, 16, 2, lc_str_padleft) \
  X("str",  "padright",    4, 17, 2, lc_str_padright) \
  X("task", "newgametask", 2,  0, 3, lc_task_newgametask) \
  X("task", "killtask",    2,  1, 1, lc_task_killtask) \
  X("task", "thisid",      2,  2, 0, lc_task_thisid) \
  X("task", "exists",      2,  3, 1, lc_task_exists) \
  X("task", "count",       2,  4, 0, lc_task_count)
