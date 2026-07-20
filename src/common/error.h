// Error messages

// Licensed under the MIT License - see LICENSE file for details.

#pragma once

// How big should the error message table be?
#define MAXERRORS                 32

#define ERR_NOERROR               0

#define ERR_COMP_SYNTAX           1
#define ERR_COMP_TOOMANYLOCALS    3
#define ERR_COMP_LOCALBEFOREDEF   4
#define ERR_COMP_UNKNOWNCHAR      5
#define ERR_COMP_INUSE            8
#define ERR_COMP_TOOMANYPARAMS    9
#define ERR_COMP_TOOMANYARGS      10
#define ERR_COMP_UNKNOWN          11

#define ERR_RUNTIME_SIGUSR1       20
#define ERR_RUNTIME_INVALIDARGS   21
#define ERR_RUNTIME_NOSUCHITEM    22
#define ERR_RUNTIME_TRUNCATED     23
#define ERR_RUNTIME_INVLIB        24
#define ERR_RUNTIME_BYTECODE      25
#define ERR_RUNTIME_INVALIDITEM   26
#define ERR_RUNTIME_INTERNAL      27
#define ERR_RUNTIME_PERSISTENCE   28
#define ERR_RUNTIME_SOURCE        29

#define ERR_NETWORK_ERROR         30

extern const char *errmsg[];

void init_errmsg(void);
