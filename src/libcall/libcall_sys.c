#include <stddef.h>
#include <stdint.h>
#include <time.h>
#include <string.h>
#include <stdio.h>

#include "compiler/compiler_pipeline.h"
#include "error.h"
#include "interpret.h"
#include "item.h"
#include "libcall_common.h"
#include "libcall_handlers.h"
#include "log.h"
#include "runtime_item_ops.h"
#include "stack.h"

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

static void lc_sys_free_output(OUTPUT_t *out, bool free_bytecode) {
  if (!out) return;
  if (free_bytecode) free(out->bytecode);
  free(out);
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

uint8_t *lc_sys_backup(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item) {
  // Save a timestamped itemstore backup and push nil. Backup I/O failures are
  // logged but are not exposed through the return value.
  (void)item;

  char timestamp[64];
  time_t now = time(NULL);
  struct tm *tm_now = localtime(&now);
  strftime(timestamp, sizeof(timestamp), "%Y%m%d-%H%M%S", tm_now);
  size_t filename_len = strlen(ctx->itemstore_filename);
  size_t timestamp_len = strlen(timestamp);
  char backupfile[filename_len + timestamp_len + 2];
  snprintf(backupfile, sizeof(backupfile), "%s_%s", ctx->itemstore_filename,
                                                                timestamp);
  if (!save_itemstore(backupfile, ctx->itemroot)) {
    logerr("sys.backup failed to persist itemstore backup '%s'.\n",
           backupfile);
  }
  return lc_sys_return_nil(ctx, nextop);
}

uint8_t *lc_sys_log(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item) {
  // Consume one value, write its text representation to the system log, and
  // push nil. Strings are freed after logging.
  (void)item;

  VALUE_t val = pop_stack(ctx->vm->stack);
  char buffer[VALUE_PLAIN_TEXT_BUFFER_SIZE];
  const char *text = NULL;
  size_t text_length = 0;
  VALUE_text_result_e result = value_plain_text(
      &val, VALUE_TEXT_NIL_OMIT, buffer, sizeof(buffer), &text, &text_length);
  (void)text_length;
  switch (result) {
    case VALUE_TEXT_OK:
      logmsg("%s", text);
      break;
    case VALUE_TEXT_NIL:
      break;
    case VALUE_TEXT_UNKNOWN_TYPE:
      logmsg("Sys.log called with unknown value type.\n");
      break;
    case VALUE_TEXT_BUFFER_TOO_SMALL:
    case VALUE_TEXT_FORMAT_ERROR:
      logmsg("<float-format-error>");
      break;
  }
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
  static uint64_t tmpname_counter = 0;

  // Compile source -> bytecode
  int8_t result = compile_source_to_bytecode_diag(val.s, strlen(val.s), &out, &diag);

  if (result != 0 || !out || !out->bytecode) {
    if (result == 0) {
      compiler_diag_reset(&diag);
      compiler_diag_set(&diag, ERR_COMP_UNKNOWN, DIAG_PHASE_COMPILE,
          "compile: missing bytecode output");
      compiler_diag_set_source_name(&diag, "<memory>");
      compiler_diag_set_location(&diag, 1, 1, 1);
      compiler_diag_set_excerpt(&diag, val.s ? val.s : "");
    }
    set_compiler_error_item(ctx ? ctx->itemroot : NULL, &diag);
    return lc_sys_compile_fail(ctx, nextop, &val, out, true, &diag);
  }

  int namelen = snprintf(tmpname, sizeof(tmpname),
      "__sys_compile_tmp__%llu", (unsigned long long)++tmpname_counter);
  if (namelen < 0 || namelen >= (int)sizeof(tmpname)) {
    set_error_item(ctx ? ctx->itemroot : NULL, ERR_RUNTIME_INTERNAL,
        "Sys.compile temporary item name generation failed.",
        ctx ? ctx->current_item : NULL);
    return lc_sys_compile_fail(ctx, nextop, &val, out, true, &diag);
  }

  ptrdiff_t raw_len = out->nextbyte - out->bytecode;
  if (raw_len < 0 || (uintmax_t)raw_len > UINT32_MAX) {
    set_error_item(ctx ? ctx->itemroot : NULL, ERR_RUNTIME_BYTECODE,
        "Sys.compile bytecode output length is out of range.",
        ctx ? ctx->current_item : NULL);
    return lc_sys_compile_fail(ctx, nextop, &val, out, true, &diag);
  }
  uint32_t len = (uint32_t)raw_len;
  ITEM_t *tmpitem = insert_code_item(ctx->itemroot, tmpname, len, out->bytecode);

  if (!tmpitem) {
    set_error_item(ctx ? ctx->itemroot : NULL, ERR_RUNTIME_INTERNAL,
        "Sys.compile temporary code item could not be created.",
        ctx ? ctx->current_item : NULL);
    return lc_sys_compile_fail(ctx, nextop, &val, out, true, &diag);
  }

  // Preserve the caller frame below the pre-call depth; discard only values
  // produced by the nested interpret() run.
  int32_t stack_top_before_interpret = ctx->vm->stack->current;
  VALUE_t run_result = interpret(ctx, tmpitem);
  value_free(&run_result);
  while (ctx->vm->stack->current > stack_top_before_interpret) {
    VALUE_t dropped = pop_stack(ctx->vm->stack);
    value_free(&dropped);
  }

  delete_item(ctx->itemroot, tmpname);
  clear_error_item(ctx ? ctx->itemroot : NULL);

  lc_sys_free_output(out, false);
  value_free(&val);
  compiler_diag_reset(&diag);

  return lc_sys_return(ctx, nextop, VALUE_TRUE);
}

uint8_t *lc_sys_exists(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item) {
  VALUE_t itemname = pop_stack(ctx->vm->stack);
  if (!lc_value_is_type(itemname, VALUE_str)) {
    value_free(&itemname);
    return lc_invalid_args_detail_return(ctx, nextop, VALUE_FALSE,
        "sys.exists item name must be a string");
  }

  char fullname[MAX_ITEM_NAME];
  bool exists = canonicalize_itemname(itemname.s, item, fullname) &&
      find_item(ctx->itemroot, fullname) != NULL;
  value_free(&itemname);
  return lc_sys_return(ctx, nextop, exists ? VALUE_TRUE : VALUE_FALSE);
}

uint8_t *lc_sys_delete(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item) {
  VALUE_t itemname = pop_stack(ctx->vm->stack);
  if (!lc_value_is_type(itemname, VALUE_str)) {
    value_free(&itemname);
    return lc_invalid_args_detail_return(ctx, nextop, VALUE_NIL,
        "sys.delete item name must be a string");
  }

  char fullname[MAX_ITEM_NAME];
  if (canonicalize_itemname(itemname.s, item, fullname)) {
    delete_item(ctx->itemroot, fullname);
  }
  value_free(&itemname);
  return lc_sys_return_nil(ctx, nextop);
}

uint8_t *lc_sys_nthname(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item) {
  VALUE_t index = pop_stack(ctx->vm->stack);
  VALUE_t itemname = pop_stack(ctx->vm->stack);
  if (!lc_value_is_type(itemname, VALUE_str) ||
      !lc_value_is_type(index, VALUE_int) || index.i < 0) {
    value_free(&index);
    value_free(&itemname);
    return lc_invalid_args_detail_return(ctx, nextop, VALUE_NIL,
        "sys.nthname requires a string item name and non-negative integer index");
  }

  VALUE_t result = VALUE_NIL;
  char fullname[MAX_ITEM_NAME];
  if (canonicalize_itemname(itemname.s, item, fullname)) {
    ITEM_t *parent = find_item(ctx->itemroot, fullname);
    if (parent) {
      ITEM_t *child = find_item_by_index(parent, (size_t)index.i);
      if (child) {
        result.type = VALUE_str;
        result.s = strdup(child->name);
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
  ITEM_t *child = find_item_by_index(ctx->itemroot, (size_t)index.i);
  if (child) {
    result.type = VALUE_str;
    result.s = strdup(child->name);
    if (!result.s) result = VALUE_NIL;
  }

  value_free(&index);
  return lc_sys_return(ctx, nextop, result);
}
