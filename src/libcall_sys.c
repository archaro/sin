#include <time.h>
#include <string.h>
#include <stdio.h>

#include "compiler_pipeline.h"
#include "error.h"
#include "floatconv.h"
#include "interpret.h"
#include "item.h"
#include "libcall_common.h"
#include "libcall_handlers.h"
#include "log.h"
#include "stack.h"

uint8_t *lc_sys_backup(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item) {
  // Create a backup of the itemstore.
  // All of the following is a long-winded way to get a backup filename.
  (void)item;
  char timestamp[64];
  time_t now = time(NULL);
  struct tm *tm_now = localtime(&now);
  strftime(timestamp, sizeof(timestamp), "%Y%m%d-%H%M%S", tm_now);
  char backupfile[strlen(ctx->itemstore_filename)+strlen(timestamp)+2];
  snprintf(backupfile, sizeof(backupfile), "%s_%s", ctx->itemstore_filename,
                                                                timestamp);
  if (!save_itemstore(backupfile, ctx->itemroot)) {
    logerr("sys.backup failed to persist itemstore backup '%s'.\n",
           backupfile);
  }
  // libcalls always return a value.
  push_stack(ctx->vm->stack, VALUE_NIL);
  return nextop;
}

uint8_t *lc_sys_log(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item) {
  // Pop the top of the stack and write it to the syslog
  // Try to do something sensible if the type is not a string.
  (void)item;
  VALUE_t val = pop_stack(ctx->vm->stack);
  switch (val.type) {
    case VALUE_str:
      logmsg(val.s);
      free(val.s);
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
      // One cannot logically output nil.
      break;
    case VALUE_bool:
      logmsg("%s", val.i?"true":"false");
      break;
    default:
      logmsg("Sys.log called with unknown value type.\n");
  }
  // libcalls always return a value.
  push_stack(ctx->vm->stack, VALUE_NIL);
  return nextop;
}

uint8_t *lc_sys_shutdown(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item) {
  // End the game loop, thereby shutting down neatly, and
  // saving the itemstore.
  // This call takes no parameters.
  (void)item;
  logmsg("Sys.shutdown called.  Shutting down.\n");
  (*ctx->safe_shutdown) = true;
  uv_stop(ctx->loop);
  // libcalls always return a value.
  push_stack(ctx->vm->stack, VALUE_NIL);
  return nextop;
}

uint8_t *lc_sys_abort(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item) {
  // End the game loop, thereby aborting, and not
  // saving the itemstore.
  // This call takes no parameters.
  (void)item;
  logmsg("Sys.abort called.  Immediate (and messy) shutdown.\n");
  (*ctx->safe_shutdown) = false;
  uv_stop(ctx->loop);
  // libcalls always return a value.
  push_stack(ctx->vm->stack, VALUE_NIL);
  return nextop;
}

uint8_t *lc_sys_compile(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item) {
  // Compile and execute some Sinistra code.
  // This call takes one parameter, expected to be a string.
  (void)item;
  VALUE_t val = pop_stack(ctx->vm->stack);

  if (!lc_value_is_type(val, VALUE_str)) {
    logmsg("Sys.compile called with non-string value.\n");
    FREE_STR(val);
    return lc_invalid_args_detail_return(ctx, nextop, VALUE_FALSE,
        "sys.compile source must be a string; non-string values, including floats, are invalid");
  }

  int8_t result = 0;
  CompilerDiagnostic diag;
  compiler_diag_init(&diag);
  OUTPUT_t *out = NULL;
  char tmpname[MAX_ITEM_NAME];
  static uint64_t tmpname_counter = 0;

  // Compile source -> bytecode
  result = compile_source_to_bytecode_diag(val.s, strlen(val.s), &out, &diag);

  if (result != 0 || !out || !out->bytecode) {
    // Compile failed; preserve structured compiler diagnostics.
    if (result == 0) {
      compiler_diag_reset(&diag);
      compiler_diag_set(&diag, ERR_COMP_UNKNOWN, DIAG_PHASE_COMPILE,
          "compile: missing bytecode output");
      compiler_diag_set_source_name(&diag, "<memory>");
      compiler_diag_set_location(&diag, 1, 1, 1);
      compiler_diag_set_excerpt(&diag, val.s ? val.s : "");
    }
    set_compiler_error_item(&diag);
    if (out) {
      if (out->bytecode) {
        free(out->bytecode);
      }
      free(out);
    }
    lc_cleanup_cstr(val.s);
    compiler_diag_reset(&diag);
    push_stack(ctx->vm->stack, VALUE_FALSE);
    return nextop;
  }

  int namelen = snprintf(tmpname, sizeof(tmpname),
      "__sys_compile_tmp__%llu", (unsigned long long)++tmpname_counter);
  if (namelen < 0 || namelen >= (int)sizeof(tmpname)) {
    set_error_item(ERR_RUNTIME_INVALIDARGS,
        "Sys.compile temporary item name generation failed.");
    free(out->bytecode);
    free(out);
    lc_cleanup_cstr(val.s);
    compiler_diag_reset(&diag);
    push_stack(ctx->vm->stack, VALUE_FALSE);
    return nextop;
  }

  // Compile succeeded: execute compiled code in a temporary code item.
  // Contract: Sys.compile must preserve the caller's stack frame below the
  // pre-call depth while discarding only temporary values produced by the
  // nested interpret() run. This keeps Sys.compile safe when invoked from
  // within an already-active interpreter frame.
  uint32_t len = out->nextbyte - out->bytecode;
  ITEM_t *tmpitem = insert_code_item(ctx->itemroot, tmpname, len, out->bytecode);

  if (!tmpitem) {
    // Could not create temp item (likely in-use/name conflict).
    // out->bytecode/out and val.s are still owned here and must be freed once.
    set_error_item(ERR_COMP_INUSE, NULL);
    free(out->bytecode);
    free(out);
    lc_cleanup_cstr(val.s);
    compiler_diag_reset(&diag);
    push_stack(ctx->vm->stack, VALUE_FALSE);
    return nextop;
  }

  int32_t stack_top_before_interpret = ctx->vm->stack->current;
  (void)interpret(ctx, tmpitem);
  while (ctx->vm->stack->current > stack_top_before_interpret) {
    VALUE_t dropped = pop_stack(ctx->vm->stack);
    value_free(&dropped);
  }

  // Best-effort cleanup of temp item
  delete_item(ctx->itemroot, tmpname);

  // clear compiler/runtime error indicators on success
  clear_error_item();

  free(out); // bytecode ownership moved into inserted item
  lc_cleanup_cstr(val.s);
  compiler_diag_reset(&diag);

  push_stack(ctx->vm->stack, VALUE_TRUE);
  return nextop;
}

