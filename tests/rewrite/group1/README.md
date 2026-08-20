# Group 1 framework adapters

These binaries are the first serial migration group from the legacy unified
harness.  Each native test translation unit has its own executable and explicit
descriptor array.  The source translation units remain shared with the legacy
`test-suite`; `SIN_TEST_FRAMEWORK_COMPAT` only adapts their assertion macros to
the isolated framework process.
