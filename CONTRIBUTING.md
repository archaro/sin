# Contributing to Sinistra

Firstly, thanks for contributing.

Sinistra is released under the MIT license, and contributions must be made under the same license.

## Build and test

Build with `make`.

Run `make test` for the standard test harness. It builds the same test binary as `make test-strict` and runs every registered core, compiler, and runtime test; benchmark-style tests still execute and print timings, but performance budget assertions are disabled.

Run `make test-strict` when you also want the benchmark budget checks enforced. This target runs the same registered tests as `make test`, but invokes the harness with `SIN_STRICT_BENCH=1`, causing the runtime benchmark test to fail if the lookup or dispatch timings exceed their strict thresholds.

The harness prints the selected mode and a final summary with per-suite and total test counts, so the output should identify whether strict benchmark checks were enabled regardless of which make target launched it.

### Strict warning builds

Strict compiler warnings are opt-in for local development and CI gates that want warnings to fail the build. Run:

```bash
make test-warnings
```

The `test-asan` and `test-lsan` targets also enable strict warnings automatically. Both rebuild with address/undefined-behavior sanitizers; `test-asan` disables leak detection for ptrace-constrained environments, while `test-lsan` enables it.


## Sanitizer and fuzzing gates

Run the same P0 gates locally before opening a PR that touches compiler, runtime, parser, bytecode, or fuzz harness code. The combined CI-style entry point is:

```bash
./ci/gate_sanitizers_fuzz.sh
```

The script exports the same sanitizer defaults used by CI and then runs the Makefile gates in this order: `make test-warnings`, `make test-asan`, `make test-lsan`, `make fuzz-build`, and `make fuzz-smoke-run` with `FUZZ_SEED=1`. Use `make fuzz-smoke` when you want the Makefile to build and run the fuzz smoke gate in one step.

### Local gate commands

Use these commands to run each gate directly:

```bash
make test
make test-warnings
make test-asan
make test-lsan
make fuzz-smoke
make fuzz-scomp
make fuzz-sdiss
make fuzz-sin-object
```

- `make test` runs the normal test harness.
- `make test-warnings` performs a clean rebuild and treats strict-warning regressions as build failures.
- `make test-asan` rebuilds and runs tests with address/undefined-behavior sanitizers and strict warnings, with leak detection disabled.
- `make test-lsan` performs the same rebuild with leak detection enabled and must run outside ptrace-constrained environments.
- `make fuzz-smoke` builds the fuzz harnesses and runs seeded smoke coverage for the `scomp`, `sdiss`, and `sin` object input paths.
- `make fuzz-scomp`, `make fuzz-sdiss`, and `make fuzz-sin-object` build individual libFuzzer targets and print the command to run each target against its corpus.

You can tune local or CI fuzz runs with environment variables without editing the script or Makefile:

```bash
FUZZ_RUNS=5000 FUZZ_TIME=60 CC=gcc FUZZ_CC=clang ./ci/gate_sanitizers_fuzz.sh
FUZZ_RUNS=5000 FUZZ_TIME=60 make fuzz-smoke
```

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

### P0 completion criteria

The P0 sanitizer and fuzzing gates are complete when all of the following are true:

- CI fails on sanitizer findings.
- CI fails on strict-warning regressions.
- CI runs seeded fuzz smoke targets for `scomp`, `sdiss`, and `sin` input paths.
- Fuzz corpora include representative existing fixtures.
- Developers can run the same gates locally with the documented commands above.

## Core language gate

Run this gate when changing any code in the compiler or interpreter paths:

```bash
./ci/gate_ir_absyn_emitbc.sh
```

## Fixture policy

Fixture conventions and regeneration guidance are documented in:

- `tests/fixtures/README.md`

## PR checklist

- Any new or changed language components must include validator, emitter mapping, opcode-spec update, and positive/negative tests.
- Any bytecode format change updates should update `docs/bytecode.md` and related encoding/header tests.
