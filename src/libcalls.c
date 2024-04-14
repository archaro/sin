#include <string.h>

#include "libcalls.h"
#include "log.h"
#include "vm.h"
#include "stack.h"
#include "interpret.h"
#include "item.h"

// This is the virtual machine object, defined in sin.c
extern VM_t vm;

// This is defined in sin.c, and it is the root of the itemstore.
// It must be initialised before any function in this file is called.
extern ITEM_t *itemroot;

typedef uint8_t *(*OP_t)(uint8_t *nextop, ITEM_t *item);

const LIBCALL_t libcalls[] = {
  {"sys", "backup", 1, 0, 0},
  {NULL, NULL, -1, -1, 0}  // End marker
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

