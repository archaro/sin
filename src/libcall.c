// Library calls - pseudo-items which do interesting things.

// Licensed under the MIT License - see LICENSE file for details.

#include <time.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>

#include "util.h"
#include "error.h"
#include "memory.h"
#include "network.h"
#include "task.h"
#include "libcall.h"
#include "log.h"
#include "stack.h"
#include "item.h"
#include "compiler_pipeline.h"
#include "interpret.h"
#include "floatconv.h"

// Connected lines.  Defined in network.c
extern LINE_t *line;

// Some shorthand
#define VM ctx->vm

// Popped VALUE_t ownership rules in this file:
// - Callers own popped values and must free any owned string payload exactly once.
// - FREE_STR(v) is the canonical cleanup helper; it is a safe no-op for non-string values.
static inline bool lc_value_is_type(VALUE_t v, VALUE_e type) {
  return value_is_type(&v, type);
}

static inline uint8_t *lc_invalid_args_return(RuntimeContext *ctx, uint8_t *nextop, VALUE_t ret) {
  set_error_item(ERR_RUNTIME_INVALIDARGS, NULL);
  push_stack(VM->stack, ret);
  return nextop;
}

static inline uint8_t *lc_invalid_args_detail_return(RuntimeContext *ctx, uint8_t *nextop, VALUE_t ret, const char *detail) {
  set_error_item(ERR_RUNTIME_INVALIDARGS, detail);
  push_stack(VM->stack, ret);
  return nextop;
}

static inline void lc_cleanup_values(VALUE_t *values, size_t count) {
  for (size_t i = 0; i < count; i++) {
    value_free(&values[i]);
  }
}

static inline void lc_cleanup_cstr(char *s) {
  if (s) {
    free(s);
  }
}

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
  push_stack(VM->stack, VALUE_NIL);
  return nextop;
}

uint8_t *lc_sys_log(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item) {
  // Pop the top of the stack and write it to the syslog
  // Try to do something sensible if the type is not a string.
  (void)item;
  VALUE_t val = pop_stack(VM->stack);
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
  push_stack(VM->stack, VALUE_NIL);
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
  push_stack(VM->stack, VALUE_NIL);
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
  push_stack(VM->stack, VALUE_NIL);
  return nextop;
}

uint8_t *lc_sys_compile(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item) {
  // Compile and execute some Sinistra code.
  // This call takes one parameter, expected to be a string.
  (void)item;
  VALUE_t val = pop_stack(VM->stack);

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
    push_stack(VM->stack, VALUE_FALSE);
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
    push_stack(VM->stack, VALUE_FALSE);
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
    push_stack(VM->stack, VALUE_FALSE);
    return nextop;
  }

  int32_t stack_top_before_interpret = VM->stack->current;
  (void)interpret(ctx, tmpitem);
  while (VM->stack->current > stack_top_before_interpret) {
    VALUE_t dropped = pop_stack(VM->stack);
    value_free(&dropped);
  }

  // Best-effort cleanup of temp item
  delete_item(ctx->itemroot, tmpname);

  // clear compiler/runtime error indicators on success
  clear_error_item();

  free(out); // bytecode ownership moved into inserted item
  lc_cleanup_cstr(val.s);
  compiler_diag_reset(&diag);

  push_stack(VM->stack, VALUE_TRUE);
  return nextop;
}

void execute_task_cb(uv_timer_t *req) {
  // This callback is for executing tasks when they are due.
  TASK_t *task = req->data;
  DEBUG_LOG("Executing task %s (id: %d)\n", task->itemname, task->id);
  // Each task runs in its own VM (which may not be necessary, but
  // we will keep it up for now).
  RuntimeContext *task_ctx = &task->runtime_context;
  task_ctx->vm = task->vm;
  ITEM_t *item = find_item(task_ctx->itemroot, task->itemname);
  if (item && item->type == ITEM_code) {
    VALUE_t ret = interpret(task_ctx, item);
    reset_stack(task->vm->stack);
    if (ret.type == VALUE_int) {
      logmsg("Bytecode interpreter returned: %ld\n", ret.i);
    } else if (ret.type == VALUE_str) {
      logmsg("Bytecode interpreter returned: %s\n", ret.s);
      value_free(&ret);
    } else if (ret.type == VALUE_float) {
      char fbuffer[64];
      if (sin_format_binary64_buf(ret.f, fbuffer, sizeof(fbuffer))) {
        logmsg("Bytecode interpreter returned: %s\n", fbuffer);
      } else {
        logmsg("Bytecode interpreter returned: <float-format-error>\n");
      }
    } else if (ret.type == VALUE_bool) {
      logmsg("Bytecode interpreter returned: %s\n", ret.i?"true":"false");
    } else if (ret.type == VALUE_nil) {
      logmsg("Bytecode interpreter returned nil.\n");
    } else {
      logerr("Interpreter returned unknown value type: '%c'.\n", ret.type);
    }
  } else {
    logerr("Cannot execute %s - not a code item.\n", task->itemname);
  }
}

