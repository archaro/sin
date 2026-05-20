// Library calls - pseudo-items which do interesting things.

// Licensed under the MIT License - see LICENSE file for details.

#include <time.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>

#include "util.h"
#include "error.h"
#include "memory.h"
#include "config.h"
#include "network.h"
#include "task.h"
#include "libcall.h"
#include "log.h"
#include "stack.h"
#include "item.h"
#include "compiler_pipeline.h"
#include "interpret.h"

// Configuration object.  Defined in sin.c
extern CONFIG_t config;

// Connected lines.  Defined in network.c
extern LINE_t *line;

// Some shorthand
#define VM config.vm

uint8_t *lc_sys_backup(uint8_t *nextop, ITEM_t *item) {
  // Create a backup of the itemstore.
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
      logmsg("%d", val.i);
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
  // End the game loop, thereby shutting down neatly, and
  // saving the itemstore.
  // This call takes no parameters.
  logmsg("Sys.shutdown called.  Shutting down.\n");
  config.safe_shutdown = true;
  uv_stop(config.loop);
  // libcalls always return a value.
  push_stack(VM->stack, VALUE_NIL);
  return nextop;
}

uint8_t *lc_sys_abort(uint8_t *nextop, ITEM_t *item) {
  // End the game loop, thereby aborting, and not
  // saving the itemstore.
  // This call takes no parameters.
  logmsg("Sys.abort called.  Immediate (and messy) shutdown.\n");
  config.safe_shutdown = false;
  uv_stop(config.loop);
  // libcalls always return a value.
  push_stack(VM->stack, VALUE_NIL);
  return nextop;
}

