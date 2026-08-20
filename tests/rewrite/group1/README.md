# Group 1 framework adapters

See [`docs/internals/testing/README.md`](../../../docs/internals/testing/README.md)
for the current framework API and test-authoring workflow.

These binaries are the first migrated group in the C17 framework. Each native
test translation unit has its own executable and explicit descriptor array.
Native assertion names in these retained test bodies map to the framework's
assertion implementation through `tests/test_assert.h`; all execution is
provided by the current isolated framework process.
