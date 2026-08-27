#include <stddef.h>
#include <stdint.h>
#include <limits.h>
#include <errno.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/stat.h>

#include "compiler/compiler_pipeline.h"
#include "error.h"
#include "interpret.h"
#include "item.h"
#include "libcall_common.h"
#include "libcall_handlers.h"
#include "log.h"
#include "runtime_item_ops.h"
#include "runtime_frame.h"
#include "stack.h"
#include "itemref.h"
#include "list.h"
#include "version.h"
#include "bytecode_format.h"

/* Test-only process-global hook; install/reset it only during quiescent,
 * serial test execution. */
static const char *lc_sys_backup_test_timestamp;

void lc_sys_backup_set_timestamp_for_tests(const char *ts) {
  lc_sys_backup_test_timestamp = ts;
}

static uint8_t *lc_sys_return(RuntimeContext *ctx, uint8_t *nextop,
                              VALUE_t ret) {
  push_stack(ctx->vm->stack, ret);
  return nextop;
}

static uint8_t *lc_sys_return_nil(RuntimeContext *ctx, uint8_t *nextop) {
  return lc_sys_return(ctx, nextop, VALUE_NIL);
}

static uint8_t *lc_sys_return_false(RuntimeContext *ctx, uint8_t *nextop) {
  return lc_sys_return(ctx, nextop, VALUE_FALSE);
}

static uint8_t *lc_sys_persistence_return(RuntimeContext *ctx,
                                           uint8_t *nextop, bool success) {
  if (!ctx || !ctx->vm || !ctx->vm->stack) return nextop;
  return lc_sys_return(ctx, nextop, success ? VALUE_TRUE : VALUE_FALSE);
}

static bool lc_sys_persistence_runtime_ready(RuntimeContext *ctx) {
  // A real libcall invocation always has these channels: interpret() needs the
  // VM stack to dispatch the handler, and the item root carries diagnostics.
  // Keep direct invalid C-level invocations defensive, but do not persist when
  // their result or error cannot be delivered through the normal ABI.
  return ctx && ctx->vm && ctx->vm->stack && itemstore_root(ctx->itemstore);
}

static void lc_sys_set_persistence_error(RuntimeContext *ctx,
                                         const char *operation,
                                         const char *target) {
  ITEM_t *root = ctx ? itemstore_root(ctx->itemstore) : NULL;
  ITEM_t *current_item = ctx ? ctx->current_item : NULL;
  const char *display_target = target && target[0] ? target : "<unconfigured>";
  int needed = snprintf(NULL, 0, "%s failed for '%s'", operation,
                        display_target);
  if (needed < 0) {
    set_error_item(root, ERR_RUNTIME_PERSISTENCE, operation, current_item);
    return;
  }

  size_t detail_size = (size_t)needed + 1u;
  char *detail = malloc(detail_size);
  if (!detail) {
    set_error_item(root, ERR_RUNTIME_PERSISTENCE, operation, current_item);
    return;
  }
  (void)snprintf(detail, detail_size, "%s failed for '%s'", operation,
                 display_target);
  set_error_item(root, ERR_RUNTIME_PERSISTENCE, detail, current_item);
  free(detail);
}

static uint8_t *lc_sys_persist(RuntimeContext *ctx, uint8_t *nextop,
                               const char *operation, const char *target) {
  if (!lc_sys_persistence_runtime_ready(ctx) || !target || target[0] == '\0') {
    lc_sys_set_persistence_error(ctx, operation, target);
    return lc_sys_persistence_return(ctx, nextop, false);
  }

  bool success = itemstore_save_with_options(target, ctx->itemstore,
                                             ctx->itemstore_durability);
  if (!success) lc_sys_set_persistence_error(ctx, operation, target);
  return lc_sys_persistence_return(ctx, nextop, success);
}

static void lc_sys_free_output(OUTPUT_t *out, bool free_bytecode) {
  if (!out) return;
  if (free_bytecode) free(out->bytecode);
  free(out);
}

#define SYS_COMPILE_TMP_PREFIX "__sys_compile_tmp__"

static uint64_t sys_compile_tmp_counter;

static uint64_t next_sys_compile_tmp_counter(uint64_t counter) {
  return counter == UINT64_MAX ? UINT64_C(0) : counter + 1u;
}

static bool next_sys_compile_tmp_name(ITEM_t *root, char *name,
                                      size_t name_size) {
  if (!root || !name || name_size == 0) return false;

  sys_compile_tmp_counter =
      next_sys_compile_tmp_counter(sys_compile_tmp_counter);
  uint64_t first_candidate = sys_compile_tmp_counter;
  do {
    int written = snprintf(name, name_size, SYS_COMPILE_TMP_PREFIX "%llu",
                           (unsigned long long)sys_compile_tmp_counter);
    if (written < 0 || (size_t)written >= name_size) return false;
    if (!find_item(root, name)) return true;
    sys_compile_tmp_counter =
        next_sys_compile_tmp_counter(sys_compile_tmp_counter);
  } while (sys_compile_tmp_counter != first_candidate);

  return false;
}