uint8_t *lc_task_newgametask(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item) {
  // Create a new game task.  There are three values on the stack:
  // name of the item to execute, time until first execution, and
  // time between executions.  The intervals are in 10ths of a second.
  // The item must exist, both time values must be >=0, and if both
  // intervals are 0 then the item is executed once immediately, and
  // not again.
  // Validate the parameters before creating the task.
  (void)item;
  VALUE_t repeatin = pop_stack(VM->stack);
  VALUE_t startin = pop_stack(VM->stack);
  VALUE_t itemname = pop_stack(VM->stack);
  if (!lc_value_is_type(repeatin, VALUE_int)
   || !lc_value_is_type(startin, VALUE_int)
   || !lc_value_is_type(itemname, VALUE_str)) {
    // Invalid parameters.  Clean them up, set the error item,
    // and return.
    VALUE_t popped[] = {repeatin, startin, itemname};
    lc_cleanup_values(popped, 3);
    return lc_invalid_args_detail_return(ctx, nextop, VALUE_NIL,
        "task.newgametask expects string item name and integer start/repeat intervals; floats are invalid for intervals");
  }
  ITEM_t *taskitem = find_item(ctx->itemroot, itemname.s);
  if (!taskitem) {
    // If the task item doesn't exist, it can't be run.
    // Ownership: free itemname once on this error path before returning.
    FREE_STR(itemname);
    push_stack(VM->stack, VALUE_NIL);
    set_error_item(ERR_RUNTIME_NOSUCHITEM, NULL);
    return nextop;
  }
  // We have the task item, and the start and repeat intervals.
  // Intervals are given in 10ths of a second, but we need milliseconds.
  repeatin.i *= 100;
  startin.i *= 100;
  TASK_t *newtask = make_task(itemname.s, repeatin.i);
  newtask->runtime_context = *ctx;
  newtask->runtime_context.vm = newtask->vm;
  // Success path: this is the only free on this path (the !taskitem branch returns).
  FREE_STR(itemname);
  // Now add the task to the game loop starting at the correct interval
  uv_timer_init(ctx->loop, newtask->timer);
  // The handle needs to be able to access its task
  newtask->timer->data = newtask;
  // Off we go!
  uv_timer_start(newtask->timer, execute_task_cb, startin.i, repeatin.i);

  // libcalls always return a value. In this case, the id of the task.
  VALUE_t ret = {VALUE_int, {newtask->id}};
  push_stack(VM->stack, ret);
  return nextop;
}

uint8_t *lc_task_killtask(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item) {
  // Given a task id, kill it.
  // First validate the argument
  (void)item;
  VALUE_t taskid = pop_stack(VM->stack);
  if (!lc_value_is_type(taskid, VALUE_int)) {
    // taskid may only own heap memory when it is a string; FREE_STR is a safe no-op otherwise.
    FREE_STR(taskid);
    return lc_invalid_args_detail_return(ctx, nextop, VALUE_NIL,
        "task.killtask id must be an integer; floats are invalid");
  }

  // Does this task even exist?
  TASK_t *task = find_task_by_id(taskid.i);
  if (!task) {
    // Nope!
    push_stack(VM->stack, VALUE_FALSE);
  } else {
    // Yes, so kill this task.
    uv_close((uv_handle_t *)task->timer, NULL);
    push_stack(VM->stack, VALUE_TRUE);
  }
  return nextop;
}

uint8_t *lc_net_input(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item) {
  // Called by the task which checks for player input.
  // We operate a fair queuing process here.  Everyone
  // gets a turn.  Find the next activity.
  (void)item;
  (*ctx->lastconn)++;
  if ((*ctx->maxconns) == 0 || (*ctx->lastconn) >= (*ctx->maxconns)) {
    (*ctx->lastconn) = 0;
  }
  while ((*ctx->lastconn) < (*ctx->maxconns)) {
    VALUE_t val = {VALUE_int, {0}};
    // Find some activity.
    switch (line[(*ctx->lastconn)].status) {
      case LINE_connecting:
        line[(*ctx->lastconn)].status = LINE_idle;
        // Set the input item to the current line
        val.i = (long)(*ctx->lastconn);
        set_item(ctx->itemroot, ctx->inputline_name, val);
        // And return a value from this libcall to say what happened.
        val.i = 1;
        push_stack(VM->stack, val);
        return nextop;
      case LINE_disconnecting:
        destroy_line(&line[(*ctx->lastconn)]);
        line[(*ctx->lastconn)].status = LINE_empty;
        // Set the input item to the current line
        val.i = (long)(*ctx->lastconn);
        set_item(ctx->itemroot, ctx->inputline_name, val);
        val.i = 2;
        push_stack(VM->stack, val);
        return nextop;
      case LINE_data:
        // Set the input item to the current line
        val.i = (long)(*ctx->lastconn);
        set_item(ctx->itemroot, ctx->inputline_name, val);
        // And grab some data.
        VALUE_t str = {VALUE_str, {0}};
        str.s = get_input(&line[(*ctx->lastconn)]);
        set_item(ctx->itemroot, ctx->inputtext_name, str);
        val.i = 3;
        push_stack(VM->stack, val);
        return nextop;
      default:
        (*ctx->lastconn)++;
    }
  }
  // No activity found.
  push_stack(VM->stack, VALUE_ZERO);
  return nextop;
}

