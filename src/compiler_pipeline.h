#ifndef COMPILER_PIPELINE_H
#define COMPILER_PIPELINE_H

#include <stddef.h>
#include <stdint.h>

#include "emitbc.h"

int8_t compile_source_to_bytecode(const char *source, size_t len, OUTPUT_t **out, char **errdetail);

#endif
