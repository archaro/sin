// Basic logging function.
// Sends stderr to stdout, or to a logfile, depending on the option which
// set os n the command line.

// Licensed under the MIT License - see LICENSE file for details.

#pragma once

#include <stdio.h>
#include <stdarg.h>
#include <stdbool.h>


typedef enum {
  LOG_LEVEL_QUIET = 0,
  LOG_LEVEL_NORMAL = 1,
  LOG_LEVEL_VERBOSE = 2
} LogLevel;

void log_set_level(LogLevel level);
LogLevel log_get_level(void);
bool log_is_verbose(void);
bool log_to_file(const char *logfile);
void close_log();
void logmsg(const char *msg, ...);
void logstatus(const char *msg, ...);
void logverbose(const char *msg, ...);
void logerr(const char *msg, ...);
