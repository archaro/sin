// Simple log facility
// Log to file if that is specified on the command line.

// Licensed under the MIT License - see LICENSE file for details.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "log.h"
#include "memory.h"

bool log_to_file(const char *logfile) {
  // Log to file.  The logfile parameter is suffixed with .log and .err
  // for stdout and stderr respectively.
  int len = strlen(logfile) + 5;
  char *newlog = GROW_ARRAY(char, NULL, 0, len);
  bool result = false;
  snprintf(newlog, len, "%s.log", logfile);
  if (!freopen(newlog,"a",stdout)) {
    fprintf(stderr, "Unable to open logfile: %s\n", newlog);
  } else {
    snprintf(newlog, len, "%s.err", logfile);
    if (!freopen(newlog,"a",stderr)) {
      fprintf(stderr, "Unable to open error logfile: %s\n", newlog);
    } else {
      result = true;
    }
  }
  FREE_ARRAY(char, newlog, len);
  return result;
}

void close_log() {
  freopen("/dev/tty","a",stderr);
  freopen("/dev/tty","a",stdout);
}

void logerr(const char *msg, ...) {
  va_list args;
  va_start(args, msg);
  vfprintf(stderr, msg, args);
  fflush(stderr);
  va_end(args);
}

void logmsg(const char *msg, ...) {
  va_list args;
  va_start(args, msg);
  vfprintf(stdout, msg, args);
  fflush(stdout);
  va_end(args);
}

