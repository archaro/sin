// Simple log facility
// Log to file if that is specified on the command line.

// Licensed under the MIT License - see LICENSE file for details.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "log.h"

static LogLevel current_log_level = LOG_LEVEL_NORMAL;

void log_set_level(LogLevel level) { current_log_level = level; }

LogLevel log_get_level(void) { return current_log_level; }

bool log_is_verbose(void) { return current_log_level >= LOG_LEVEL_VERBOSE; }

bool log_to_file(const char *logfile) {
  // Log to file.  The logfile parameter is suffixed with .log and .err
  // for stdout and stderr respectively.
  size_t len = strlen(logfile) + sizeof(".err");
  char *newlog = malloc(len);
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
  free(newlog);
  return result;
}

void close_log() {
  if (!freopen("/dev/tty","a",stderr)) {
    fprintf(stderr, "Unable to restore stderr to /dev/tty\n");
  }
  if (!freopen("/dev/tty","a",stdout)) {
    fprintf(stderr, "Unable to restore stdout to /dev/tty\n");
  }
}

void logerr(const char *msg, ...) {
  va_list args;
  va_start(args, msg);
  vfprintf(stderr, msg, args);
  fflush(stderr);
  va_end(args);
}

void logmsg(const char *msg, ...) {
  if (current_log_level < LOG_LEVEL_NORMAL) return;
  va_list args;
  va_start(args, msg);
  vfprintf(stdout, msg, args);
  fflush(stdout);
  va_end(args);
}

void logstatus(const char *msg, ...) {
  if (current_log_level < LOG_LEVEL_NORMAL) return;
  va_list args;
  va_start(args, msg);
  vfprintf(stderr, msg, args);
  fflush(stderr);
  va_end(args);
}

void logverbose(const char *msg, ...) {
  if (current_log_level < LOG_LEVEL_VERBOSE) return;
  va_list args;
  va_start(args, msg);
  vfprintf(stderr, msg, args);
  fflush(stderr);
  va_end(args);
}
