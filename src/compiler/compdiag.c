// Compiler diagnostics

// Licensed under the MIT License - see LICENSE file for details.

#include "compdiag.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "error.h"
#include "memory.h"

static char *compdiag_alloc_detail(const char *phase, const char *msg) {
  const char *safe_phase = phase ? phase : "comp";
  const char *safe_msg = msg ? msg : "unknown";

  int needed = snprintf(NULL, 0, "%s: %s", safe_phase, safe_msg);
  if (needed < 0) return NULL;

  char *buf = GROW_ARRAY(char, NULL, 0, (size_t)needed + 1);
  if (!buf) return NULL;

  snprintf(buf, (size_t)needed + 1, "%s: %s", safe_phase, safe_msg);
  return buf;
}

bool compdiag_set_once(int8_t *current_errnum, char **errdetail,
                       int8_t new_errnum, const char *phase,
                       const char *detail) {
  if (!current_errnum || *current_errnum != ERR_NOERROR) return false;

  *current_errnum = new_errnum;
  if (errdetail) {
    compdiag_reset_detail(errdetail);
    *errdetail = compdiag_alloc_detail(phase, detail);
  }
  return true;
}

bool compdiag_setf_once(int8_t *current_errnum, char **errdetail,
                        int8_t new_errnum, const char *phase,
                        const char *fmt, ...) {
  if (!current_errnum || *current_errnum != ERR_NOERROR) return false;

  va_list args;
  va_start(args, fmt);
  int needed = vsnprintf(NULL, 0, fmt, args);
  va_end(args);
  if (needed < 0) {
    return compdiag_set_once(current_errnum, errdetail, new_errnum, phase,
                             "formatting error");
  }

  char *msg = GROW_ARRAY(char, NULL, 0, (size_t)needed + 1);
  if (!msg) {
    return compdiag_set_once(current_errnum, errdetail, new_errnum, phase,
                             "out of memory");
  }

  va_start(args, fmt);
  vsnprintf(msg, (size_t)needed + 1, fmt, args);
  va_end(args);

  bool recorded = compdiag_set_once(current_errnum, errdetail, new_errnum, phase, msg);
  FREE_ARRAY(char, msg, (size_t)needed + 1);
  return recorded;
}

void compdiag_reset_detail(char **errdetail) {
  if (!errdetail || !*errdetail) return;

  size_t len = strlen(*errdetail);
  FREE_ARRAY(char, *errdetail, len + 1);
  *errdetail = NULL;
}

void compiler_diag_init(CompilerDiagnostic *d) {
  if (!d) return;
  d->code = 0;
  d->phase = DIAG_PHASE_NONE;
  d->message = NULL;
  d->line = d->column = d->span = -1;
  d->has_loc = false;
}

void compiler_diag_reset(CompilerDiagnostic *d) {
  if (!d) return;
  free(d->message);
  compiler_diag_init(d);
}

void compiler_diag_set(CompilerDiagnostic *d, int8_t code, DiagPhase phase,
                       const char *message) {
  if (!d) return;
  compiler_diag_reset(d);
  d->code = code;
  d->phase = phase;
  d->message = strdup(message ? message : "");
}

const char *compiler_diag_phase_name(DiagPhase p) {
  switch (p) {
    case DIAG_PHASE_PARSE:
      return "PARSE";
    case DIAG_PHASE_SEMANT:
      return "SEMANT";
    case DIAG_PHASE_LOWER:
      return "LOWER";
    case DIAG_PHASE_IR_VALIDATE:
      return "IR_VALIDATE";
    case DIAG_PHASE_EMITBC:
      return "EMITBC";
    case DIAG_PHASE_IO:
      return "IO";
    default:
      return "NONE";
  }
}
