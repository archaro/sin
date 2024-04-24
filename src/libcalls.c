#include <time.h>
#include <string.h>

#include "util.h"
#include "config.h"
#include "libcalls.h"
#include "log.h"
#include "stack.h"
#include "item.h"

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
  push_stack(VM.stack, VALUE_NIL);
  return nextop;
}

uint8_t *lc_sys_log(uint8_t *nextop, ITEM_t *item) {
  // Pop the top of the stack and write it to the syslog
  // Try to do something sensible if the type is not a string.
  VALUE_t val = pop_stack(VM.stack);
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
  push_stack(VM.stack, VALUE_NIL);
  return nextop;
}

const LIBCALL_t libcalls[] = {
  {"sys", "backup", 1, 0, 0, lc_sys_backup},
  {"sys", "log", 1, 1, 1, lc_sys_log},
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

