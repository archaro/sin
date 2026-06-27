#include <stddef.h>
#include "test_assert.h"
#include "test_helpers.h"
#include "shared/test_pipeline_cases.h"

static void run_source_case(const PipelineGoldenCase *tc) {
  ASSERT_NOT_NULL(tc->source);
  compile_source_and_assert_hex(tc->source, tc->fixture_path);
}

void test_pipeline_source_golden(void) {
  size_t case_count = 0;
  const PipelineGoldenCase *cases = pipeline_cases_for_layers(PIPELINE_LAYER_SOURCE, &case_count);

  for (size_t i = 0; i < case_count; i++) {
    run_source_case(&cases[i]);
  }
}
