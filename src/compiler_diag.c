#include "compiler_diag.h"
#include <stdlib.h>
#include <string.h>
void compiler_diag_init(CompilerDiagnostic *d){ if(!d) return; d->code=0; d->phase=DIAG_PHASE_NONE; d->message=NULL; d->line=d->column=d->span=-1; d->has_loc=false; }
void compiler_diag_reset(CompilerDiagnostic *d){ if(!d) return; free(d->message); compiler_diag_init(d);} 
void compiler_diag_set(CompilerDiagnostic *d, int8_t code, DiagPhase phase, const char *message){ if(!d) return; compiler_diag_reset(d); d->code=code; d->phase=phase; d->message=strdup(message?message:""); }
const char *compiler_diag_phase_name(DiagPhase p){ switch(p){case DIAG_PHASE_PARSE:return "PARSE";case DIAG_PHASE_SEMANT:return "SEMANT";case DIAG_PHASE_LOWER:return "LOWER";case DIAG_PHASE_IR_VALIDATE:return "IR_VALIDATE";case DIAG_PHASE_EMITBC:return "EMITBC";case DIAG_PHASE_IO:return "IO";default:return "NONE";} }
