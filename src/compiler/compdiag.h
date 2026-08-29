// Compiler diagnostics API

// Licensed under the MIT License - see LICENSE file for details.

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "compiler/source_span.h"

typedef enum {
  DIAG_PHASE_NONE = 0,
  DIAG_PHASE_PARSE,
  DIAG_PHASE_SEMANT,
  DIAG_PHASE_LOWER,
  DIAG_PHASE_IR_VALIDATE,
  DIAG_PHASE_EMITBC,
  DIAG_PHASE_COMPILE,
  DIAG_PHASE_IO
} DiagPhase;

typedef struct {
  int8_t code;
  DiagPhase phase;
  char *message;
  char *stable_code;
  char *source_name;
  char *excerpt;
  int line, column, span;
  bool has_loc;
} CompilerDiagnostic;

bool compdiag_set_once_diag(int8_t *current_errnum, CompilerDiagnostic *diag,
                            int8_t new_errnum, DiagPhase diag_phase,
                            const char *phase, const char *message);
#if defined(__GNUC__) || defined(__clang__)
#define SIN_COMPD_PRINTF_FORMAT(format_index, argument_index) \
  __attribute__((format(printf, format_index, argument_index)))
#else
#define SIN_COMPD_PRINTF_FORMAT(format_index, argument_index)
#endif
bool compdiag_setf_once_diag(int8_t *current_errnum,
                             CompilerDiagnostic *diag, int8_t new_errnum,
                             DiagPhase diag_phase, const char *phase,
                             const char *fmt, ...)
    SIN_COMPD_PRINTF_FORMAT(6, 7);
#undef SIN_COMPD_PRINTF_FORMAT

void compiler_diag_init(CompilerDiagnostic *d);
void compiler_diag_reset(CompilerDiagnostic *d);
void compiler_diag_set(CompilerDiagnostic *d, int8_t code, DiagPhase phase, const char *message);
void compiler_diag_set_location(CompilerDiagnostic *d, int line, int column, int span);
void compiler_diag_set_span(CompilerDiagnostic *d, CompilerSourceSpan span);
void compiler_diag_set_source_name(CompilerDiagnostic *d, const char *source_name);
void compiler_diag_set_excerpt(CompilerDiagnostic *d, const char *excerpt);
const char *compiler_diag_stable_code(int8_t errnum, DiagPhase phase);
const char *compiler_diag_phase_name(DiagPhase p);