uint8_t *lc_net_write(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item) {
  // Write data out to a line
  // Validate the parameters before creating the task.
  (void)item;
  VALUE_t out = pop_stack(VM->stack);
  VALUE_t linenum = pop_stack(VM->stack);
  size_t line_index = 0;

  if (!lc_value_is_type(linenum, VALUE_int) || linenum.i < 0 ||
      (size_t)linenum.i >= (*ctx->maxconns)) {
    FREE_STR(out);
    FREE_STR(linenum);
    return lc_invalid_args_detail_return(ctx, nextop, VALUE_NIL,
        "net.write line must be an integer connection index; floats are invalid");
  } else {
    line_index = (size_t)linenum.i;
    if ((line[line_index].status != LINE_data
        && line[line_index].status != LINE_idle)
        || line[line_index].telnet == NULL) {
      FREE_STR(out);
      push_stack(VM->stack, VALUE_NIL);
      return nextop;
    }
    switch(out.type) {
      case VALUE_str:
        if (line[line_index].outbuf &&
            !line_can_accept_output(&line[line_index], strlen(out.s))) {
          logerr("net.write rejected for line %zu: output buffer limit or backpressure.\n",
                 line_index);
          FREE_STR(out);
          push_stack(VM->stack, VALUE_FALSE);
          return nextop;
        }
        telnet_send_text(line[line_index].telnet, out.s, strlen(out.s));
        FREE_STR(out);
        break;
      case VALUE_int: {
        char buffer[22];
        itoa(out.i, buffer, 10);
        if (line[line_index].outbuf &&
            !line_can_accept_output(&line[line_index], strlen(buffer))) {
          logerr("net.write rejected for line %zu: output buffer limit or backpressure.\n",
                 line_index);
          push_stack(VM->stack, VALUE_FALSE);
          return nextop;
        }
        telnet_send_text(line[line_index].telnet, buffer, strlen(buffer));
        break;
      }
      case VALUE_float: {
        char fbuffer[64];
        if (sin_format_binary64_buf(out.f, fbuffer, sizeof(fbuffer))) {
          if (line[line_index].outbuf &&
              !line_can_accept_output(&line[line_index], strlen(fbuffer))) {
            logerr("net.write rejected for line %zu: output buffer limit or backpressure.\n",
                   line_index);
            push_stack(VM->stack, VALUE_FALSE);
            return nextop;
          }
          telnet_send_text(line[line_index].telnet, fbuffer, strlen(fbuffer));
        }
        break;
      }
      case VALUE_nil:
        // Nothing to output
        break;
      case VALUE_bool: {
        char *t = "true";
        char *f = "false";
        if (line[line_index].outbuf &&
            !line_can_accept_output(&line[line_index], strlen(out.i?t:f))) {
          logerr("net.write rejected for line %zu: output buffer limit or backpressure.\n",
                 line_index);
          push_stack(VM->stack, VALUE_FALSE);
          return nextop;
        }
        telnet_send_text(line[line_index].telnet, out.i?t:f,
                                                        strlen(out.i?t:f));
        break;
      }
    }
  }
  if (line[line_index].status == LINE_disconnecting) {
    push_stack(VM->stack, VALUE_FALSE);
    return nextop;
  }
  // Libcalls always return a value
  push_stack(VM->stack, VALUE_NIL);
  return nextop;
}

uint8_t *lc_str_capitalise(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item) {
  // If the value on the top of the stack is a string, capitalise the
  // first letter.  Otherwise pop the top of the stack and push nil.
  (void)item;

  if (VM->stack->stack[VM->stack->current].type == VALUE_str) {
    VM->stack->stack[VM->stack->current].s[0] =
                        toupper(VM->stack->stack[VM->stack->current].s[0]);
  } else {
    pop_stack(VM->stack);
    push_stack(VM->stack, VALUE_NIL);
  }
  return nextop;
}

uint8_t *lc_str_upper(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item) {
  // If the value on the top of the stack is a string, make it
  // uppercase.  Otherwise pop the top of the stack and push nil.
  (void)item;

  if (VM->stack->stack[VM->stack->current].type == VALUE_str) {
    char *c = VM->stack->stack[VM->stack->current].s;
    while (*c) {
      *c = toupper(*c);
      c++;
    }
  } else {
    pop_stack(VM->stack);
    push_stack(VM->stack, VALUE_NIL);
  }
  return nextop;
}

uint8_t *lc_str_lower(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item) {
  // If the value on the top of the stack is a string, make it
  // lowercase.  Otherwise pop the top of the stack and push nil.
  (void)item;

  if (VM->stack->stack[VM->stack->current].type == VALUE_str) {
    char *c = VM->stack->stack[VM->stack->current].s;
    while (*c) {
      *c = tolower(*c);
      c++;
    }
  } else {
    pop_stack(VM->stack);
    push_stack(VM->stack, VALUE_NIL);
  }
  return nextop;
}
