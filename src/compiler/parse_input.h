// ParserInput type - used in various parts of the compiler

// Licensed under the MIT License - see LICENSE file for details.

#pragma once

#include <stddef.h>

typedef struct {
  const char *data;
  size_t len;
  const char *source_name;
} ParseInput;

