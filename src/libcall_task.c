#include <stdint.h>
#include <string.h>

#include "floatconv.h"
#include "interpret.h"
#include "item.h"
#include "libcall_common.h"
#include "libcall_handlers.h"
#include "log.h"
#include "stack.h"
#include "task.h"

void execute_task_cb(uv_timer_t *req) {
  // This callback is for executing tasks when they are due.
  TASK_t *task = req->data;
  DEBUG_LOG("Executing task %s (id: %d)\n", task->itemname, task->id);
  // Each task runs in its own VM (which may not be necessary, but
  // we will keep it up for now).
  RuntimeContext *task_ctx = &task->runtime_context;
  task_ctx->vm = task->vm;
  task_ctx->itemroot = task->itemroot;
  task_ctx->loop = task->loop;
  ITEM_t *item = find_item(task->itemroot, task->itemname);
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
  VALUE_t repeatin = pop_stack(ctx->vm->stack);
  VALUE_t startin = pop_stack(ctx->vm->stack);
  VALUE_t itemname = pop_stack(ctx->vm->stack);
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
  // Intervals are given in 10ths of a second, but we need milliseconds.
  if (startin.i < 0 || repeatin.i < 0 ||
      startin.i > (INT64_MAX / 100) || repeatin.i > (INT64_MAX / 100)) {
    FREE_STR(itemname);
    return lc_invalid_args_detail_return(ctx, nextop, VALUE_NIL,
        "task.newgametask intervals must be non-negative and within timer range");
  }
  ITEM_t *taskitem = find_item(ctx->itemroot, itemname.s);
  if (!taskitem) {
    // If the task item doesn't exist, it can't be run.
    // Ownership: free itemname once on this error path before returning.
    FREE_STR(itemname);
    push_stack(ctx->vm->stack, VALUE_NIL);
    set_error_item(ERR_RUNTIME_NOSUCHITEM, NULL);
    return nextop;
  }
  // We have the task item, and the validated start and repeat intervals.
  uint64_t start_ms = (uint64_t)startin.i * 100u;
  uint64_t repeat_ms = (uint64_t)repeatin.i * 100u;
  TASK_t *newtask = make_task(itemname.s, repeat_ms);
  newtask->itemroot = ctx->itemroot;
  newtask->loop = ctx->loop;
  newtask->runtime_context = *ctx;
  newtask->runtime_context.libcalls = NULL;
  newtask->runtime_context.initialized = false;
  (void)runtime_init(&newtask->runtime_context, newtask->vm);
  newtask->runtime_context.itemroot = newtask->itemroot;
  newtask->runtime_context.loop = newtask->loop;
  // Success path: this is the only free on this path (the !taskitem branch returns).
  FREE_STR(itemname);
  // Now add the task to the game loop starting at the correct interval
  uv_timer_init(ctx->loop, newtask->timer);
  // The handle needs to be able to access its task
  newtask->timer->data = newtask;
  // Off we go!
  uv_timer_start(newtask->timer, execute_task_cb, start_ms, repeat_ms);

  // libcalls always return a value. In this case, the id of the task.
  VALUE_t ret = {VALUE_int, {(int64_t)newtask->id}};
  push_stack(ctx->vm->stack, ret);
  return nextop;
}

uint8_t *lc_task_killtask(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item) {
  // Given a task id, kill it.
  // First validate the argument
  (void)item;
  VALUE_t taskid = pop_stack(ctx->vm->stack);
  if (!lc_value_is_type(taskid, VALUE_int)) {
    // taskid may only own heap memory when it is a string; FREE_STR is a safe no-op otherwise.
    FREE_STR(taskid);
    return lc_invalid_args_detail_return(ctx, nextop, VALUE_NIL,
        "task.killtask id must be an integer; floats are invalid");
  }

  // Does this task even exist?
  if (taskid.i < 0) {
    push_stack(ctx->vm->stack, VALUE_FALSE);
    return nextop;
  }
  TASK_t *task = find_task_by_id((uint64_t)taskid.i);
  if (!task) {
    // Nope!
    push_stack(ctx->vm->stack, VALUE_FALSE);
  } else {
    // Yes, so kill this task.
    uv_close((uv_handle_t *)task->timer, NULL);
    push_stack(ctx->vm->stack, VALUE_TRUE);
  }
  return nextop;
}
