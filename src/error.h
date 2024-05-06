// Error messages

// Licensed under the MIT License - see LICENSE file for details.

#pragma once

// How big should the error message table be?
#define MAXERRORS                 31

#define ERR_NOERROR               0

#define ERR_COMP_SYNTAX           1
#define ERR_COMP_MAXDEPTH         2
#define ERR_COMP_TOOMANYLOCALS    3
#define ERR_COMP_LOCALBEFOREDEF   4
#define ERR_COMP_UNKNOWNCHAR      5
#define ERR_COMP_UNKNOWNLIB       6
#define ERR_COMP_WRONGARGS        7
#define ERR_COMP_INUSE            8

#define ERR_RUNTIME_SIGUSR1       20
#define ERR_RUNTIME_INVALIDARGS   21
#define ERR_RUNTIME_NOSUCHITEM    22

extern const char *errmsg[];

void init_errmsg();
