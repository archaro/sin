#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

typedef enum {
  BC_CONVERT_SUCCESS = 0,
  BC_CONVERT_INVALID,
  BC_CONVERT_TRUNCATED,
  BC_CONVERT_UNSUPPORTED_VERSION,
  BC_CONVERT_ALLOCATION_FAILURE
} BC_ConvertStatus;

typedef struct {
  BC_ConvertStatus status;
  uint8_t *data;
  uint32_t length;
  /* Nonzero only when a conversion-work charge was rejected. */
  size_t budget_request;
} BC_ConvertResult;

/* Converts legacy 0.7.1 or v1 bytecode to owned v1 bytes. On success, data is
 * caller-owned and must be released with bc_convert_result_free(). On failure,
 * data is NULL and length is zero; ALLOCATION_FAILURE denotes allocator or
 * size-growth failure. Legacy M tokens use the immutable positional ABI from
 * commit cd9dd1b (Sinistra 0.7.1), independent of the current registry. */
BC_ConvertResult bc_convert_latest(const uint8_t *input, uint32_t length);
BC_ConvertResult bc_convert_latest_with_limits(const uint8_t *input,
                                               uint32_t length,
                                               uint32_t max_output_length,
                                               size_t *work_used,
                                               size_t work_limit,
                                               bool *budget_exhausted);
void bc_convert_result_free(BC_ConvertResult *result);

/* Deterministic relocation-lookup instrumentation used by scaling tests. */
void bc_convert_test_reset_lookup_probes(void);
size_t bc_convert_test_lookup_probes(void);
