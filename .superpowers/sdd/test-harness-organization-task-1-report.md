# Task 1: libcall test-harness organization

## Result

Split the oversized libcall test source by handler library, relocated the
`sys.compile` runtime integration test to core, and retained the existing
combined harness registration in `tests/shared/test_compiler.c` unchanged.
No production source changed.

## Structural evidence

- RED: before implementation, each required new core/support path was absent;
  `tests/compiler/test_sys_compile_libcall.c` was present.
- GREEN: `tests/core/test_libcall_{sys,task,net,str,sys_compile}.c` and
  `tests/shared/test_libcall_support.[ch]` exist; the former compiler path is
  absent. `Makefile` references every new source and no longer references the
  removed compiler path.
- Exported test check: the sorted exported test-name list from the original
  `test_libcall_registry.c` is exactly equal to the combined list from the five
  split core libcall files, with no duplicate names. Test bodies were moved as
  contiguous original-source blocks; no assertions were rewritten.

## Files changed

- `tests/core/test_libcall_registry.c`: registry/generic and cross-library
  formatting coverage retained.
- `tests/core/test_libcall_sys.c`, `test_libcall_task.c`,
  `test_libcall_net.c`, `test_libcall_str.c`: handler-specific test bodies.
- `tests/core/test_libcall_sys_compile.c`: verbatim relocation from
  `tests/compiler/test_sys_compile_libcall.c`.
- `tests/shared/test_libcall_support.[ch]`: private shared runtime fixture and
  common invalid-argument/boolean assertions.
- `Makefile`, `docs/architecture.md`.

## Commands and results

- `make -B tests/test-compiler` — PASS; also caught and corrected initial
  boundary-only extraction errors before final validation.
- Structural GREEN/exported-test uniqueness script and `git diff --check` —
  PASS.
- `make test` — PASS, 226/226 tests.
- `make test-warnings` — PASS, 226/226 tests with strict warnings.

## Self-review

- Confirmed the compiler harness registrations were not edited; all moved tests
  remain registered once in their original logical runtime suite.
- Confirmed `test_libcall_float_integer_only_arguments_rejected` and
  `test_libcall_output_formats_values` remain in the registry/generic source.
- Kept network capture helpers in the files that require them; extracted only
  setup/teardown, context construction, invalid-argument, and boolean helpers
  genuinely used by multiple split files.
- Checked staged diff whitespace before commit.

## Concerns

None. The split duplicates the original broad include/prototype preamble in
the new test files to avoid behavior-affecting declaration cleanup; this is
intentional for a mechanical relocation.
