// Error codes

// Licensed under the MIT License - see LICENSE file for details.

#include "error.h"

const char *errmsg[MAXERRORS];

void init_errmsg(void) {
  errmsg[ERR_NOERROR] = "No error.";
  errmsg[ERR_COMP_SYNTAX] = "Syntax error.";
  errmsg[ERR_COMP_TOOMANYLOCALS] = "Too many local variables.";
  errmsg[ERR_COMP_LOCALBEFOREDEF] = "Local used before definition.";
  errmsg[ERR_COMP_UNKNOWNCHAR] = "Unknown character in input.";
  errmsg[ERR_COMP_INUSE] = "Item in use; cannot replace it.";
  errmsg[ERR_COMP_TOOMANYPARAMS] = "Too many parameters in item definition.";
  errmsg[ERR_COMP_TOOMANYARGS] = "Too many arguments passed to item.";
  errmsg[ERR_COMP_UNKNOWN] = "Unknown compiler error.";
  errmsg[ERR_RUNTIME_SIGUSR1] = "Restarting due to SIGUSR1.";
  errmsg[ERR_RUNTIME_INVALIDARGS] = "Invalid arguments to library call.";
  errmsg[ERR_RUNTIME_NOSUCHITEM] = "Item does not exist.";
  errmsg[ERR_RUNTIME_TRUNCATED] = "Truncated bytecode.";
  errmsg[ERR_RUNTIME_INVLIB] = "Invalid libcall.";
  errmsg[ERR_RUNTIME_BYTECODE] = "Invalid bytecode.";
  errmsg[ERR_RUNTIME_INVALIDITEM] = "Invalid item name.";
  errmsg[ERR_RUNTIME_INTERNAL] = "Internal runtime error.";
  errmsg[ERR_RUNTIME_PERSISTENCE] = "Itemstore persistence failed.";
  errmsg[ERR_NETWORK_ERROR] = "Network error.";
}