static bool sys_compile_error_is_nil(ITEM_t *root) {
  ITEM_t *error = root ? find_item(root, "error") : NULL;
  const VALUE_t *value = error ? item_value(error) : NULL;
  return !error || (item_kind(error) == ITEM_value && value &&
                    value->type == VALUE_nil);
}

static uint8_t *lc_sys_compile_fail(RuntimeContext *ctx, uint8_t *nextop,
                                    VALUE_t *source, OUTPUT_t *out,
                                    bool free_bytecode,
                                    CompilerDiagnostic *diag) {
  lc_sys_free_output(out, free_bytecode);
  value_free(source);
  compiler_diag_reset(diag);
  return lc_sys_return_false(ctx, nextop);
}

static char *lc_sys_backup_candidate(const char *filename, const char *timestamp,
                                     uint64_t suffix) {
  int needed;
  if (suffix == 0) {
    needed = snprintf(NULL, 0, "%s_%s", filename, timestamp);
  } else {
    needed = snprintf(NULL, 0, "%s_%s_%llu", filename, timestamp,
                      (unsigned long long)suffix);
  }
  if (needed < 0) return NULL;
  size_t size = (size_t)needed + 1u;
  char *candidate = malloc(size);
  if (!candidate) return NULL;
  if (suffix == 0) {
    (void)snprintf(candidate, size, "%s_%s", filename, timestamp);
  } else {
    (void)snprintf(candidate, size, "%s_%s_%llu", filename, timestamp,
                   (unsigned long long)suffix);
  }
  return candidate;
}

uint8_t *lc_sys_backup(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item) {
  // Save a timestamped itemstore backup and report whether persistence was
  // fully confirmed.  Publication uses atomic link() so a competing actor
  // cannot silently replace a target that appeared between the existence
  // check and publication; collisions retry with the next deterministic
  // suffix.
  (void)item;

  if (!lc_sys_persistence_runtime_ready(ctx) || !ctx->itemstore_filename ||
      ctx->itemstore_filename[0] == '\0') {
    lc_sys_set_persistence_error(ctx, "sys.backup",
                                 ctx ? ctx->itemstore_filename : NULL);
    return lc_sys_persistence_return(ctx, nextop, false);
  }

  char timestamp[64];
  if (lc_sys_backup_test_timestamp) {
    size_t ts_len = strlen(lc_sys_backup_test_timestamp);
    if (ts_len >= sizeof(timestamp)) {
      lc_sys_set_persistence_error(ctx, "sys.backup",
                                   ctx->itemstore_filename);
      return lc_sys_persistence_return(ctx, nextop, false);
    }
    memcpy(timestamp, lc_sys_backup_test_timestamp, ts_len + 1u);
  } else {
    time_t now = time(NULL);
    struct tm *tm_now = localtime(&now);
    if (!tm_now || strftime(timestamp, sizeof(timestamp), "%Y%m%d-%H%M%S",
                            tm_now) == 0) {
      lc_sys_set_persistence_error(ctx, "sys.backup",
                                   ctx->itemstore_filename);
      return lc_sys_persistence_return(ctx, nextop, false);
    }
  }

  for (uint64_t suffix = 0; suffix < UINT64_MAX; suffix++) {
    char *backupfile = lc_sys_backup_candidate(ctx->itemstore_filename,
                                                timestamp, suffix);
    if (!backupfile) {
      lc_sys_set_persistence_error(ctx, "sys.backup",
                                   ctx->itemstore_filename);
      return lc_sys_persistence_return(ctx, nextop, false);
    }

    /* Atomically probe existence: lstat for the fast path, then link() as
     * the atomic guard.  If the target appeared between the two, link()
     * fails with EEXIST and we retry the next suffix. */
    errno = 0;
    struct stat candidate_stat;
    if (lstat(backupfile, &candidate_stat) == 0) {
      /* Occupied: file, directory, or symlink (including dangling). */
      free(backupfile);
      continue;
    }
    if (errno != ENOENT) {
      free(backupfile);
      lc_sys_set_persistence_error(ctx, "sys.backup",
                                   ctx->itemstore_filename);
      return lc_sys_persistence_return(ctx, nextop, false);
    }

    ITEMSTORE_SAVE_RESULT_e result = itemstore_save_no_replace(
        backupfile, ctx->itemstore, ctx->itemstore_durability);
    if (result == ITEMSTORE_SAVE_SUCCESS) {
      free(backupfile);
      return lc_sys_persistence_return(ctx, nextop, true);
    }
    if (result != ITEMSTORE_SAVE_TARGET_EXISTS) {
      lc_sys_set_persistence_error(ctx, "sys.backup", backupfile);
      free(backupfile);
      return lc_sys_persistence_return(ctx, nextop, false);
    }
    free(backupfile);
    /* Collision: another actor created the target.  Retry next suffix. */
  }

  lc_sys_set_persistence_error(ctx, "sys.backup",
                               ctx->itemstore_filename);
  return lc_sys_persistence_return(ctx, nextop, false);
}

