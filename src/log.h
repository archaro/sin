// Basic logging function.
// Sends stderr to stdout, or to a logfile, depending on the option which was
// set on the command line.

#pragma once

#include <stdio.h>
#include <stdarg.h>
#include <stdbool.h>

#ifdef DEBUG
#define DEBUG_LOG(...) logmsg(__VA_ARGS__)
#else
#define DEBUG_LOG(...) ((void)0)
#endif

bool log_to_file(const char *logfile);
void close_log();
void logmsg(const char *msg, ...);
void logerr(const char *msg, ...);
