#include "error.h"

const char *errmsg[MAXERRORS];

void init_errmsg() {
  errmsg[ERR_NOERROR] = "No error.";
  errmsg[ERR_COMP_SYNTAX] = "Syntax error.";
  errmsg[ERR_COMP_MAXDEPTH] = "Maximum nesting depth reached.";
  errmsg[ERR_COMP_TOOMANYLOCALS] = "Too many local variables.";
  errmsg[ERR_COMP_LOCALBEFOREDEF] = "Local used before definition.";
  errmsg[ERR_COMP_UNKNOWNCHAR] = "Unknown character in input.";
  errmsg[ERR_COMP_UNKNOWNLIB] = "Unknown library call.";
  errmsg[ERR_COMP_WRONGARGS] = "Wrong number of arguments to library call.";
  errmsg[ERR_COMP_INUSE] = "Item in use; cannot replace it.";
  errmsg[ERR_RUNTIME_SIGUSR1] = "Restarting due to SIGUSR1.";
  errmsg[ERR_RUNTIME_INVALIDARGS] = "Invalid arguments to library call.";
  errmsg[ERR_RUNTIME_NOSUCHITEM] = "Item does not exist.";
}