uint8_t *lc_sys_save(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item) {
  // Synchronize the current item tree to the configured primary itemstore.
  (void)item;
  return lc_sys_persist(ctx, nextop, "sys.save",
                        ctx ? ctx->itemstore_filename : NULL);
}

static VALUE_t lc_sys_string_copy(const char *text) {
  if (!text) return VALUE_NIL;
  char *copy = strdup(text);
  if (!copy) return VALUE_NIL;
  return (VALUE_t){VALUE_str, {.s = copy}};
}

static int64_t lc_sys_count_value(size_t count) {
  if ((uintmax_t)count > (uintmax_t)INT64_MAX) return INT64_MAX;
  return (int64_t)count;
}

static const char *lc_sys_item_type_name(const ITEM_t *target) {
  if (!target) return NULL;
  if (item_kind(target) == ITEM_code) return "code";
  if (item_kind(target) != ITEM_value) return NULL;
  const VALUE_t *value = item_value(target);
  if (!value) return NULL;
  switch (value->type) {
    case VALUE_nil: return "nil";
    case VALUE_bool: return "bool";
    case VALUE_int: return "int";
    case VALUE_float: return "float";
    case VALUE_str: return "string";
    case VALUE_itemref: return "itemref";
    case VALUE_list: return "list";
  }
  return NULL;
}

/* Resolve the shared item-name contract used by sys introspection calls. */
static bool lc_sys_resolve_itemname(RuntimeContext *ctx, ITEM_t *item,
                                    const VALUE_t *name, char *fullname) {
  if (!name || !fullname) return false;
  if (name->type == VALUE_str) {
    return canonicalize_itemname(name->s,
                                 item ? item : (ctx ? ctx->current_item : NULL),
                                 fullname);
  }
  if (name->type == VALUE_itemref) {
    const char *path = sin_itemref_path(name->itemref);
    return path && canonicalize_itemname(path, NULL, fullname);
  }
  return false;
}

static void lc_sys_report_strict_contract(RuntimeContext *ctx,
                                          const char *detail) {
  if (!ctx || !ctx->strict_runtime_contracts) return;
  logerr("Runtime contract violation: %s.\n", detail ? detail : "<no detail>");
  set_error_item(itemstore_root(ctx->itemstore), ERR_RUNTIME_INVALIDARGS,
                 detail, ctx->current_item);
}

int64_t lc_sys_wall_milliseconds(int64_t seconds, int64_t microseconds) {
  int64_t fraction = microseconds / INT64_C(1000);
  int64_t fraction_seconds = fraction / INT64_C(1000);
  int64_t fraction_milliseconds = fraction % INT64_C(1000);
  if (fraction_milliseconds < 0) {
    fraction_milliseconds += INT64_C(1000);
    fraction_seconds--;
  }

  if (fraction_seconds > 0 && seconds > INT64_MAX - fraction_seconds) {
    return INT64_MAX;
  }
  if (fraction_seconds < 0 && seconds < INT64_MIN - fraction_seconds) {
    return INT64_MIN;
  }
  int64_t normalized_seconds = seconds + fraction_seconds;

  const int64_t maximum_seconds = INT64_MAX / INT64_C(1000);
  if (normalized_seconds > maximum_seconds) return INT64_MAX;
  const int64_t minimum_seconds = INT64_MIN / INT64_C(1000);
  if (normalized_seconds < minimum_seconds) {
    const int64_t adjacent_second = minimum_seconds - INT64_C(1);
    const int64_t minimum_fraction =
        INT64_C(1000) + (INT64_MIN % INT64_C(1000));
    if (normalized_seconds != adjacent_second ||
        fraction_milliseconds < minimum_fraction) {
      return INT64_MIN;
    }
    return INT64_MIN + (fraction_milliseconds - minimum_fraction);
  }

  int64_t milliseconds = normalized_seconds * INT64_C(1000);
  if (normalized_seconds == maximum_seconds &&
      fraction_milliseconds > INT64_MAX - milliseconds) {
    return INT64_MAX;
  }
  return milliseconds + fraction_milliseconds;
}

uint8_t *lc_sys_thisitem(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item) {
  (void)item;
  if (!ctx || !ctx->current_item) return lc_sys_return_nil(ctx, nextop);

  char name[MAX_ITEM_NAME] = {0};
  get_itemname(ctx->current_item, name);
  return lc_sys_return(ctx, nextop, lc_sys_string_copy(name));
}

uint8_t *lc_sys_parentitem(RuntimeContext *ctx, uint8_t *nextop,
                           ITEM_t *item) {
  (void)item;
  ITEM_t *parent = ctx && ctx->current_item ? item_parent(ctx->current_item) : NULL;
  if (!parent || !item_parent(parent)) {
    return lc_sys_return_nil(ctx, nextop);
  }

  char name[MAX_ITEM_NAME] = {0};
  get_itemname(parent, name);
  return lc_sys_return(ctx, nextop, lc_sys_string_copy(name));
}

