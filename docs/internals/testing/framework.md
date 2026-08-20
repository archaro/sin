# Framework architecture

The framework lives in `tests/framework/`. A test translation unit defines a
`TF_TestDescriptor` array and calls `tf_main(argc, argv, tests, count)`. The
descriptor owns a stable ID, a comma-separated tag list, a timeout in
milliseconds, and at least one inventory contract ID.

## Build and execution flow

`mk/tests.mk` compiles each framework or adapter executable into
`obj/<build>-<compiler>/tests/...`; production programs and the shared archive
are in the matching `obj/` and `lib/` directories. The top-level runner is
`obj/.../tests/framework/framework-runner`.

1. The runner invokes every executable with `--list`.
2. Each executable validates descriptors and emits
   `TF|LIST|id|tags|timeout_ms|contracts` records.
3. The runner rejects malformed metadata, duplicate IDs, and discovery errors.
4. For each selected ID it invokes the executable with `--run ID`.
5. `tf_main` executes the descriptor in a fresh process. The runner also puts
   each child in a process group, captures stdout/stderr, enforces the
   descriptor timeout, and kills/reaps the group on timeout or capture error.
6. The child emits a `TF|RESULT|...` record and exits 0 for success, 1 for a
   failed assertion/crash/timeout, or 2 for usage/metadata errors. The runner
   emits `TF|TOTAL|scope|selected|passed|failed` and returns the aggregate
   status.

By default only failing child output is replayed. Set `TF_VERBOSE=1` to replay
successful output too. `TEST_JOBS=N` runs up to N non-serial descriptors in
parallel. `exclusive`, `network`, and `benchmark` tags are serialization
barriers; all tests still get process isolation. IDs and token components use
letters, digits, `_`, `-`, `.`, and `:`; tags and contracts are comma-separated
without empty components.

## Fixtures and cleanup

`tf_fixture_init` creates a private `sin-test-XXXXXX` directory below
`TF_TMP_ROOT` (default `/tmp`). `tf_fixture_file` rejects absolute and `..`
paths, and `tf_fixture_cleanup` removes the tree. The framework registers every
fixture for normal-path cleanup. Assertions run the framework cleanup before
`_exit`; crashes, timeouts, and other forced termination cannot execute
in-process fixture cleanup and may leave a private directory below the
configured temporary root. Process-group cleanup still runs for managed child
processes.
Make exports both `TF_TMP_ROOT` and `SIN_TEST_TMP_ROOT` into the active object
tree; direct invocations may use `/tmp`.

Process capture uses non-blocking pipes and a monotonic deadline. Both the
direct child and descendants are in its process group, so timeout cleanup does
not leave servers or shell descendants behind. Temporary runner capture files
are unlinked immediately and live below the configured temporary root.

## Hooks and configuration

`tf_reset_hooks` restores allocation, itemstore, and I/O hooks between tests.
`tf_alloc_fail_after` and `tf_io_failures` let focused tests exercise failure
paths. `framework_config.c` supplies the default test `CONFIG_t`; a white-box
translation unit may own that symbol and use a dedicated Make rule when it
needs different stubs. Keep such ownership local to that executable.

Negative framework fixtures intentionally crash, hang, or fail assertions and
are kept in `framework-negative-fixture`; they are run by focused self-tests,
not by the ordinary all-pass list. The framework self-test itself must contain
only expected-to-pass descriptors.
