#pragma once

#include <stdbool.h>
#include <stdint.h>

bool sin_parse_binary64(const char *text, double *out, char **errdetail);
bool sin_parse_binary64_bits(const char *text, uint64_t *out_bits, char **errdetail);
