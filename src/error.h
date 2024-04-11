// Error messages

#pragma once

// How big should the error message table be?
#define MAXERRORS                 21

#define ERR_NOERROR               0

#define ERR_COMP_SYNTAX           1
#define ERR_COMP_MAXDEPTH         2
#define ERR_COMP_TOOMANYLOCALS    3
#define ERR_COMP_LOCALBEFOREDEF   4
#define ERR_COMP_UNKNOWNCHAR      5

#define ERR_RUNTIME_SIGUSR1       20

extern const char *errmsg[];

void init_errmsg();
