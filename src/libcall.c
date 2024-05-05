#include <time.h>
#include <string.h>

#include "util.h"
#include "error.h"
#include "memory.h"
#include "config.h"
#include "task.h"
#include "libcall.h"
#include "log.h"
#include "stack.h"
#include "item.h"
#include "interpret.h"

// Configuration object.  Defined in sin.c
extern CONFIG_t config;

// Some shorthand
#define VM config.vm

uint8_t *lc_sys_backup(uint8_t *nextop, ITEM_t *item) {
  // Create a backup of the itemstore.
  DEBUG_LOG("Called sys.backup\n");
  // All of the following is a long-winded way to get a backup filename.
  char timestamp[64];
  time_t now = time(NULL);
  struct tm *tm_now = localtime(&now);
  strftime(timestamp, sizeof(timestamp), "%Y%m%d-%H%M%S", tm_now);
  char backupfile[strlen(config.itemstore)+strlen(timestamp)+2];
  snprintf(backupfile, sizeof(backupfile), "%s_%s", config.itemstore,
                                                                timestamp);
  save_itemstore(backupfile, config.itemroot);
  // libcalls always return a value.
  push_stack(VM->stack, VALUE_NIL);
  return nextop;
}

uint8_t *lc_sys_log(uint8_t *nextop, ITEM_t *item) {
  // Pop the top of the stack and write it to the syslog
  // Try to do something sensible if the type is not a string.
  VALUE_t val = pop_stack(VM->stack);
  switch (val.type) {
    case VALUE_str:
      logmsg(val.s);
      free(val.s);
      break;
    case VALUE_int:
      char valuebuf[21];
      itoa(val.i, valuebuf, 10);
      break;
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

uint8_t *lc_sys_shutdown(uint8_t *nextop, ITEM_t *item) {
  // End the game loop, thereby shutting down.
  // This call takes no parameters.
  logmsg("Sys.shutdown called.  Shutting down.\n");
  uv_stop(config.loop);
  // libcalls always return a value.
  push_stack(VM->stack, VALUE_NIL);
  return nextop;
}

void execute_task_cb(uv_timer_t *req) {
  // This callback is for executing tasks when they are due.
  TASK_t *task = req->data;
  DEBUG_LOG("Executing task %s (id: %d)\n", task->itemname, task->id);
  // Each task runs in its own VM (which may not be necessary, but
  // we will keep it up for now).
  config.vm = task->vm;
  ITEM_t *item = find_item(config.itemroot, task->itemname);
  if (item && item->type == ITEM_code) {
    VALUE_t ret = interpret(item);
    reset_stack(VM->stack);
    if (ret.type == VALUE_int) {
      logmsg("Bytecode interpreter returned: %ld\n", ret.i);
    } else if (ret.type == VALUE_str) {
      logmsg("Bytecode interpreter returned: %s\n", ret.s);
      FREE_ARRAY(char, ret.s, strlen(ret.s));
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

uint8_t *lc_task_newgametask(uint8_t *nextop, ITEM_t *item) {
  // Create a new game task.  There are three values on the stack:
  // name of the item to execute, time until first execution, and
  // time between executions.  The intervals are in 10ths of a second.
  // The item must exist, both time values must be >=0, and if both
  // intervals are 0 then the item is executed once immediately, and
  // not again.
  // Validate the parameters before creating the task.
  VALUE_t repeatin = pop_stack(VM->stack);
  VALUE_t startin = pop_stack(VM->stack);
  VALUE_t itemname = pop_stack(VM->stack);
  if (repeatin.type != VALUE_int || startin.type != VALUE_int
                               || itemname.type != VALUE_str) {
    // Invalid parameters.  Clean them up, set the error item,
    // and return.
    FREE_STR(repeatin);
    FREE_STR(startin);
    FREE_STR(itemname);
    set_error_item(ERR_RUNTIME_INVALIDARGS);
    push_stack(VM->stack, VALUE_NIL);
    return nextop;
  }
  ITEM_t *taskitem = find_item(config.itemroot, itemname.s);
  if (!taskitem) {
    // If the task item doesn't exist, it can't be run.
    FREE_STR(itemname);
    push_stack(VM->stack, VALUE_NIL);
    set_error_item(ERR_RUNTIME_NOSUCHITEM);
    return nextop;
  }
  // We have the task item, and the start and repeat intervals.
  // Intervals are given in 10ths of a second, but we need milliseconds.
  repeatin.i *= 100;
  startin.i *= 100;
  TASK_t *newtask = make_task(itemname.s, repeatin.i);
  FREE_STR(itemname);
  // Now add the task to the game loop starting at the correct interval
  uv_timer_init(config.loop, newtask->timer);
  // The handle needs to be able to access its task
  newtask->timer->data = newtask;
  // Off we go!
  uv_timer_start(newtask->timer, execute_task_cb, startin.i, repeatin.i);

  // libcalls always return a value. In this case, the id of the task.
  VALUE_t ret = {VALUE_int, {newtask->id}};
  push_stack(VM->stack, ret);
  return nextop;
}

const LIBCALL_t libcalls[] = {
  {"sys", "backup", 1, 0, 0, lc_sys_backup},
  {"sys", "log", 1, 1, 1, lc_sys_log},
  {"sys", "shutdown", 1, 2, 0, lc_sys_shutdown},
  {"task", "newgametask", 2, 0, 3, lc_task_newgametask},
  {NULL, NULL, -1, -1, 0, NULL}  // End marker
};

bool libcall_lookup(const char *libname, const char *callname,
                   uint8_t *lib_index, uint8_t *call_index, uint8_t *args) {
  // Finds a library call.  Returns true if found, with lib_index and
  // call_index being updated to the correct indices.
  for (int i = 0; libcalls[i].libname != NULL; i++) {
    if (strcmp(libcalls[i].libname, libname) == 0 &&
        strcmp(libcalls[i].callname, callname) == 0) {
      *lib_index = libcalls[i].lib_index;
      *call_index = libcalls[i].call_index;
      *args = libcalls[i].args;
      return true;  // Found
    }
  }
  return false;  // Not found
}

void *libcall_func(uint8_t lib, uint8_t call) {
  // Given a library and call index, try to find it in the
  // libcall table.  Return a pointer to its function if
  // found, otherwise return NULL.
  for (int i = 0; libcalls[i].libname != NULL; i++) {
    if (libcalls[i].lib_index == lib &&
        libcalls[i].call_index == call) {
      return libcalls[i].func;
    }
  }
  return NULL;
}

