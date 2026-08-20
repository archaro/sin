# Adding and running tests

Start by locating the owning module and its contract in the inventory. Run a
focused descriptor while iterating, then run `make test` once on the final tree.

## Test kinds

### Native unit or integration test

1. Add a function to the owning `tests/core`, `tests/compiler`,
   `tests/interpreter`, or `tests/network` translation unit.
2. Add a descriptor to the corresponding `tests/rewrite` adapter (or create a
   new adapter and Make rule). Use `TF_*` assertions, a stable ID, a contract,
   timeout, and tags.
3. Add/update the `tests/inventory/tests.csv` and relevant API/catalog edge.
4. Build and focus it, for example:
   `./obj/debug-gcc/tests/rewrite/test_semant --run rewrite.compiler.case`.

### Conformance case

Add a minimal `.src` and expectations under `tests/fixtures/conformance/`, then
add one pipe-delimited row to `conformance.manifest`. Positive rows declare
compile, disassembly, and runtime phases; negative rows assert a nonzero
compile status and skip later phases. Update the language/libcall contract edge
and run `make test`.

### Inventory or contract coverage

Edit the appropriate checked-in CSV (`language.csv`, `libcalls.csv`,
`contracts.csv`, `api.csv`, `archive_symbols.csv`, or `tests.csv`) together with
the behavior witness. Run `python3 tests/inventory/audit.py --archive
lib/debug-gcc/libsinshared.a` after building, or use the complete `make test`
gate. Never add a catalog row without an executable or native witness.

### Fixture or golden data

Follow [`tests/fixtures/README.md`](../../../tests/fixtures/README.md) and the
conformance README. Keep source-of-truth files and expected output changes in
the same change; regeneration is deliberate and manual. `make test` must not
rewrite checked-in fixtures.

### Framework self-test

Add expected-to-pass behavior to `framework_selftest.c`. A deliberately failing,
crashing, or hanging process belongs in `framework_negative_fixture.c` and must
be invoked by a self-test using `tf_process_run`; do not put it in the normal
runner list. Verify output, timeout, process-group, fixture, and cleanup
contracts as applicable.

### Coverage floors

The normal gate does not rewrite coverage data. A production-source behavior
change should add a witness first. Run `make test-full` when coverage-floor
implications are part of the change; update the applicable reviewed floor only
after manual review with a rationale. The checked-in baseline snapshots and
parity ledger are active audit inputs, not disposable migration files.

### Network and exclusive tests

Use an ephemeral loopback port, bounded process waits, and explicit close/reap
cleanup. Tag descriptors `exclusive,network`; use framework fixtures for logs
and sockets. White-box libuv/Telnet tests belong in the network adapter, while
real localhost chat belongs in the chat adapter. Run focused network checks
with `TEST_JOBS=1` before the full gate.

### Fuzz tests

The three harnesses are `tests/fuzz/fuzz_scomp.c`, `fuzz_sdiss.c`, and
`fuzz_sin_object.c`. Add a small checked-in seed only when it documents a useful
regression. Run `make test-fuzz` with `FUZZ_RUNS`, `FUZZ_TIME`, `FUZZ_SEED`, and
`FUZZ_ARTIFACT_DIR` as needed; generated corpus and logs stay under `obj/`.

## Targets, profiles, and troubleshooting

Supported targets are `make`, `make lib`, `make test`, `make bench`,
`make test-sanitize`, `make test-fuzz`, and `make test-full`. Relevant
variables are `BUILD=debug|release|sanitize|coverage`, `CC=gcc|clang`,
`TEST_JOBS=N`, `TF_VERBOSE=1`, `FUZZ_RUNS`, `FUZZ_TIME`, `FUZZ_SEED`, and
`FUZZ_ARTIFACT_DIR`. Coverage is limited to reviewed GCC 13 or Clang 18 tool
chains and writes reports under `obj/coverage-<compiler>/`; sanitizer builds
also stay under `obj/`, while fuzz outputs use `obj/fuzz-<profile>-<compiler>/`.

For a missing descriptor, run its binary with `--list` and check its adapter's
Make rule. For a duplicate ID or malformed record, run the framework runner
directly and inspect `TF|ERROR|...`. For a timeout, set `TF_VERBOSE=1`, lower
the focused scope, and check that the child process group is reaped. For stale
fixtures or contracts, run the relevant README's audit command and then the
inventory audit. Sanitizer tests require an environment without ptrace
restrictions because LeakSanitizer cannot operate under ptrace.
