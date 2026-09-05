# Contributing to Sinistra

First, thanks for contributing.

Sinistra is released under the MIT license. Contributions should be bounded,
understandable, and accompanied by a focused commit message.

## Use of AI

Sinistra is developed using AI tools, and AI-assisted PRs are welcome subject
to the following simple rules:
- PRs must be understandable to a human without AI assistance - make sure to
document everything clearly.
- PRs should be limited to fixing or adding one thing, and touch the minimum
amount of code and documentation.  Bulk PRs that have unrelated tentacles
everywhere are likely to be rejected.

## Build and test

Sinistra  is built on Linux and tested on Ubuntu with gcc and clang.  There are
no plans at present to support other platforms, but PRs welcome.  C17 is the
supported standard.

Building will require `make`, `gcc` and/or `clang`, `bison`, `flex`,
`libuv1-dev`, `pkg-config`, and `xxd`.

To install the necessary packages on Ubuntu:
```bash
sudo apt-get update
sudo apt-get install -y build-essential clang bison flex libuv1-dev pkg-config xxd
```

`make` builds the four executables in the default debug profile. Use
`BUILD=release make` for optimized output and `CC=clang make` to select Clang.
All test and fuzz binaries, generated corpora, coverage data, and temporary
logs are written below the active `obj/<build>-<compiler>/` directory.

There are several test targets:
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
batches. Note that `test-sanitize` must run outside ptrace-restricted
environments because LeakSanitizer cannot operate correctly there.

The [testing internals guide](docs/internals/testing/README.md) documents the
framework protocol, assertion/process/fixture APIs, and workflows for adding
native, conformance, inventory, fixture, network, and fuzz coverage.

Before submitting a PR, ensure that `test-full` runs to completion without
errors or warnings.  Minimum coverage targets are enforced, so if you make
changes, you need to ensure that the tests are suitably updated.

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

If you want to use the clangd compilation database, install `bear` and run
`make compiledb`.

Fixture conventions and deliberate regeneration procedures are documented in
`tests/fixtures/README.md` and `tests/fixtures/conformance/README.md`.
Do not rewrite fixtures during normal test execution.

## Mandatory pre-PR requirements

Before submitting a change, ensure the relevant inventory catalog and positive
and negative coverage entries are updated. Public language, bytecode, runtime,
itemstore, and libcall behavior should have a deterministic framework or
conformance witness.
