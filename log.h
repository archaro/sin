// Basic logging function.
// Sends stderr to stdout, or to a logfile, depending on the option which was
// set on the command line.

#pragma once

#include <stdio.h>
#include <stdarg.h>
#include <stdbool.h>

bool log_to_file(const char *logfile);
void close_log();
void logmsg(const char *msg, ...);
void logerr(const char *msg, ...);
