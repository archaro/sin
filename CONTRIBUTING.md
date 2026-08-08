# Contributing to Sinistra

Firstly, thanks for contributing.

Sinistra is released under the MIT license, and contributions must be made under the same license.

## Build and test

The development toolchain requires `make`, a C17 compiler, Bison, Flex, the
`libuv` development package, `pkg-config`, and `xxd`. `pkg-config` is required
to discover the `libuv` module; `xxd` is required to seed the disassembler fuzz
corpus from checked-in fixture data. On Debian or Ubuntu, install the
toolchain with:

```bash
sudo apt-get update
sudo apt-get install -y build-essential clang bison flex libuv1-dev pkg-config xxd
```

Build with `make`.

### Editor code intelligence (optional)

The project builds through make, so clangd needs a compilation database. Install
`bear` and run:

```bash
make compiledb
```

This cleans, rebuilds, and writes `compile_commands.json` (gitignored). The
clean is deliberate: `bear` records only the compilations it observes, so
regenerating over an up-to-date tree yields a partial database that still looks
valid. The target also builds the test binaries, because `all` alone omits
`-Itests` and leaves everything under `tests/` unable to resolve
`test_assert.h`.

Rerun it whenever `CPPFLAGS` or `BASE_CFLAGS` change.

Run `make test` for the standard test harness. It builds the same test binary as `make test-strict` and runs every registered core, compiler, and runtime test; benchmark-style tests still execute, but performance budget assertions are disabled.

Run `make test-strict` when you also want the benchmark budget checks enforced. This target runs the same registered tests as `make test`, but invokes the harness with `SIN_STRICT_BENCH=1`, causing the runtime benchmark test to fail if the lookup or dispatch timings exceed their strict thresholds.

Test targets suppress successful compilation commands, per-test records, and
normal program chatter. Each runner ends with dynamic ran/passed/failed/skipped
totals and `status=SUCCESS`; an aggregate target such as `make test` prints one
combined total. Failures replay the captured build or test diagnostics and end
with `status=FAILURE`. Set `SIN_BENCH_REPORT=1` to replay explicitly requested
benchmark measurements before the concise totals.

### Strict warning builds

Strict compiler warnings are opt-in for local development and CI gates that want warnings to fail the build. Run:

```bash
make test-warnings
```

The `test-asan` and `test-lsan` targets also enable strict warnings automatically. Both rebuild with address/undefined-behavior sanitizers; `test-asan` disables leak detection for ptrace-constrained environments, while `test-lsan` enables it.


## Sanitizer and fuzzing gates

Run the combined local gate before opening a PR that touches compiler, runtime,
parser, bytecode, itemstore loading, or fuzz harness code:

```bash
./ci/gate_sanitizers_fuzz.sh
```

The script runs these checks in order:

- `make test-warnings`
- `make test-release`, which uses `BUILD=release` with strict warnings.
- `make test-lsan`, which uses the ASan/UBSan build with leak checking enabled.
- `FUZZ_SEED=1 make fuzz-smoke`

The combined gate does not run `test-asan`; that target is a local fallback for
environments where leak checking cannot run, such as a ptrace-constrained
environment.

### Local gate commands

Use these commands to run each gate directly:

```bash
make test
make test-warnings
make test-release
make test-asan
make test-lsan
FUZZ_SEED=1 make fuzz-smoke
make fuzz-scomp
make fuzz-sdiss
make fuzz-sin-object
```

- `make test` runs the normal test harness.
- `make test-warnings` performs a clean rebuild and treats strict-warning regressions as build failures.
- `make test-release` performs the standard test and network checks with release compiler flags and strict warnings.
- `make test-asan` rebuilds and runs tests with address/undefined-behavior sanitizers and strict warnings, with leak detection disabled.
- `make test-lsan` performs the same rebuild with leak detection enabled and must run outside ptrace-constrained environments. This is the sanitizer check used by the combined local gate and its hosted counterpart.
- `make fuzz-smoke` builds the fuzz harnesses and runs seeded smoke coverage for the `scomp`, `sdiss`, and `sin` object input paths.
- `make fuzz-scomp`, `make fuzz-sdiss`, and `make fuzz-sin-object` build individual libFuzzer targets and print the command to run each target against its corpus.