uint8_t *lc_sys_calleritem(RuntimeContext *ctx, uint8_t *nextop,
                           ITEM_t *item) {
  (void)item;
  if (!ctx || !ctx->vm || !ctx->vm->stack || !ctx->vm->callstack) {
    return nextop;
  }

  ITEM_t *caller = ctx->invocation_caller_item;
  if (size_callstack(ctx->vm->callstack) >
      ctx->invocation_callstack_floor) {
    caller = ctx->vm->callstack->entry[ctx->vm->callstack->current].item;
  }
  if (!caller) return lc_sys_return_nil(ctx, nextop);

  char name[MAX_ITEM_NAME] = {0};
  get_itemname(caller, name);
  return lc_sys_return(ctx, nextop, lc_sys_string_copy(name));
}

uint8_t *lc_sys_itemtype(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item) {
  (void)item;
  VALUE_t itemname = pop_stack(ctx->vm->stack);
  if (itemname.type != VALUE_str && itemname.type != VALUE_itemref) {
    value_free(&itemname);
    return lc_invalid_args_detail_return(ctx, nextop, VALUE_NIL,
        "sys.itemtype item name must be a string or item reference");
  }

  VALUE_t result = VALUE_NIL;
  char fullname[MAX_ITEM_NAME];
  if (lc_sys_resolve_itemname(ctx, item, &itemname, fullname)) {
    ITEM_t *target = find_item(itemstore_root(ctx->itemstore), fullname);
    result = lc_sys_string_copy(lc_sys_item_type_name(target));
  }
  value_free(&itemname);
  return lc_sys_return(ctx, nextop, result);
}

uint8_t *lc_sys_childcount(RuntimeContext *ctx, uint8_t *nextop,
                           ITEM_t *item) {
  (void)item;
  VALUE_t itemname = pop_stack(ctx->vm->stack);
  if (itemname.type != VALUE_str && itemname.type != VALUE_itemref) {
    value_free(&itemname);
    return lc_invalid_args_detail_return(ctx, nextop, VALUE_NIL,
        "sys.childcount item name must be a string or item reference");
  }

  VALUE_t result = VALUE_NIL;
  char fullname[MAX_ITEM_NAME];
  if (lc_sys_resolve_itemname(ctx, item, &itemname, fullname)) {
    ITEM_t *target = find_item(itemstore_root(ctx->itemstore), fullname);
    if (target) {
      result = (VALUE_t){VALUE_int,
                         {.i = lc_sys_count_value(item_child_count(target))}};
    }
  }
  value_free(&itemname);
  return lc_sys_return(ctx, nextop, result);
}

uint8_t *lc_sys_paramcount(RuntimeContext *ctx, uint8_t *nextop,
                           ITEM_t *item) {
  (void)item;
  if (!ctx || !ctx->vm || !ctx->vm->stack) return nextop;

  VALUE_t itemname = pop_stack(ctx->vm->stack);
  if (itemname.type != VALUE_str && itemname.type != VALUE_itemref) {
    value_free(&itemname);
    return lc_invalid_args_detail_return(ctx, nextop, VALUE_NIL,
        "sys.paramcount item name must be a string or item reference");
  }

  VALUE_t result = VALUE_NIL;
  char fullname[MAX_ITEM_NAME];
  if (itemstore_root(ctx->itemstore) &&
      lc_sys_resolve_itemname(ctx, item, &itemname, fullname)) {
    ITEM_t *target = find_item(itemstore_root(ctx->itemstore), fullname);
    const uint8_t *bytecode = target ? item_bytecode(target) : NULL;
    uint32_t bytecode_len = target ? item_bytecode_length(target) : 0;
    BC_FormatHeader header;
    if (target && item_kind(target) == ITEM_code && bytecode &&
        bc_decode_header(bytecode, bytecode_len, &header) == BC_FORMAT_OK) {
      result = (VALUE_t){VALUE_int, {.i = (int64_t)header.params}};
    }
  }
  value_free(&itemname);
  return lc_sys_return(ctx, nextop, result);
}

