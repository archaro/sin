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
    // Compile failed; release owned errdetail/out/val.s exactly once.
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
    // Could not create temp item (likely in-use/name conflict).
    // out->bytecode/out and val.s are still owned here and must be freed once.
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
  // Success path: this is the only free on this path (the !taskitem branch returns).
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
    // taskid may only own heap memory when it is a string; FREE_STR is a safe no-op otherwise.
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
  uint8_t token;
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
static OP_t libcall_token_registry[256] = {0};
static bool libcall_token_present[256] = {0};
static bool libcall_registry_ready = false;


static bool libcall_args_in_range(uint8_t args) {
  return args <= 32;
}

typedef struct LIBCALL_KEY_NODE {
  char *key;
  struct LIBCALL_KEY_NODE *next;
} LIBCALL_KEY_NODE_t;

static uint32_t libcall_key_hash(const char *key) {
  // djb2 hash
  uint32_t hash = 5381U;
  for (unsigned char c = (unsigned char)*key; c != '\0'; c = (unsigned char)*++key) {
    hash = ((hash << 5) + hash) + c;
  }
  return hash;
}

static void libcall_key_set_free(LIBCALL_KEY_NODE_t **buckets, size_t bucket_count) {
  if (!buckets) return;
  for (size_t i = 0; i < bucket_count; i++) {
    LIBCALL_KEY_NODE_t *node = buckets[i];
    while (node) {
      LIBCALL_KEY_NODE_t *next = node->next;
      if (node->key) {
        FREE_ARRAY(char, node->key, strlen(node->key) + 1);
      }
      FREE_ARRAY(LIBCALL_KEY_NODE_t, node, 1);
      node = next;
    }
  }
  FREE_ARRAY(LIBCALL_KEY_NODE_t *, buckets, bucket_count);
}

static bool libcall_registry_fail(const char *msg, const LIBCALL_t *entry, size_t idx, bool fail_fast) {
  logerr("FATAL: libcall registry self-check failed: %s (entry %zu: %s.%s lib=%d call=%d args=%u)\n",
         msg, idx,
         entry && entry->libname ? entry->libname : "<null-lib>",
         entry && entry->callname ? entry->callname : "<null-call>",
         entry ? (int)entry->lib_index : -1,
         entry ? (int)entry->call_index : -1,
         entry ? (unsigned)entry->args : 0U);
  if (fail_fast) {
    abort();
  }
  return false;
}

bool libcall_registry_self_check(const LIBCALL_t *calls, bool fail_fast) {
  if (!calls) return libcall_registry_fail("registry pointer is null", NULL, 0, fail_fast);

  bool seen_lib[256] = {0};
  bool seen_pair[256][256] = {{0}};
  size_t key_bucket_count = 257;
  LIBCALL_KEY_NODE_t **seen_keys = GROW_ARRAY(LIBCALL_KEY_NODE_t *, NULL, 0, key_bucket_count);
  if (!seen_keys) {
    return libcall_registry_fail("failed to allocate textual key set", NULL, 0, fail_fast);
  }
  memset(seen_keys, 0, sizeof(*seen_keys) * key_bucket_count);
  for (size_t i = 0; calls[i].libname != NULL || calls[i].callname != NULL; i++) {
    const LIBCALL_t *e = &calls[i];
    if (!e->libname || !e->callname || !e->func) {
      libcall_key_set_free(seen_keys, key_bucket_count);
      return libcall_registry_fail("entry requires non-null libname/callname/func", e, i, fail_fast);
    }
    if (e->lib_index < 0 || e->call_index < 0) {
      libcall_key_set_free(seen_keys, key_bucket_count);
      return libcall_registry_fail("negative lib_index/call_index", e, i, fail_fast);
    }
    if (!libcall_args_in_range(e->args)) {
      libcall_key_set_free(seen_keys, key_bucket_count);
      return libcall_registry_fail("args out of acceptable range", e, i, fail_fast);
    }

    char *lookup_key = NULL;
    if (!libcall_make_key(e->libname, e->callname, &lookup_key)) {
      libcall_key_set_free(seen_keys, key_bucket_count);
      return libcall_registry_fail("failed to allocate textual key", e, i, fail_fast);
    }
    size_t bucket = (size_t)(libcall_key_hash(lookup_key) % key_bucket_count);
    for (LIBCALL_KEY_NODE_t *node = seen_keys[bucket]; node; node = node->next) {
      if (strcmp(node->key, lookup_key) == 0) {
        FREE_ARRAY(char, lookup_key, strlen(lookup_key) + 1);
        libcall_key_set_free(seen_keys, key_bucket_count);
        return libcall_registry_fail("duplicate textual key libname.callname", e, i, fail_fast);
      }
    }
    LIBCALL_KEY_NODE_t *new_node = GROW_ARRAY(LIBCALL_KEY_NODE_t, NULL, 0, 1);
    if (!new_node) {
      FREE_ARRAY(char, lookup_key, strlen(lookup_key) + 1);
      libcall_key_set_free(seen_keys, key_bucket_count);
      return libcall_registry_fail("failed to allocate textual key node", e, i, fail_fast);
    }
    new_node->key = lookup_key;
    new_node->next = seen_keys[bucket];
    seen_keys[bucket] = new_node;

    uint8_t li = (uint8_t)e->lib_index, ci = (uint8_t)e->call_index;
    if (seen_pair[li][ci]) {
      libcall_key_set_free(seen_keys, key_bucket_count);
      return libcall_registry_fail("duplicate numeric key (lib_index,call_index)", e, i, fail_fast);
    }
    seen_pair[li][ci] = true;
    seen_lib[li] = true;
  }

  int max_lib = -1;
  for (int i=0;i<256;i++) if (seen_lib[i]) max_lib = i;
  for (int i=0;i<=max_lib;i++) if (!seen_lib[i] && i!=0) {
    libcall_key_set_free(seen_keys, key_bucket_count);
    return libcall_registry_fail("lib_index values must be contiguous from 1..max", &calls[0], 0, fail_fast);
  }
  libcall_key_set_free(seen_keys, key_bucket_count);
  return true;
}
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

