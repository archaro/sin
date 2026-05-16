#ifndef PARSE_INPUT_H
#define PARSE_INPUT_H

#include <stddef.h>

typedef struct {
  const char *data;
  size_t len;
  const char *source_name;
} ParseInput;

#endif
