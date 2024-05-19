// Basic logging function.
// Sends stderr to stdout, or to a logfile, depending on the option which
// set os n the command line.

// Licensed under the MIT License - see LICENSE file for details.

#pragma once

#include <stdio.h>
#include <stdarg.h>
#include <stdbool.h>

#ifdef DEBUG
#define DEBUG_LOG(...) logmsg(__VA_ARGS__)
#else
#define DEBUG_LOG(...) ((void)0)
#endif

#ifdef ITEMDEBUG
#define ITEMDEBUG_LOG(...) logmsg(__VA_ARGS__)
#else
#define ITEMDEBUG_LOG(...) ((void)0)
#endif

#ifdef STRINGDEBUG
#define STRINGDEBUG_LOG(...) logmsg(__VA_ARGS__)
#else
#define STRINGDEBUG_LOG(...) ((void)0)
#endif

#ifdef DISASS
#define DISASS_LOG(...) logmsg(__VA_ARGS__)
#else
#define DISASS_LOG(...) ((void)0)
#endif

bool log_to_file(const char *logfile);
void close_log();
void logmsg(const char *msg, ...);
void logerr(const char *msg, ...);
