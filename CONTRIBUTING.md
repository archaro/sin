# Contributing to Sinistra

Firstly, thanks for contributing.

Sinistra is released under the MIT license, and contributions must be made under the same license.

## Build and test

Build with `make`.

Run `make test` for the standard test harness. It builds the same test binary as `make teststrict` and runs every registered core, compiler, and runtime test; benchmark-style tests still execute and print timings, but performance budget assertions are disabled.

Run `make teststrict` when you also want the benchmark budget checks enforced. This target runs the same registered tests as `make test`, but invokes the harness with `SIN_STRICT_BENCH=1`, causing the runtime benchmark test to fail if the lookup or dispatch timings exceed their strict thresholds.

The harness prints the selected mode and a final summary with per-suite and total test counts, so the output should identify whether strict benchmark checks were enabled regardless of which make target launched it.

### Strict warning builds

Strict compiler warnings are opt-in for local development and CI gates that want warnings to fail the build. Run:

```bash
make STRICT_WARNINGS=1 test
```

The `test-sanitize` target also enables strict warnings automatically, so sanitizer runs use the same warning gate in addition to address/undefined-behavior sanitizers.

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

