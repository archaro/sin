// Non-owning source provenance shared by compiler stages.
//
// Licensed under the MIT License - see LICENSE file for details.

#pragma once

#include <stdbool.h>

/* A zero-initialized span is invalid.  Line and column are one-based when
 * valid; span is the number of source columns covered on the first line. */
typedef struct {
  int line;
  int column;
  int span;
} CompilerSourceSpan;

#define COMPILER_SOURCE_SPAN_INVALID ((CompilerSourceSpan){0, 0, 0})

static inline bool compiler_source_span_valid(CompilerSourceSpan span) {
  return span.line > 0 && span.column > 0 && span.span > 0;
}