`test-asan` remains useful as a local fallback when LSan cannot run. It is not
part of `gate_sanitizers_fuzz.sh` and is not duplicated as a hosted CI job.

You can tune local or CI fuzz runs with environment variables without editing the script or Makefile:

```bash
FUZZ_RUNS=5000 FUZZ_TIME=60 CC=gcc FUZZ_CC=clang ./ci/gate_sanitizers_fuzz.sh
FUZZ_RUNS=5000 FUZZ_TIME=60 FUZZ_SEED=1 make fuzz-smoke
```

Set `FUZZ_ARTIFACT_DIR` to preserve libFuzzer crash artifacts instead of
leaving them in the gate's temporary working directory. The directory is
created when needed and receives artifacts from each fuzz harness, including
inputs produced by failed runs. For example:

```bash
FUZZ_ARTIFACT_DIR="$PWD/tests/fuzz/artifacts" FUZZ_SEED=1 make fuzz-smoke
FUZZ_ARTIFACT_DIR="$PWD/tests/fuzz/artifacts" ./ci/gate_sanitizers_fuzz.sh
```

Hosted CI sets `FUZZ_ARTIFACT_DIR` to
`$RUNNER_TEMP/sin-libfuzzer-artifacts` and uploads it when the fuzz job fails
(`if: failure()`). This keeps crash, timeout, out-of-memory, and leak inputs
available after a failed fuzz run. Local artifacts under
`tests/fuzz/artifacts/` are ignored by Git and must not be committed.

### Hosted CI jobs

Hosted CI runs these jobs in parallel after installing the prerequisites above:

```bash
make CC=gcc test-warnings
make CC=clang test-warnings
make test-release
make test-lsan
FUZZ_SEED=1 FUZZ_ARTIFACT_DIR="$RUNNER_TEMP/sin-libfuzzer-artifacts" make fuzz-smoke
```

The two warning jobs provide GCC and Clang coverage; the release, leak-
sanitizer, and fuzz commands each run once. Hosted CI does not invoke
`test-asan` or the combined local script in addition to these jobs.

### Fuzz corpora

Seed corpora live under `tests/fuzz/corpus/`:

- `tests/fuzz/corpus/scomp/` for compiler source-input fuzzing.
- `tests/fuzz/corpus/sdiss/` for disassembler object-input fuzzing.
- `tests/fuzz/corpus/sin-object/` for `sin` object/itemstore input fuzzing.

The `sdiss` and `sin-object` corpora may be seeded from representative existing fixtures during the build: `make fuzz-sdiss`, `make fuzz-sin-object`, and `make fuzz-build` refresh generated corpus inputs before building or running the relevant harnesses.

### Reproducing fuzz failures

When libFuzzer reports a failure, save the crashing input path from its output, rebuild the matching harness, and run that harness with the saved input as the only argument. For example:

```bash
make fuzz-scomp
tests/fuzz/fuzz_scomp path/to/crash-input

make fuzz-sdiss
tests/fuzz/fuzz_sdiss path/to/crash-input

make fuzz-sin-object
tests/fuzz/fuzz_sin_object path/to/crash-input
```

If the crash was found with non-default sanitizer, compiler, or fuzzing settings, rerun with the same environment variables, such as `FUZZ_CC`, `FUZZ_CFLAGS`, `FUZZ_LDFLAGS`, `ASAN_OPTIONS`, or `UBSAN_OPTIONS`. Keep the crashing input as a regression corpus entry whenever it is small, deterministic, and safe to commit.

### Gate completion criteria

The sanitizer and fuzzing gates are complete when all of the following are true:

- CI fails on sanitizer findings.
- CI fails on strict-warning regressions.
- CI runs seeded fuzz smoke targets for `scomp`, `sdiss`, and `sin` input paths.
- Fuzz corpora include representative existing fixtures.
- Developers can run the combined local gate with the documented command above.

## Fixture policy

Fixture conventions and regeneration guidance are documented in:

- `tests/fixtures/README.md`

## PR checklist

- Any new or changed language components must include validator, emitter mapping, opcode-spec update, and positive/negative tests.
- Any bytecode format change updates should update `docs/bytecode.md` and related encoding/header tests.
