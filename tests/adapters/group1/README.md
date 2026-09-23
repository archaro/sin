# Group 1 framework adapters

See [`docs/internals/testing/README.md`](../../../docs/internals/testing/README.md)
for the current framework API and test-authoring workflow.

Group 1 binaries each own one native test translation unit and its executable
descriptor array.
Native assertion names in these retained test bodies map to the framework's
assertion implementation through `tests/test_assert.h`; all execution is
provided by the current isolated framework process.
