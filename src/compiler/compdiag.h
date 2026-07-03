// Compiler diagnostics API

// Licensed under the MIT License - see LICENSE file for details.

#pragma once

#include <stdbool.h>
#include <stdint.h>

bool compdiag_set_once(int8_t *current_errnum, char **errdetail,
                       int8_t new_errnum, const char *phase,
                       const char *detail);
bool compdiag_setf_once(int8_t *current_errnum, char **errdetail,
                        int8_t new_errnum, const char *phase,
                        const char *fmt, ...);
void compdiag_reset_detail(char **errdetail);

typedef enum {
  DIAG_PHASE_NONE = 0,
  DIAG_PHASE_PARSE,
  DIAG_PHASE_SEMANT,
  DIAG_PHASE_LOWER,
  DIAG_PHASE_IR_VALIDATE,
  DIAG_PHASE_EMITBC,
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

void compiler_diag_init(CompilerDiagnostic *d);
void compiler_diag_reset(CompilerDiagnostic *d);
void compiler_diag_set(CompilerDiagnostic *d, int8_t code, DiagPhase phase, const char *message);
void compiler_diag_set_location(CompilerDiagnostic *d, int line, int column, int span);
void compiler_diag_set_source_name(CompilerDiagnostic *d, const char *source_name);
void compiler_diag_set_excerpt(CompilerDiagnostic *d, const char *excerpt);
const char *compiler_diag_stable_code(int8_t errnum, DiagPhase phase);
const char *compiler_diag_phase_name(DiagPhase p);
