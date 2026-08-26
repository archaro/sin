#include <stdint.h>
#include <inttypes.h>
#include <string.h>

#include "floatconv.h"
#include "interpret.h"
#include "item.h"
#include "itemref.h"
#include "libcall_common.h"
#include "libcall_handlers.h"
#include "log.h"
#include "stack.h"
#include "task.h"

void execute_task_cb(uv_timer_t *req) {
  // This callback is for executing tasks when they are due.
  TASK_t *task = req->data;
  logverbose("Executing task %s (id: %" PRIu64 ")\n", task->itemname,
             task->id);
  // Each task runs in its own VM (which may not be necessary, but
  // we will keep it up for now).
  RuntimeContext *task_ctx = &task->runtime_context;
  task_ctx->vm = task->vm;
  task_ctx->itemstore = task->itemstore;
  task_ctx->loop = task->loop;
  task_ctx->current_task_id = task->id;
  ITEM_t *item = find_item(itemstore_root(task->itemstore), task->itemname);
  if (item && item_kind(item) == ITEM_code) {
    VALUE_t ret = interpret(task_ctx, item);
    reset_stack(task->vm->stack);
    if (ret.type == VALUE_int) {
      logverbose("Bytecode interpreter returned: %ld\n", ret.i);
    } else if (ret.type == VALUE_str) {
      logverbose("Bytecode interpreter returned: %s\n", ret.s);
    } else if (ret.type == VALUE_float) {
      char fbuffer[64];
      if (sin_format_binary64_buf(ret.f, fbuffer, sizeof(fbuffer))) {
        logverbose("Bytecode interpreter returned: %s\n", fbuffer);
      } else {
        logverbose("Bytecode interpreter returned: <float-format-error>\n");
      }
    } else if (ret.type == VALUE_bool) {
      logverbose("Bytecode interpreter returned: %s\n", ret.i?"true":"false");
    } else if (ret.type == VALUE_nil) {
      logverbose("Bytecode interpreter returned nil.\n");
    } else if (ret.type == VALUE_itemref || ret.type == VALUE_list) {
      logverbose("Bytecode interpreter returned %s.\n",
                 value_type_name(ret.type));
    } else {
      logerr("Interpreter returned unknown value type: '%c'.\n", ret.type);
    }
    value_free(&ret);
  } else {
    logerr("Cannot execute %s - not a code item.\n", task->itemname);
  }
  task_ctx->current_task_id = 0;
  if (task->interval == 0) {
    (void)request_task_close(task);
  }
}