uint8_t *lc_sys_compile(uint8_t *nextop, ITEM_t *item) {
  // Compile and execute some Sinistra code.
  // This call takes one parameter, expected to be a string.
  VALUE_t val = pop_stack(VM->stack);

  if (val.type != VALUE_str) {
    logmsg("Sys.compile called with non-string value.\n");
    FREE_STR(val);
    set_error_item(ERR_RUNTIME_INVALIDARGS, NULL);
    push_stack(VM->stack, VALUE_FALSE);
    return nextop;
  }

  int8_t result = 0;
  char *errdetail = NULL;
  OUTPUT_t *out = NULL;
  char tmpname[MAX_ITEM_NAME];
  static uint64_t tmpname_counter = 0;

  // Compile source -> bytecode
  result = compile_source_to_bytecode(val.s, strlen(val.s), &out, &errdetail);

  if (result != 0 || !out || !out->bytecode) {
    // Compile failed
    set_error_item(result != 0 ? result : ERR_COMP_UNKNOWN, errdetail);
    if (errdetail) {
      FREE_ARRAY(char, errdetail, strlen(errdetail) + 1);
    }
    if (out) {
      if (out->bytecode) {
        FREE_ARRAY(unsigned char, out->bytecode, out->maxsize);
      }
      FREE_ARRAY(OUTPUT_t, out, 1);
    }
    FREE_ARRAY(char, val.s, strlen(val.s) + 1);
    push_stack(VM->stack, VALUE_FALSE);
    return nextop;
  }

  int namelen = snprintf(tmpname, sizeof(tmpname),
      "__sys_compile_tmp__%llu", (unsigned long long)++tmpname_counter);
  if (namelen < 0 || namelen >= (int)sizeof(tmpname)) {
    set_error_item(ERR_RUNTIME_INVALIDARGS,
        "Sys.compile temporary item name generation failed.");
    FREE_ARRAY(unsigned char, out->bytecode, out->maxsize);
    FREE_ARRAY(OUTPUT_t, out, 1);
    FREE_ARRAY(char, val.s, strlen(val.s) + 1);
    push_stack(VM->stack, VALUE_FALSE);
    return nextop;
  }

  // Compile succeeded: execute compiled code in a temporary code item.
  // Contract: Sys.compile must preserve the caller's stack frame below the
  // pre-call depth while discarding only temporary values produced by the
  // nested interpret() run. This keeps Sys.compile safe when invoked from
  // within an already-active interpreter frame.
  uint32_t len = out->nextbyte - out->bytecode;
  ITEM_t *tmpitem = insert_code_item(config.itemroot, tmpname, len, out->bytecode);

  if (!tmpitem) {
    // Could not create temp item (likely in-use/name conflict)
    set_error_item(ERR_COMP_INUSE, NULL);
    FREE_ARRAY(unsigned char, out->bytecode, out->maxsize);
    FREE_ARRAY(OUTPUT_t, out, 1);
    FREE_ARRAY(char, val.s, strlen(val.s) + 1);
    push_stack(VM->stack, VALUE_FALSE);
    return nextop;
  }

  int32_t stack_top_before_interpret = VM->stack->current;
  (void)interpret(tmpitem);
  while (VM->stack->current > stack_top_before_interpret) {
    FREE_STR(pop_stack(VM->stack));
  }

  // Best-effort cleanup of temp item
  delete_item(config.itemroot, tmpname);

  // clear compiler/runtime error indicators on success
  set_item(config.itemroot, "error", VALUE_NIL);
  set_item(config.itemroot, "error.msg", VALUE_NIL);

  FREE_ARRAY(OUTPUT_t, out, 1); // bytecode ownership moved into inserted item
  FREE_ARRAY(char, val.s, strlen(val.s) + 1);

  push_stack(VM->stack, VALUE_TRUE);
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
    set_error_item(ERR_RUNTIME_INVALIDARGS, NULL);
    push_stack(VM->stack, VALUE_NIL);
    return nextop;
  }
  ITEM_t *taskitem = find_item(config.itemroot, itemname.s);
  if (!taskitem) {
    // If the task item doesn't exist, it can't be run.
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

uint8_t *lc_task_killtask(uint8_t *nextop, ITEM_t *item) {
  // Given a task id, kill it.
  // First validate the argument
  VALUE_t taskid = pop_stack(VM->stack);
  if (taskid.type != VALUE_int) {
    FREE_STR(taskid);
    set_error_item(ERR_RUNTIME_INVALIDARGS, NULL);
    push_stack(VM->stack, VALUE_NIL);
    return nextop;
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

uint8_t *lc_net_input(uint8_t *nextop, ITEM_t *item) {
  // Called by the task which checks for player input.
  // We operate a fair queuing process here.  Everyone
  // gets a turn.  Find the next activity.
  config.lastconn++;
  if (config.lastconn >= config.maxconns) {
    config.lastconn = 0;
  }
  while (config.lastconn < config.maxconns) {
    VALUE_t val = {VALUE_int, {0}};
    // Find some activity.
    switch (line[config.lastconn].status) {
      case LINE_connecting:
        line[config.lastconn].status = LINE_idle;
        // Set the input item to the current line
        val.i = config.lastconn;
        set_item(config.itemroot, config.inputline, val);
        // And return a value from this libcall to say what happened.
        val.i = 1;
        push_stack(VM->stack, val);
        return nextop;
      case LINE_disconnecting:
        destroy_line(&line[config.lastconn]);
        line[config.lastconn].status = LINE_empty;
        // Set the input item to the current line
        val.i = config.lastconn;
        set_item(config.itemroot, config.inputline, val);
        val.i = 2;
        push_stack(VM->stack, val);
        return nextop;
      case LINE_data:
        // Set the input item to the current line
        val.i = config.lastconn;
        set_item(config.itemroot, config.inputline, val);
        // And grab some data.
        VALUE_t str = {VALUE_str, {0}};
        str.s = get_input(&line[config.lastconn]);
        set_item(config.itemroot, config.inputtext, str);
        val.i = 3;
        push_stack(VM->stack, val);
        return nextop;
      default:
        config.lastconn++;
    }
  }
  // No activity found.
  push_stack(VM->stack, VALUE_ZERO);
  return nextop;
}

uint8_t *lc_net_write(uint8_t *nextop, ITEM_t *item) {
  // Write data out to a line
  // Validate the parameters before creating the task.
  VALUE_t out = pop_stack(VM->stack);
  VALUE_t linenum = pop_stack(VM->stack);

  if (linenum.type != VALUE_int || linenum.i < 0 
                                        || linenum.i >= config.maxconns) {
    FREE_STR(out);
    set_error_item(ERR_RUNTIME_INVALIDARGS, NULL);
    push_stack(VM->stack, VALUE_NIL);
    return nextop;
  } else {
    switch(out.type) {
      case VALUE_str:
        telnet_send_text(line[linenum.i].telnet, out.s, strlen(out.s));
        FREE_STR(out);
        break;
      case VALUE_int:
        char buffer[22];
        itoa(out.i, buffer, 10);
        telnet_send_text(line[linenum.i].telnet, buffer, strlen(buffer));
        break;
      case VALUE_nil:
        // Nothing to output
        break;
      case VALUE_bool:
        char *t = "true";
        char *f = "false";
        telnet_send_text(line[linenum.i].telnet, out.i?t:f,
                                                        strlen(out.i?t:f));
        break;
    }
  }
  // Libcalls always return a value
  push_stack(VM->stack, VALUE_NIL);
  return nextop;
}

uint8_t *lc_str_capitalise(uint8_t *nextop, ITEM_t *item) {
  // If the value on the top of the stack is a string, capitalise the
  // first letter.  Otherwise pop the top of the stack and push nil.

  if (VM->stack->stack[VM->stack->current].type == VALUE_str) {
    VM->stack->stack[VM->stack->current].s[0] =
                        toupper(VM->stack->stack[VM->stack->current].s[0]);
  } else {
    pop_stack(VM->stack);
    push_stack(VM->stack, VALUE_NIL);
  }
  return nextop;
}

uint8_t *lc_str_upper(uint8_t *nextop, ITEM_t *item) {
  // If the value on the top of the stack is a string, make it
  // uppercase.  Otherwise pop the top of the stack and push nil.

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

uint8_t *lc_str_lower(uint8_t *nextop, ITEM_t *item) {
  // If the value on the top of the stack is a string, make it
  // lowercase.  Otherwise pop the top of the stack and push nil.

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


typedef struct {
  OP_t func;
  uint8_t args;
  bool present;
} LIBCALL_REG_ENTRY_t;

typedef struct {
  const char *libname;
  const char *callname;
  uint8_t lib_index;
  uint8_t call_index;
  uint8_t args;
  char *lookup_key;
} LIBCALL_NAME_ENTRY_t;

static int libcall_name_entry_cmp(const void *a, const void *b) {
  const LIBCALL_NAME_ENTRY_t *ea = (const LIBCALL_NAME_ENTRY_t *)a;
  const LIBCALL_NAME_ENTRY_t *eb = (const LIBCALL_NAME_ENTRY_t *)b;
  return strcmp(ea->lookup_key, eb->lookup_key);
}

static bool libcall_make_key(const char *libname, const char *callname,
                             char **out_key) {
  size_t liblen = strlen(libname);
  size_t calllen = strlen(callname);
  size_t keylen = liblen + 1 + calllen;
  char *key = GROW_ARRAY(char, NULL, 0, keylen + 1);
  if (!key) {
    return false;
  }
  memcpy(key, libname, liblen);
  key[liblen] = '\x1f';
  memcpy(key + liblen + 1, callname, calllen);
  key[keylen] = '\0';
  *out_key = key;
  return true;
}

static LIBCALL_REG_ENTRY_t *libcall_registry = NULL;
static LIBCALL_NAME_ENTRY_t *libcall_name_registry = NULL;
static size_t libcall_registry_width = 0;
static size_t libcall_registry_height = 0;
static size_t libcall_name_registry_count = 0;
static bool libcall_registry_ready = false;

const LIBCALL_t libcalls[] = {
  {"sys",  "backup",       1, 0, 0, lc_sys_backup},
  {"sys",  "log",          1, 1, 1, lc_sys_log},
  {"sys",  "shutdown",     1, 2, 0, lc_sys_shutdown},
  {"sys",  "abort",        1, 3, 0, lc_sys_abort},
  {"sys",  "compile",      1, 4, 1, lc_sys_compile},
  {"task", "newgametask",  2, 0, 3, lc_task_newgametask},
  {"task", "killtask",     2, 1, 1, lc_task_killtask},
  {"net",  "input",        3, 0, 0, lc_net_input},
  {"net",  "write",        3, 1, 2, lc_net_write},
  {"str",  "capitalise",   4, 0, 1, lc_str_capitalise},
  {"str",  "upper",        4, 1, 1, lc_str_upper},
  {"str",  "lower",        4, 2, 1, lc_str_lower},
  {NULL,   NULL,          -1, -1, 0, NULL}  // End marker
};

static size_t libcall_registry_index(uint8_t lib_index, uint8_t call_index) {
  return ((size_t)lib_index * libcall_registry_width) + (size_t)call_index;
}

bool libcall_init_registry(void) {
  if (libcall_registry_ready) {
    return true;
  }

  int8_t max_lib_index = -1;
  int8_t max_call_index = -1;
  size_t count = 0;
  for (size_t i = 0; libcalls[i].libname != NULL; i++) {
    if (libcalls[i].lib_index < 0 || libcalls[i].call_index < 0) {
      return false;
    }
    if (libcalls[i].lib_index > max_lib_index) {
      max_lib_index = libcalls[i].lib_index;
    }
    if (libcalls[i].call_index > max_call_index) {
      max_call_index = libcalls[i].call_index;
    }
    count++;
  }

  libcall_registry_height = (size_t)max_lib_index + 1;
  libcall_registry_width = (size_t)max_call_index + 1;
  size_t dense_count = libcall_registry_height * libcall_registry_width;

  libcall_registry = GROW_ARRAY(LIBCALL_REG_ENTRY_t, NULL, 0, dense_count);
  libcall_name_registry = GROW_ARRAY(LIBCALL_NAME_ENTRY_t, NULL, 0, count);
  if (!libcall_registry || !libcall_name_registry) {
    return false;
  }
  memset(libcall_registry, 0, sizeof(LIBCALL_REG_ENTRY_t) * dense_count);

  for (size_t i = 0; i < count; i++) {
    uint8_t lib_index = (uint8_t)libcalls[i].lib_index;
    uint8_t call_index = (uint8_t)libcalls[i].call_index;
    size_t dense_index = libcall_registry_index(lib_index, call_index);
    if (libcall_registry[dense_index].present) {
      return false;
    }
    libcall_registry[dense_index].func = libcalls[i].func;
    libcall_registry[dense_index].args = libcalls[i].args;
    libcall_registry[dense_index].present = true;

    libcall_name_registry[i].libname = libcalls[i].libname;
    libcall_name_registry[i].callname = libcalls[i].callname;
    libcall_name_registry[i].lib_index = lib_index;
    libcall_name_registry[i].call_index = call_index;
    libcall_name_registry[i].args = libcalls[i].args;
    libcall_name_registry[i].lookup_key = NULL;
    if (!libcall_make_key(libcalls[i].libname, libcalls[i].callname,
                          &libcall_name_registry[i].lookup_key)) {
      return false;
    }
  }

  qsort(libcall_name_registry, count, sizeof(LIBCALL_NAME_ENTRY_t),
        libcall_name_entry_cmp);

  for (size_t i = 1; i < count; i++) {
    if (strcmp(libcall_name_registry[i - 1].lookup_key,
               libcall_name_registry[i].lookup_key) == 0) {
      return false;
    }
  }

  libcall_name_registry_count = count;
  libcall_registry_ready = true;
  return true;
}

bool libcall_validate_registry(void) {
  if (!libcall_init_registry()) {
    return false;
  }

  for (size_t i = 0; libcalls[i].libname != NULL; i++) {
    uint8_t lib_index = (uint8_t)libcalls[i].lib_index;
    uint8_t call_index = (uint8_t)libcalls[i].call_index;
    if (lib_index >= libcall_registry_height || call_index >= libcall_registry_width) {
      return false;
    }
    size_t dense_index = libcall_registry_index(lib_index, call_index);
    if (!libcall_registry[dense_index].present) {
      return false;
    }
    if (libcall_registry[dense_index].func != libcalls[i].func ||
        libcall_registry[dense_index].args != libcalls[i].args) {
      return false;
    }
  }
  return true;
}

bool libcall_lookup(const char *libname, const char *callname,
                   uint8_t *lib_index, uint8_t *call_index, uint8_t *args) {
  if (!libcall_init_registry()) {
    return false;
  }

  char *lookup_key = NULL;
  if (!libcall_make_key(libname, callname, &lookup_key)) {
    return false;
  }

  LIBCALL_NAME_ENTRY_t needle = {.lookup_key = lookup_key};
  LIBCALL_NAME_ENTRY_t *entry = bsearch(&needle, libcall_name_registry,
      libcall_name_registry_count, sizeof(LIBCALL_NAME_ENTRY_t),
      libcall_name_entry_cmp);
  FREE_ARRAY(char, lookup_key, strlen(lookup_key) + 1);

  if (!entry) {
    return false;
  }

  *lib_index = entry->lib_index;
  *call_index = entry->call_index;
  *args = entry->args;
  return true;
}

bool libcall_names_unique(const LIBCALL_t *calls) {
  for (size_t i = 0; calls[i].libname != NULL; i++) {
    for (size_t j = i + 1; calls[j].libname != NULL; j++) {
      if (strcmp(calls[i].libname, calls[j].libname) == 0 &&
          strcmp(calls[i].callname, calls[j].callname) == 0) {
        return false;
      }
    }
  }
  return true;
}

void *libcall_func(uint8_t lib, uint8_t call) {
  if (!libcall_init_registry()) {
    return NULL;
  }
  if (lib >= libcall_registry_height || call >= libcall_registry_width) {
    return NULL;
  }

  LIBCALL_REG_ENTRY_t entry = libcall_registry[libcall_registry_index(lib, call)];
  return entry.present ? entry.func : NULL;
}
