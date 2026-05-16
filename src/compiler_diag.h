#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef enum { DIAG_PHASE_NONE=0, DIAG_PHASE_PARSE, DIAG_PHASE_SEMANT, DIAG_PHASE_LOWER, DIAG_PHASE_IR_VALIDATE, DIAG_PHASE_EMITBC, DIAG_PHASE_IO } DiagPhase;
typedef struct { int8_t code; DiagPhase phase; char *message; int line,column,span; bool has_loc; } CompilerDiagnostic;
void compiler_diag_init(CompilerDiagnostic *d);
void compiler_diag_reset(CompilerDiagnostic *d);
void compiler_diag_set(CompilerDiagnostic *d, int8_t code, DiagPhase phase, const char *message);
const char *compiler_diag_phase_name(DiagPhase p);