static uint8_t *lc_task_timer_setup_failed(RuntimeContext *ctx, uint8_t *nextop,
                                           TASK_t *task, VALUE_t *itemname,
                                           const char *detail) {
  if (task) destroy_task(task);
  if (itemname) value_free(itemname);
  push_stack(ctx->vm->stack, VALUE_NIL);
  set_error_item(ctx ? itemstore_root(ctx->itemstore) : NULL, ERR_RUNTIME_INVALIDARGS,
                         detail, ctx ? ctx->current_item : NULL);
  return nextop;
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
   || (itemname.type != VALUE_str && itemname.type != VALUE_itemref)) {
    // Invalid parameters.  Clean them up, set the error item,
    // and return.
    VALUE_t popped[] = {repeatin, startin, itemname};
    lc_cleanup_values(popped, 3);
    return lc_invalid_args_detail_return(ctx, nextop, VALUE_NIL,
        "task.newgametask expects string item name or item reference and integer start/repeat intervals; floats are invalid for intervals");
  }
  // Intervals are given in 10ths of a second, but we need milliseconds.
  if (startin.i < 0 || repeatin.i < 0 ||
      startin.i > (INT64_MAX / 100) || repeatin.i > (INT64_MAX / 100)) {
    value_free(&itemname);
    return lc_invalid_args_detail_return(ctx, nextop, VALUE_NIL,
        "task.newgametask intervals must be non-negative and within timer range");
  }
  const char *task_item_name = itemname.type == VALUE_itemref
      ? sin_itemref_path(itemname.itemref) : itemname.s;
  ITEM_t *taskitem = task_item_name
      ? find_item(itemstore_root(ctx->itemstore), task_item_name) : NULL;
  if (!taskitem) {
    // If the task item doesn't exist, it can't be run.
    // Ownership: free itemname once on this error path before returning.
    value_free(&itemname);
    push_stack(ctx->vm->stack, VALUE_NIL);
    set_error_item(ctx ? itemstore_root(ctx->itemstore) : NULL, ERR_RUNTIME_NOSUCHITEM,
                           NULL, ctx ? ctx->current_item : NULL);
    return nextop;
  }
  // We have the task item, and the validated start and repeat intervals.
  uint64_t start_ms = (uint64_t)startin.i * 100u;
  uint64_t repeat_ms = (uint64_t)repeatin.i * 100u;
  TASK_t *newtask = make_task((char *)task_item_name, repeat_ms);
  if (!newtask) {
    value_free(&itemname);
    push_stack(ctx->vm->stack, VALUE_NIL);
    set_error_item(ctx ? itemstore_root(ctx->itemstore) : NULL, ERR_RUNTIME_INVALIDARGS,
                           "Unable to allocate new game task.",
                           ctx ? ctx->current_item : NULL);
    return nextop;
  }
  newtask->itemstore = ctx->itemstore;
  newtask->loop = ctx->loop;
  newtask->runtime_context = *ctx;
  newtask->runtime_context.current_task_id = 0;
  newtask->runtime_context.libcalls = NULL;
  newtask->runtime_context.initialized = false;
  if (!runtime_init(&newtask->runtime_context, newtask->vm)) {
    return lc_task_timer_setup_failed(ctx, nextop, newtask, &itemname,
                                      "task.newgametask failed to initialize runtime");
  }
  newtask->runtime_context.itemstore = newtask->itemstore;
  newtask->runtime_context.loop = newtask->loop;
  // Now add the task to the game loop starting at the correct interval
  if (!ctx->loop) {
    return lc_task_timer_setup_failed(ctx, nextop, newtask, &itemname,
                                      "task.newgametask requires an active event loop");
  }
  if (!start_task_timer(newtask, ctx->loop, execute_task_cb, start_ms)) {
    const char *detail = newtask->state == TASK_ALLOCATED
        ? "task.newgametask failed to initialize timer"
        : "task.newgametask failed to start timer";
    return lc_task_timer_setup_failed(ctx, nextop, newtask, &itemname,
                                      detail);
  }
  // Success path: this is the only free on this path (the !taskitem branch returns).
  value_free(&itemname);

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
    push_stack(ctx->vm->stack,
               request_task_close(task) ? VALUE_TRUE : VALUE_FALSE);
  }
  return nextop;
}

uint8_t *lc_task_thisid(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item) {
  (void)item;
  if (ctx->current_task_id == 0) {
    push_stack(ctx->vm->stack, VALUE_NIL);
  } else {
    push_stack(ctx->vm->stack,
               (VALUE_t){VALUE_int, {.i = ctx->current_task_id > INT64_MAX
                   ? INT64_MAX : (int64_t)ctx->current_task_id}});
  }
  return nextop;
}

uint8_t *lc_task_exists(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item) {
  (void)item;
  VALUE_t arg = pop_stack(ctx->vm->stack);
  if (!lc_value_is_type(arg, VALUE_int)) {
    lc_cleanup_values(&arg, 1);
    return lc_invalid_args_detail_return(ctx, nextop, VALUE_NIL,
        "task.exists expects an integer task id; floats and strings are invalid");
  }
  int64_t id = arg.i;
  if (id < 0) {
    push_stack(ctx->vm->stack, VALUE_FALSE);
    return nextop;
  }
  TASK_t *task = find_task_by_id((uint64_t)id);
  push_stack(ctx->vm->stack, task ? VALUE_TRUE : VALUE_FALSE);
  return nextop;
}

uint8_t *lc_task_count(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item) {
  (void)item;
  size_t count = task_list_count();
  int64_t result;
  if (count > (size_t)INT64_MAX) {
    result = INT64_MAX;
  } else {
    result = (int64_t)count;
  }
  push_stack(ctx->vm->stack, (VALUE_t){VALUE_int, {.i = result}});
  return nextop;
}
