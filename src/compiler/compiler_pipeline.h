// Compiler pipeline API

// Licensed under the MIT License - see LICENSE file for details.

#pragma once

#include <stddef.h>
#include <stdint.h>

#include "compiler/compdiag.h"
#include "compiler/emitbc.h"
#include "compiler/parse_input.h"

int8_t compile_source_to_bytecode(const char *source, size_t len, OUTPUT_t **out, char **errdetail);
int8_t compile_parse_input_to_bytecode(const ParseInput *input, OUTPUT_t **out, char **errdetail);
int8_t compile_source_to_bytecode_with_params(const char *source, size_t len, const char **params, size_t param_count, OUTPUT_t **out, char **errdetail);
int8_t compile_source_to_bytecode_diag(const char *source, size_t len, OUTPUT_t **out, CompilerDiagnostic *out_diag);
int8_t compile_parse_input_to_bytecode_diag(const ParseInput *input, OUTPUT_t **out, CompilerDiagnostic *out_diag);
int8_t compile_parse_input_to_bytecode_diag_with_node_limit(
    const ParseInput *input, size_t ast_node_limit, OUTPUT_t **out,
    CompilerDiagnostic *out_diag);
