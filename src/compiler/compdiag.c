// Compiler diagnostics

// Licensed under the MIT License - see LICENSE file for details.

#include "compiler/compdiag.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "error.h"
static char *compiler_diag_strdup(const char *s) {
  const char *safe = s ? s : "";
  size_t len = strlen(safe);
  char *copy = malloc(len + 1);
  if (!copy) return NULL;
  memcpy(copy, safe, len + 1);
  return copy;
}

static void compiler_diag_free(char *s) {
  free(s);
}

static char *compdiag_format_message(const char *phase, const char *msg) {
  const char *safe_phase = phase ? phase : "comp";
  const char *safe_msg = msg ? msg : "unknown";

  int needed = snprintf(NULL, 0, "%s: %s", safe_phase, safe_msg);
  if (needed < 0) return NULL;

  char *buf = malloc((size_t)needed + 1);
  if (!buf) return NULL;

  snprintf(buf, (size_t)needed + 1, "%s: %s", safe_phase, safe_msg);
  return buf;
}

bool compdiag_set_once_diag(int8_t *current_errnum, CompilerDiagnostic *diag,
                            int8_t new_errnum, DiagPhase diag_phase,
                            const char *phase, const char *message) {
  if (!current_errnum || *current_errnum != ERR_NOERROR) return false;

  *current_errnum = new_errnum;
  char *formatted = compdiag_format_message(phase, message);
  if (diag) {
    compiler_diag_set(diag, new_errnum, diag_phase,
                     formatted ? formatted : message);
  }
  compiler_diag_free(formatted);
  return true;
}
bool compdiag_setf_once_diag(int8_t *current_errnum, CompilerDiagnostic *diag,
                             int8_t new_errnum, DiagPhase diag_phase,
                             const char *phase, const char *fmt, ...) {
  if (!current_errnum || *current_errnum != ERR_NOERROR) return false;

  va_list args;
  va_start(args, fmt);
  int needed = vsnprintf(NULL, 0, fmt, args);
  va_end(args);
  if (needed < 0) {
    return compdiag_set_once_diag(current_errnum, diag, new_errnum, diag_phase,
                                  phase, "formatting error");
  }

  char *msg = malloc((size_t)needed + 1);
  if (!msg) {
    return compdiag_set_once_diag(current_errnum, diag, new_errnum, diag_phase,
                                  phase, "out of memory");
  }

  va_start(args, fmt);
  vsnprintf(msg, (size_t)needed + 1, fmt, args);
  va_end(args);

  bool recorded = compdiag_set_once_diag(current_errnum, diag, new_errnum,
                                         diag_phase, phase, msg);
  compiler_diag_free(msg);
  return recorded;
}
void compiler_diag_init(CompilerDiagnostic *d) {
  if (!d) return;
  d->code = 0;
  d->phase = DIAG_PHASE_NONE;
  d->message = NULL;
  d->stable_code = NULL;
  d->source_name = NULL;
  d->excerpt = NULL;
  d->line = d->column = d->span = -1;
  d->has_loc = false;
}

void compiler_diag_reset(CompilerDiagnostic *d) {
  if (!d) return;
  compiler_diag_free(d->message);
  compiler_diag_free(d->stable_code);
  compiler_diag_free(d->source_name);
  compiler_diag_free(d->excerpt);
  compiler_diag_init(d);
}

void compiler_diag_set(CompilerDiagnostic *d, int8_t code, DiagPhase phase,
                       const char *message) {
  if (!d) return;
  compiler_diag_reset(d);
  d->code = code;
  d->phase = phase;
  d->message = compiler_diag_strdup(message ? message : "");
  d->stable_code = compiler_diag_strdup(compiler_diag_stable_code(code, phase));
}

void compiler_diag_set_location(CompilerDiagnostic *d, int line, int column, int span) {
  if (!d) return;
  d->line = line;
  d->column = column;
  d->span = span;
  d->has_loc = line > 0 && column > 0;
}

void compiler_diag_set_span(CompilerDiagnostic *d, CompilerSourceSpan span) {
  if (!d) return;
  compiler_diag_set_location(d, span.line, span.column, span.span);
}

void compiler_diag_set_source_name(CompilerDiagnostic *d, const char *source_name) {
  if (!d) return;
  compiler_diag_free(d->source_name);
  d->source_name = compiler_diag_strdup(source_name ? source_name : "");
}

void compiler_diag_set_excerpt(CompilerDiagnostic *d, const char *excerpt) {
  if (!d) return;
  compiler_diag_free(d->excerpt);
  d->excerpt = compiler_diag_strdup(excerpt ? excerpt : "");
}

const char *compiler_diag_stable_code(int8_t errnum, DiagPhase phase) {
  static char buf[32];
  snprintf(buf, sizeof(buf), "SIN-%s-%04d", compiler_diag_phase_name(phase), errnum);
  return buf;
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
    case DIAG_PHASE_COMPILE:
      return "COMPILE";
    case DIAG_PHASE_IO:
      return "IO";
    default:
      return "NONE";
  }
}