uint8_t *lc_sys_source(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item) {
  (void)item;
  if (!ctx || !ctx->vm || !ctx->vm->stack) return nextop;

  VALUE_t itemname = pop_stack(ctx->vm->stack);
  if (itemname.type != VALUE_str && itemname.type != VALUE_itemref) {
    value_free(&itemname);
    return lc_invalid_args_detail_return(ctx, nextop, VALUE_NIL,
        "sys.source item name must be a string or item reference");
  }

  char fullname[MAX_ITEM_NAME];
  if (!itemstore_root(ctx->itemstore) ||
      !lc_sys_resolve_itemname(ctx, item, &itemname, fullname)) {
    value_free(&itemname);
    return lc_sys_return_nil(ctx, nextop);
  }
  ITEM_t *target = find_item(itemstore_root(ctx->itemstore), fullname);
  if (!target || item_kind(target) != ITEM_code) {
    value_free(&itemname);
    return lc_sys_return_nil(ctx, nextop);
  }

  char read_detail[512];
  char *source = read_itemsource_in_srcroot(target, ctx->srcroot, read_detail,
                                            sizeof(read_detail));
  value_free(&itemname);
  if (source) {
    return lc_sys_return(ctx, nextop,
                         (VALUE_t){VALUE_str, {.s = source}});
  }

  char error_detail[1024];
  (void)snprintf(error_detail, sizeof(error_detail), "sys.source{%s}: %s",
                 fullname, read_detail[0] ? read_detail : "source read failed");
  set_error_item(itemstore_root(ctx->itemstore), ERR_RUNTIME_SOURCE, error_detail,
                 ctx->current_item);
  return lc_sys_return(ctx, nextop, lc_sys_string_copy(""));
}

static ITEM_t *lc_sys_reference_target(RuntimeContext *ctx, ITEM_t *item,
                                       const VALUE_t *ref, char *fullname) {
  if (!ctx || !ref || ref->type != VALUE_itemref ||
      !lc_sys_resolve_itemname(ctx, item, ref, fullname)) return NULL;
  return find_item(itemstore_root(ctx->itemstore), fullname);
}

static bool lc_sys_schedule_code_call(RuntimeContext *ctx, uint8_t *nextop,
                                       ITEM_t *target, size_t supplied) {
  if (!ctx || !target || item_kind(target) != ITEM_code) return false;
  const uint8_t *bytecode = item_bytecode(target);
  uint32_t bytecode_len = item_bytecode_length(target);
  BC_FormatHeader header;
  if (!bytecode || bc_decode_header(bytecode, bytecode_len, &header) != BC_FORMAT_OK) return false;
  return runtime_frame_prepare_call(
      ctx, ctx->current_item, nextop, target, supplied, header.locals,
      header.params, (uint8_t *)ctx->decoder.frame_start,
      (uint8_t *)ctx->decoder.frame_end, NULL);
}

uint8_t *lc_sys_itemref(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item) {
  (void)item;
  VALUE_t name = pop_stack(ctx->vm->stack);
  if (name.type != VALUE_str) {
    value_free(&name);
    return lc_invalid_args_detail_return(ctx, nextop, VALUE_NIL,
        "sys.itemref name must be a string");
  }
  char fullname[MAX_ITEM_NAME];
  VALUE_t result = VALUE_NIL;
  if (lc_sys_resolve_itemname(ctx, item, &name, fullname)) {
    SIN_ITEMREF_t *ref = sin_itemref_create(fullname);
    if (ref) result = (VALUE_t){VALUE_itemref, {.itemref = ref}};
  }
  value_free(&name);
  return lc_sys_return(ctx, nextop, result);
}

uint8_t *lc_sys_itemname(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item) {
  (void)item;
  VALUE_t ref = pop_stack(ctx->vm->stack);
  if (ref.type != VALUE_itemref) {
    value_free(&ref);
    return lc_invalid_args_detail_return(ctx, nextop, VALUE_NIL,
        "sys.itemname reference must be an item reference");
  }
  const char *path = sin_itemref_path(ref.itemref);
  VALUE_t result = path ? lc_sys_string_copy(path) : VALUE_NIL;
  value_free(&ref);
  return lc_sys_return(ctx, nextop, result);
}

uint8_t *lc_sys_fetch(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item) {
  (void)item;
  VALUE_t ref = pop_stack(ctx->vm->stack);
  if (ref.type != VALUE_itemref) {
    value_free(&ref);
    return lc_invalid_args_detail_return(ctx, nextop, VALUE_NIL,
        "sys.fetch reference must be an item reference");
  }
  char fullname[MAX_ITEM_NAME];
  ITEM_t *target = lc_sys_reference_target(ctx, item, &ref, fullname);
  VALUE_t result = VALUE_NIL;
  if (target && item_kind(target) == ITEM_value) {
    const VALUE_t *stored = item_value(target);
    if (stored) (void)value_clone_fallible(stored, &result);
  } else if (target && item_kind(target) == ITEM_code) {
    if (lc_sys_schedule_code_call(ctx, nextop, target, 0u)) {
      value_free(&ref);
      return NULL;
    }
  }
  value_free(&ref);
  return lc_sys_return(ctx, nextop, result);
}

