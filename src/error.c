#include "error.h"

const char *errmsg[MAXERRORS];

void init_errmsg() {
  errmsg[ERR_NOERROR] = "No error.";
  errmsg[ERR_COMP_SYNTAX] = "Syntax error.";
  errmsg[ERR_COMP_MAXDEPTH] = "Maximum nesting depth reached.";
  errmsg[ERR_COMP_TOOMANYLOCALS] = "Too many local variables.";
  errmsg[ERR_COMP_LOCALBEFOREDEF] = "Local used before definition.";
  errmsg[ERR_COMP_UNKNOWNCHAR] = "Unknown character in input.";
  errmsg[ERR_RUNTIME_SIGUSR1] = "Restarting due to SIGUSR1.";
}