#define LIBCALL_FUNC_SIGNATURE_GUARD(name) \
  _Static_assert(__builtin_types_compatible_p(__typeof__(&(name)), OP_t), \
                 "libcall function must match OP_t signature")

LIBCALL_FUNC_SIGNATURE_GUARD(lc_sys_backup);
LIBCALL_FUNC_SIGNATURE_GUARD(lc_sys_log);
LIBCALL_FUNC_SIGNATURE_GUARD(lc_sys_shutdown);
LIBCALL_FUNC_SIGNATURE_GUARD(lc_sys_abort);
LIBCALL_FUNC_SIGNATURE_GUARD(lc_sys_compile);
LIBCALL_FUNC_SIGNATURE_GUARD(lc_task_newgametask);
LIBCALL_FUNC_SIGNATURE_GUARD(lc_task_killtask);
LIBCALL_FUNC_SIGNATURE_GUARD(lc_net_input);
LIBCALL_FUNC_SIGNATURE_GUARD(lc_net_write);
LIBCALL_FUNC_SIGNATURE_GUARD(lc_str_capitalise);
LIBCALL_FUNC_SIGNATURE_GUARD(lc_str_upper);
LIBCALL_FUNC_SIGNATURE_GUARD(lc_str_lower);

static size_t libcall_registry_index(uint8_t lib_index, uint8_t call_index) {
  return ((size_t)lib_index * libcall_registry_width) + (size_t)call_index;
}

void libcall_registry_free_all(void) {
  if (libcall_name_registry) {
    for (size_t i = 0; i < libcall_name_registry_count; i++) {
      if (libcall_name_registry[i].lookup_key) {
        FREE_ARRAY(char, libcall_name_registry[i].lookup_key,
                   strlen(libcall_name_registry[i].lookup_key) + 1);
        libcall_name_registry[i].lookup_key = NULL;
      }
    }
    FREE_ARRAY(LIBCALL_NAME_ENTRY_t, libcall_name_registry, libcall_name_registry_count);
  }

  size_t dense_count = libcall_registry_height * libcall_registry_width;
  if (libcall_registry) {
    FREE_ARRAY(LIBCALL_REG_ENTRY_t, libcall_registry, dense_count);
  }

  libcall_registry = NULL;
  libcall_name_registry = NULL;
  libcall_registry_width = 0;
  libcall_registry_height = 0;
  libcall_name_registry_count = 0;
  memset(libcall_token_registry, 0, sizeof(libcall_token_registry));
  memset(libcall_token_present, 0, sizeof(libcall_token_present));
  libcall_registry_ready = false;
}