uint8_t *lc_sys_call(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item) {
  (void)item;
  VALUE_t arguments = pop_stack(ctx->vm->stack);
  VALUE_t ref = pop_stack(ctx->vm->stack);
  if (ref.type != VALUE_itemref || arguments.type != VALUE_list || !arguments.list) {
    VALUE_t bad[] = {ref, arguments};
    lc_cleanup_values(bad, 2);
    return lc_invalid_args_detail_return(ctx, nextop, VALUE_NIL,
        "sys.call requires an item reference and a list");
  }
  char fullname[MAX_ITEM_NAME];
  ITEM_t *target = lc_sys_reference_target(ctx, item, &ref, fullname);
  size_t count = sin_list_count(arguments.list);
  bool prepared = target && item_kind(target) == ITEM_code;
  VALUE_t result = VALUE_NIL;
  if (prepared) {
    /* List order is preserved by pushing each element from left to right. */
    size_t pushed = 0;
    const uint8_t *bytecode = item_bytecode(target);
    BC_FormatHeader header;
    bool header_ok = bytecode &&
        bc_decode_header(bytecode, item_bytecode_length(target), &header) == BC_FORMAT_OK;
    size_t effective = 0u;
    prepared = header_ok && runtime_frame_preflight_call(
        ctx, count, header.locals, header.params, &effective);
    while (prepared && pushed < effective) {
      const VALUE_t *source = sin_list_get(arguments.list, pushed);
      VALUE_t clone = VALUE_NIL;
      if (!source || !value_clone_fallible(source, &clone)) {
        while (pushed > 0u) {
          VALUE_t dropped = pop_stack(ctx->vm->stack);
          value_free(&dropped);
          pushed--;
        }
        prepared = false;
        break;
      }
      push_stack(ctx->vm->stack, clone);
      pushed++;
    }
    if (prepared) {
      if (count > effective) {
        lc_sys_report_strict_contract(ctx,
            "sys.call discarded extra argument for target item");
      }
      prepared = lc_sys_schedule_code_call(ctx, nextop, target, effective);
    }
    if (!prepared) {
      while (pushed > 0u) {
        VALUE_t dropped = pop_stack(ctx->vm->stack);
        value_free(&dropped);
        pushed--;
      }
    } else {
      value_free(&ref);
      value_free(&arguments);
      return NULL;
    }
  }
  value_free(&ref);
  value_free(&arguments);
  return lc_sys_return(ctx, nextop, result);
}

uint8_t *lc_sys_rootcount(RuntimeContext *ctx, uint8_t *nextop,
                          ITEM_t *item) {
  (void)item;
  int64_t count = ctx && itemstore_root(ctx->itemstore)
      ? lc_sys_count_value(item_child_count(itemstore_root(ctx->itemstore))) : 0;
  return lc_sys_return(ctx, nextop,
                       (VALUE_t){VALUE_int, {.i = count}});
}

uint8_t *lc_sys_version(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item) {
  (void)item;
  return lc_sys_return(ctx, nextop, lc_sys_string_copy(SINVERSION));
}

uint8_t *lc_sys_now(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item) {
  (void)item;
  uv_timeval64_t wall = {0};
  int status = uv_gettimeofday(&wall);
  if (status != 0) {
    logerr("sys.now failed to read wall clock: %s.\n", uv_strerror(status));
    return lc_sys_return(ctx, nextop,
                         (VALUE_t){VALUE_int, {.i = 0}});
  }
  int64_t milliseconds = lc_sys_wall_milliseconds(wall.tv_sec, wall.tv_usec);
  return lc_sys_return(ctx, nextop,
                       (VALUE_t){VALUE_int, {.i = milliseconds}});
}

uint8_t *lc_sys_monotime(RuntimeContext *ctx, uint8_t *nextop,
                         ITEM_t *item) {
  (void)item;
  uint64_t milliseconds = uv_hrtime() / UINT64_C(1000000);
  int64_t result = milliseconds > (uint64_t)INT64_MAX
      ? INT64_MAX : (int64_t)milliseconds;
  return lc_sys_return(ctx, nextop,
                       (VALUE_t){VALUE_int, {.i = result}});
}

uint8_t *lc_sys_log(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item) {
  // Consume one value, write its text representation to the system log, and
  // push nil. Strings are freed after logging.
  (void)item;

  VALUE_t val = pop_stack(ctx->vm->stack);
  char *rendered = NULL;
  size_t text_length = 0;
  VALUE_text_result_e result = value_render_text(
      &val, VALUE_TEXT_NIL_OMIT, &rendered, &text_length);
  switch (result) {
    case VALUE_TEXT_OK:
      logmsg("%.*s", (int)text_length, rendered);
      break;
    case VALUE_TEXT_NIL:
      break;
    case VALUE_TEXT_UNKNOWN_TYPE:
      logmsg("Sys.log called with unknown value type.\n");
      break;
    case VALUE_TEXT_BUFFER_TOO_SMALL:
    case VALUE_TEXT_FORMAT_ERROR:
    case VALUE_TEXT_ALLOCATION_ERROR:
    case VALUE_TEXT_OUTPUT_LIMIT:
    case VALUE_TEXT_MALFORMED:
      logmsg("<value-render-error>");
      break;
  }
  free(rendered);
  FREE_STR(val);
  return lc_sys_return_nil(ctx, nextop);
}

