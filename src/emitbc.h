#pragma once

#include <stdint.h>

#include "ir.h"
#include "parser.h"

int8_t emit_bytecode(IR_Unit *ir, uint8_t local_count, uint8_t param_count,
                     OUTPUT_t *out, char **errdetail);