bool libcall_init_registry(void) {
  LIBCALL_REG_ENTRY_t *tmp_registry = NULL;
  LIBCALL_NAME_ENTRY_t *tmp_name_registry = NULL;
  size_t tmp_registry_width = 0;
  size_t tmp_registry_height = 0;
  size_t tmp_count = 0;
  size_t dense_count = 0;
  OP_t tmp_token_registry[256] = {0};
  bool tmp_token_present[256] = {0};

  if (libcall_registry_ready) {
    return true;
  }

  if (!libcall_registry_self_check(libcalls, false)) {
    return false;
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

  tmp_registry_height = (size_t)max_lib_index + 1;
  tmp_registry_width = (size_t)max_call_index + 1;
  tmp_count = count;
  dense_count = tmp_registry_height * tmp_registry_width;

  tmp_registry = GROW_ARRAY(LIBCALL_REG_ENTRY_t, NULL, 0, dense_count);
  tmp_name_registry = GROW_ARRAY(LIBCALL_NAME_ENTRY_t, NULL, 0, tmp_count);
  if (!tmp_registry || !tmp_name_registry) {
    goto fail;
  }
  memset(tmp_registry, 0, sizeof(LIBCALL_REG_ENTRY_t) * dense_count);
  memset(tmp_name_registry, 0, sizeof(LIBCALL_NAME_ENTRY_t) * tmp_count);

  for (size_t i = 0; i < tmp_count; i++) {
    uint8_t lib_index = (uint8_t)libcalls[i].lib_index;
    uint8_t call_index = (uint8_t)libcalls[i].call_index;
    size_t dense_index = ((size_t)lib_index * tmp_registry_width) + (size_t)call_index;
    if (tmp_registry[dense_index].present) {
      goto fail;
    }
    tmp_registry[dense_index].func = libcalls[i].func;
    tmp_registry[dense_index].args = libcalls[i].args;
    tmp_registry[dense_index].present = true;

    tmp_name_registry[i].libname = libcalls[i].libname;
    tmp_name_registry[i].callname = libcalls[i].callname;
    tmp_name_registry[i].lib_index = lib_index;
    tmp_name_registry[i].call_index = call_index;
    tmp_name_registry[i].args = libcalls[i].args;
    tmp_name_registry[i].token = (uint8_t)i;
    tmp_name_registry[i].lookup_key = NULL;
    if (!libcall_make_key(libcalls[i].libname, libcalls[i].callname,
                          &tmp_name_registry[i].lookup_key)) {
      goto fail;
    }
    tmp_token_registry[(uint8_t)i] = libcalls[i].func;
    tmp_token_present[(uint8_t)i] = true;
  }

  qsort(tmp_name_registry, tmp_count, sizeof(LIBCALL_NAME_ENTRY_t),
        libcall_name_entry_cmp);

  for (size_t i = 1; i < tmp_count; i++) {
    if (strcmp(tmp_name_registry[i - 1].lookup_key,
               tmp_name_registry[i].lookup_key) == 0) {
      goto fail;
    }
  }

  libcall_registry = tmp_registry;
  libcall_name_registry = tmp_name_registry;
  libcall_registry_width = tmp_registry_width;
  libcall_registry_height = tmp_registry_height;
  libcall_name_registry_count = tmp_count;
  memcpy(libcall_token_registry, tmp_token_registry, sizeof(libcall_token_registry));
  memcpy(libcall_token_present, tmp_token_present, sizeof(libcall_token_present));
  libcall_registry_ready = true;
  return true;

fail:
  for (size_t i = 0; i < tmp_count; i++) {
    if (tmp_name_registry && tmp_name_registry[i].lookup_key) {
      FREE_ARRAY(char, tmp_name_registry[i].lookup_key,
                 strlen(tmp_name_registry[i].lookup_key) + 1);
      tmp_name_registry[i].lookup_key = NULL;
    }
  }
  if (tmp_name_registry) {
    FREE_ARRAY(LIBCALL_NAME_ENTRY_t, tmp_name_registry, tmp_count);
  }
  if (tmp_registry) {
    FREE_ARRAY(LIBCALL_REG_ENTRY_t, tmp_registry, dense_count);
  }
  return false;
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


bool libcall_lookup_token(const char *libname, const char *callname, uint8_t *token, uint8_t *args) {
  if (!libcall_init_registry()) return false;
  char *lookup_key = NULL;
  if (!libcall_make_key(libname, callname, &lookup_key)) return false;
  LIBCALL_NAME_ENTRY_t needle = {.lookup_key = lookup_key};
  LIBCALL_NAME_ENTRY_t *entry = bsearch(&needle, libcall_name_registry,
      libcall_name_registry_count, sizeof(LIBCALL_NAME_ENTRY_t),
      libcall_name_entry_cmp);
  FREE_ARRAY(char, lookup_key, strlen(lookup_key) + 1);
  if (!entry) return false;
  if (token) *token = entry->token;
  if (args) *args = entry->args;
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

OP_t libcall_func_token(uint8_t token) {
  if (!libcall_init_registry()) return NULL;
  if (!libcall_token_present[token]) return NULL;
  return libcall_token_registry[token];
}