uint8_t *lc_sys_shutdown(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item) {
  // Stop the event loop, mark shutdown as safe, and push nil.
  (void)item;

  logmsg("Sys.shutdown called.  Shutting down.\n");
  (*ctx->safe_shutdown) = true;
  if (ctx->shutdown_requested) (*ctx->shutdown_requested) = true;
  uv_stop(ctx->loop);
  return lc_sys_return_nil(ctx, nextop);
}

uint8_t *lc_sys_abort(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item) {
  // Stop the event loop, mark shutdown as unsafe, and push nil.
  (void)item;

  logmsg("Sys.abort called.  Immediate (and messy) shutdown.\n");
  (*ctx->safe_shutdown) = false;
  if (ctx->shutdown_requested) (*ctx->shutdown_requested) = true;
  uv_stop(ctx->loop);
  return lc_sys_return_nil(ctx, nextop);
}

uint8_t *lc_sys_compile(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item) {
  // Consume a source string, compile it into a temporary code item, execute it,
  // discard values produced by that execution, and push true/false.
  (void)item;

  VALUE_t val = pop_stack(ctx->vm->stack);

  if (!lc_value_is_type(val, VALUE_str)) {
    logverbose("Sys.compile called with non-string value.\n");
    FREE_STR(val);
    return lc_invalid_args_detail_return(ctx, nextop, VALUE_FALSE,
        "sys.compile source must be a string; non-string values, including floats, are invalid");
  }

  CompilerDiagnostic diag;
  compiler_diag_init(&diag);
  OUTPUT_t *out = NULL;
  char tmpname[MAX_ITEM_NAME];

  // Compile source -> bytecode
  ParseInput compile_input = {val.s, strlen(val.s), "<memory>"};
  int8_t result = compile_parse_input_to_bytecode_diag_with_node_limit(
      &compile_input, ctx ? ctx->compiler_ast_node_limit : 0, &out, &diag);

  if (result != 0 || !out || !out->bytecode) {
    if (result == 0) {
      compiler_diag_reset(&diag);
      compiler_diag_set(&diag, ERR_COMP_UNKNOWN, DIAG_PHASE_COMPILE,
          "compile: missing bytecode output");
      compiler_diag_set_source_name(&diag, "<memory>");
      compiler_diag_set_location(&diag, 1, 1, 1);
      compiler_diag_set_excerpt(&diag, val.s ? val.s : "");
    }
    set_compiler_error_item(ctx ? itemstore_root(ctx->itemstore) : NULL, &diag);
    return lc_sys_compile_fail(ctx, nextop, &val, out, true, &diag);
  }

  ptrdiff_t raw_len = out->nextbyte - out->bytecode;
  if (raw_len < 0 || (uintmax_t)raw_len > UINT32_MAX) {
    set_error_item(ctx ? itemstore_root(ctx->itemstore) : NULL, ERR_RUNTIME_BYTECODE,
        "Sys.compile bytecode output length is out of range.",
        ctx ? ctx->current_item : NULL);
    return lc_sys_compile_fail(ctx, nextop, &val, out, true, &diag);
  }
  uint32_t len = (uint32_t)raw_len;
  clear_error_item(ctx ? itemstore_root(ctx->itemstore) : NULL);

  if (!next_sys_compile_tmp_name(ctx ? itemstore_root(ctx->itemstore) : NULL, tmpname,
                                 sizeof(tmpname))) {
    set_error_item(ctx ? itemstore_root(ctx->itemstore) : NULL, ERR_RUNTIME_INTERNAL,
        "Sys.compile temporary item name generation failed.",
        ctx ? ctx->current_item : NULL);
    return lc_sys_compile_fail(ctx, nextop, &val, out, true, &diag);
  }

  ITEM_MUTATION_RESULT_t mutation =
      item_set_code(itemstore_root(ctx->itemstore), tmpname, len,
                    out->bytecode);

  if (!item_mutation_succeeded(mutation)) {
    set_error_item(ctx ? itemstore_root(ctx->itemstore) : NULL, ERR_RUNTIME_INTERNAL,
        "Sys.compile temporary code item could not be created.",
        ctx ? ctx->current_item : NULL);
    return lc_sys_compile_fail(ctx, nextop, &val, out, true, &diag);
  }
  ITEM_t *tmpitem = mutation.item;

  // Preserve the caller frame below the pre-call depth; discard only values
  // produced by the nested interpret() run.
  int32_t stack_top_before_interpret = ctx->vm->stack->current;
  VALUE_t run_result = interpret(ctx, tmpitem);
  value_free(&run_result);
  while (ctx->vm->stack->current > stack_top_before_interpret) {
    VALUE_t dropped = pop_stack(ctx->vm->stack);
    value_free(&dropped);
  }

  (void)item_delete(itemstore_root(ctx->itemstore), tmpname);
  bool error_is_nil = sys_compile_error_is_nil(itemstore_root(ctx->itemstore));
  bool success = !ctx->interrupted && error_is_nil;
  if (ctx->interrupted && error_is_nil) {
    set_error_item(itemstore_root(ctx->itemstore), ERR_RUNTIME_SIGUSR1, NULL,
                   ctx->current_item);
  } else if (success) {
    clear_error_item(itemstore_root(ctx->itemstore));
  }

  lc_sys_free_output(out, false);
  value_free(&val);
  compiler_diag_reset(&diag);

  return lc_sys_return(ctx, nextop, success ? VALUE_TRUE : VALUE_FALSE);
}

