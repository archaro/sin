# Test APIs and Helper Ownership

Include `test_framework.h` for new framework-native code. Retained native test
bodies may include `tests/test_assert.h`, whose assertion names are thin aliases
to the same implementation; there is no fallback harness.

## Assertions

- `TF_ASSERT_TRUE` and `TF_ASSERT_FALSE` check boolean conditions.
- `TF_ASSERT_I64`, `TF_ASSERT_U64`, `TF_ASSERT_FLOAT_BITS`, and `TF_ASSERT_STR`
  compare scalar, bitwise, or string values.
- `TF_ASSERT_BYTES` reports lengths and the first differing byte.
- `TF_ASSERT_DIAGNOSTIC` checks that expected text occurs in a diagnostic.
- `TF_ASSERT_PROCESS` checks exit status and rejects timeout, signal, or
  capture failure.
- `TEST_FAILF`, `ASSERT_TRUE`, `ASSERT_EQ_INT`, and `ASSERT_NOT_NULL` remain
  source-compatible names for retained native bodies. New tests should use
  the `TF_*` names directly.

Assertions report file and line, reset hooks, remove registered fixtures, and
terminate the isolated child. Do not return a failed status manually after an
assertion.

## Processes and Fixtures

Use `tf_process_run(argv, timeout_ms, &result)` for a child process, then call
`tf_process_result_destroy`. It captures independent stdout and stderr and
records `exited`, `exit_status`, `signaled`, `signal_number`, `timed_out`, and
`capture_failed`. Use `tf_fixture_init`, `tf_fixture_file`, and
`tf_fixture_cleanup` for all writable test data; never write generated output
into checked-in fixture directories during a test.

Shared test helpers belong in `tests/test_helpers.h` and
`tests/shared/test_helpers.c` when they are generic across test groups:
temporary paths, process wrappers, fixture reads, bytecode fixture decoding,
and common itemstore setup are appropriate. Put domain-specific helpers beside
their native test source or in that group's adapter. Do not add production
dependencies to the framework.

## Descriptor and Adapter Rules

Keep IDs stable once they are in `tests/inventory/tests.csv`. Add a contract
edge for each observable behavior. Tag tests that mutate process-global hooks,
fixed paths, use libuv/network resources, or run benchmarks as `exclusive`;
network tests also use `network`. The `benchmark` tag serializes benchmark
descriptors; it does not make them opt-in or exclude them from `make test`
runs. Extended measurements are opt-in via `SIN_EXTENDED_BENCH=1`.
Every descriptor needs a timeout appropriate to its slowest child operation.

White-box tests may include a production `.c` file only when private state or
link-time stubs are the behavior under test. Give that source one descriptor
owner and a dedicated Make rule; do not create a second standalone runner.
