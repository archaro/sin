#ifndef TEST_PIPELINE_CASES_H
#define TEST_PIPELINE_CASES_H

#include <stddef.h>

#include "absyn.h"

typedef AS_NODE *(*PipelineAstBuilder)(void);

typedef enum {
  PIPELINE_LAYER_AST = 1u << 0,
  PIPELINE_LAYER_SOURCE = 1u << 1,
} PipelineLayerMask;

typedef struct {
  const char *name;
  PipelineAstBuilder build_ast;
  const char *source;
  const char *fixture_path;
  unsigned layers;
} PipelineGoldenCase;

const PipelineGoldenCase *pipeline_golden_cases(size_t *count);
const PipelineGoldenCase *pipeline_cases_for_layers(unsigned layers, size_t *count);

#endif
