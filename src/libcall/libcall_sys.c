#include <stddef.h>
#include <stdint.h>
#include <time.h>
#include <string.h>
#include <stdio.h>

#include "compiler/compiler_pipeline.h"
#include "error.h"
#include "floatconv.h"
#include "interpret.h"
#include "item.h"
#include "libcall_common.h"
#include "libcall_handlers.h"
#include "log.h"
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
  switch (val.type) {
    case VALUE_str:
      logmsg("%s", val.s);
      FREE_STR(val);
      break;
    case VALUE_int:
      logmsg("%ld", val.i);
      break;
    case VALUE_float: {
      char fbuffer[64];
      if (sin_format_binary64_buf(val.f, fbuffer, sizeof(fbuffer))) {
        logmsg("%s", fbuffer);
      } else {
        logmsg("<float-format-error>");
      }
      break;
    }
    case VALUE_nil:
      break;
    case VALUE_bool:
      logmsg("%s", val.i?"true":"false");
      break;
    default:
      logmsg("Sys.log called with unknown value type.\n");
  }
  return lc_sys_return_nil(ctx, nextop);
}

uint8_t *lc_sys_shutdown(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item) {
  // Stop the event loop, mark shutdown as safe, and push nil.
  (void)item;

  logmsg("Sys.shutdown called.  Shutting down.\n");
  (*ctx->safe_shutdown) = true;
  uv_stop(ctx->loop);
  return lc_sys_return_nil(ctx, nextop);
}

uint8_t *lc_sys_abort(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item) {
  // Stop the event loop, mark shutdown as unsafe, and push nil.
  (void)item;

  logmsg("Sys.abort called.  Immediate (and messy) shutdown.\n");
  (*ctx->safe_shutdown) = false;
  uv_stop(ctx->loop);
  return lc_sys_return_nil(ctx, nextop);
}

uint8_t *lc_sys_compile(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item) {
  // Consume a source string, compile it into a temporary code item, execute it,
  // discard values produced by that execution, and push true/false.
  (void)item;

  VALUE_t val = pop_stack(ctx->vm->stack);

  if (!lc_value_is_type(val, VALUE_str)) {
    logmsg("Sys.compile called with non-string value.\n");
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
    set_compiler_error_item_on_root(ctx ? ctx->itemroot : NULL, &diag);
    return lc_sys_compile_fail(ctx, nextop, &val, out, true, &diag);
  }

  int namelen = snprintf(tmpname, sizeof(tmpname),
      "__sys_compile_tmp__%llu", (unsigned long long)++tmpname_counter);
  if (namelen < 0 || namelen >= (int)sizeof(tmpname)) {
    set_error_item_on_root(ctx ? ctx->itemroot : NULL, ERR_RUNTIME_INVALIDARGS,
        "Sys.compile temporary item name generation failed.");
    return lc_sys_compile_fail(ctx, nextop, &val, out, true, &diag);
  }

  ptrdiff_t raw_len = out->nextbyte - out->bytecode;
  if (raw_len < 0 || (uintmax_t)raw_len > UINT32_MAX) {
    set_error_item_on_root(ctx ? ctx->itemroot : NULL, ERR_RUNTIME_INVALIDARGS,
        "Sys.compile bytecode output length is out of range.");
    return lc_sys_compile_fail(ctx, nextop, &val, out, true, &diag);
  }
  uint32_t len = (uint32_t)raw_len;
  ITEM_t *tmpitem = insert_code_item(ctx->itemroot, tmpname, len, out->bytecode);

  if (!tmpitem) {
    set_error_item_on_root(ctx ? ctx->itemroot : NULL, ERR_COMP_INUSE, NULL);
    return lc_sys_compile_fail(ctx, nextop, &val, out, true, &diag);
  }

  // Preserve the caller frame below the pre-call depth; discard only values
  // produced by the nested interpret() run.
  int32_t stack_top_before_interpret = ctx->vm->stack->current;
  (void)interpret(ctx, tmpitem);
  while (ctx->vm->stack->current > stack_top_before_interpret) {
    VALUE_t dropped = pop_stack(ctx->vm->stack);
    value_free(&dropped);
  }

  delete_item(ctx->itemroot, tmpname);
  clear_error_item_on_root(ctx ? ctx->itemroot : NULL);

  lc_sys_free_output(out, false);
  value_free(&val);
  compiler_diag_reset(&diag);

  return lc_sys_return(ctx, nextop, VALUE_TRUE);
}
