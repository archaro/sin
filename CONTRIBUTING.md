# Contributing to Sinistra

Sinistra is released under the MIT license. Contributions should be bounded,
understandable, and accompanied by a focused commit message.

## Build and test

The Unix-like toolchain requires `make`, a C17 compiler, Bison, Flex, the
`libuv` development package, `pkg-config`, and `xxd`. On Debian or Ubuntu:

```bash
sudo apt-get update
sudo apt-get install -y build-essential clang bison flex libuv1-dev pkg-config xxd
```

`make` builds the four executables in the default debug profile. Use
`BUILD=release make` for optimized output and `CC=clang make` to select Clang.
All test and fuzz binaries, generated corpora, coverage data, and temporary
logs are written below the active `obj/<build>-<compiler>/` directory.

The supported test interface is intentionally small:

```bash
make test
make test-sanitize       # ASan + UBSan + leak detection
make test-fuzz           # seeded scomp, sdiss, and sin-object campaigns
make test-full           # debug/release/coverage/sanitizer/fuzz composition
make bench               # opt-in release benchmark measurements
```

`make test` builds with strict warnings and runs the C17 framework, all
migrated deterministic groups, conformance fixtures, inventory audits, CLI,
network, and integration checks. `TEST_JOBS=N` controls non-exclusive runner
batches. `test-sanitize` must run outside ptrace-restricted environments
because LeakSanitizer cannot operate correctly there.

`test-full` is the pre-PR gate for compiler, parser, bytecode, runtime,
itemstore, or fuzz changes. It composes the shared recipes without cleaning or
recursively rebuilding the workspace. Coverage is checked against the recorded
module baseline and inventory catalogs remain enforceable.

## Fuzzing

`test-fuzz` seeds corpora from checked-in examples and fixtures, builds all
three libFuzzer harnesses with `FUZZ_CC` (default `clang`), and runs each with
the configured `FUZZ_RUNS`, `FUZZ_TIME`, and `FUZZ_SEED` values. Crash artifacts
default to the active object directory; set `FUZZ_ARTIFACT_DIR` to archive
failures elsewhere, for example in hosted CI's temporary artifact directory.

The fuzz harness sources are `tests/fuzz/fuzz_scomp.c`,
`tests/fuzz/fuzz_sdiss.c`, and `tests/fuzz/fuzz_sin_object.c`. Checked-in
corpora remain under `tests/fuzz/corpus/`; generated copies are never written
there.

## Editor support and fixtures

Install `bear` and run `make compiledb` if a clangd compilation database is
needed. Fixture conventions and deliberate regeneration procedures are
documented in `tests/fixtures/README.md` and
`tests/fixtures/conformance/README.md`. Do not rewrite fixtures during normal
test execution.

Before submitting a change, ensure the relevant inventory catalog and positive
and negative coverage entries are updated. Public language, bytecode, runtime,
itemstore, and libcall behavior should have a deterministic framework or
conformance witness.