uint8_t *lc_sys_exists(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item) {
  (void)item;
  VALUE_t itemname = pop_stack(ctx->vm->stack);
  if (itemname.type != VALUE_str && itemname.type != VALUE_itemref) {
    value_free(&itemname);
    return lc_invalid_args_detail_return(ctx, nextop, VALUE_FALSE,
        "sys.exists item name must be a string or item reference");
  }

  char fullname[MAX_ITEM_NAME];
  bool exists = lc_sys_resolve_itemname(ctx, item, &itemname, fullname) &&
      find_item(itemstore_root(ctx->itemstore), fullname) != NULL;
  value_free(&itemname);
  return lc_sys_return(ctx, nextop, exists ? VALUE_TRUE : VALUE_FALSE);
}

uint8_t *lc_sys_delete(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item) {
  VALUE_t itemname = pop_stack(ctx->vm->stack);
  if (itemname.type != VALUE_str && itemname.type != VALUE_itemref) {
    value_free(&itemname);
    return lc_invalid_args_detail_return(ctx, nextop, VALUE_NIL,
        "sys.delete item name must be a string or item reference");
  }

  char fullname[MAX_ITEM_NAME];
  ITEM_MUTATION_STATUS_e status = ITEM_MUTATION_INVALID_NAME;
  if (lc_sys_resolve_itemname(ctx, item, &itemname, fullname)) {
    if (runtime_reject_error_namespace_mutation(
            itemstore_root(ctx->itemstore), fullname, "sys.delete",
            ctx->current_item)) {
      value_free(&itemname);
      return lc_sys_return_false(ctx, nextop);
    }
    status = item_delete(itemstore_root(ctx->itemstore), fullname).status;
    if (status == ITEM_MUTATION_IN_USE) {
      char detail[MAX_ITEM_NAME + 128u];
      int written = snprintf(
          detail, sizeof(detail),
          "sys.delete refused for '%s': target or a descendant is "
          "execution-pinned.",
          fullname);
      if (written < 0 || (size_t)written >= sizeof(detail)) {
        set_error_item(itemstore_root(ctx->itemstore), ERR_RUNTIME_INUSE,
                       "sys.delete target or descendant is execution-pinned.",
                       ctx->current_item);
      } else {
        set_error_item(itemstore_root(ctx->itemstore), ERR_RUNTIME_INUSE,
                       detail, ctx->current_item);
      }
    }
  }
  value_free(&itemname);
  return lc_sys_return(ctx, nextop,
                       status == ITEM_MUTATION_DELETED ? VALUE_TRUE : VALUE_FALSE);
}

uint8_t *lc_sys_nthname(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item) {
  (void)item;
  VALUE_t index = pop_stack(ctx->vm->stack);
  VALUE_t itemname = pop_stack(ctx->vm->stack);
  if ((itemname.type != VALUE_str && itemname.type != VALUE_itemref) ||
      !lc_value_is_type(index, VALUE_int) || index.i < 0) {
    value_free(&index);
    value_free(&itemname);
    return lc_invalid_args_detail_return(ctx, nextop, VALUE_NIL,
        "sys.nthname requires a string or item reference and non-negative integer index");
  }

  VALUE_t result = VALUE_NIL;
  char fullname[MAX_ITEM_NAME];
  if (lc_sys_resolve_itemname(ctx, item, &itemname, fullname)) {
    ITEM_t *parent = find_item(itemstore_root(ctx->itemstore), fullname);
    if (parent) {
      ITEM_t *child = item_child_at(parent, (size_t)index.i);
      if (child) {
        result.type = VALUE_str;
        result.s = strdup(item_layer_name(child));
        if (!result.s) result = VALUE_NIL;
      }
    }
  }

  value_free(&index);
  value_free(&itemname);
  return lc_sys_return(ctx, nextop, result);
}

uint8_t *lc_sys_rootname(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item) {
  (void)item;

  VALUE_t index = pop_stack(ctx->vm->stack);
  if (!lc_value_is_type(index, VALUE_int) || index.i < 0) {
    value_free(&index);
    return lc_invalid_args_detail_return(ctx, nextop, VALUE_NIL,
        "sys.rootname index must be a non-negative integer");
  }

  VALUE_t result = VALUE_NIL;
  ITEM_t *child = item_child_at(itemstore_root(ctx->itemstore), (size_t)index.i);
  if (child) {
    result.type = VALUE_str;
    result.s = strdup(item_layer_name(child));
    if (!result.s) result = VALUE_NIL;
  }

  value_free(&index);
  return lc_sys_return(ctx, nextop, result);
}
