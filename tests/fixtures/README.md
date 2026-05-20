# Fixture policy

This directory and related `examples/` artifacts define golden fixtures used by compiler and interpreter tests.

## Golden fixture classes

1. **Source fixtures** (`*.src`)
   - Purpose: canonical Sin source programs used by golden tests.
   - Source of truth: the `.src` file contents.
   - Regeneration: author/edit directly; no generator.

2. **Bytecode hex fixtures** (`*.hex`)
   - Purpose: expected bytecode snapshots for compile pipeline tests.
   - Source of truth: compiler output from the associated source/program builder case.
   - Regeneration: `make regen-fixtures`.

3. **Generated object artifacts** (`*.generated.obj` / `*.reference.obj`)
   - Purpose: temporary outputs used by tests to compare compiler/runtime behavior.
   - Source of truth: `./scomp` output from the paired `.src` source at test time.
   - Regeneration examples:
     - `./scomp examples/chat-boot.src tests/fixtures/chat-boot.reference.obj`
     - `./scomp examples/echo-boot.src tests/fixtures/interpret/echo-boot.generated.obj`

4. **Interpreter output fixtures** (`*.expected.txt`)
   - Purpose: expected stdout/stderr/exit contracts for runtime behavior.
   - Source of truth: `./sin` output for a freshly-compiled object, normalized to test format.
   - Regeneration example:
     - `./scomp examples/echo-boot.src tests/fixtures/interpret/echo-boot.generated.obj && ./sin -o tests/fixtures/interpret/echo-boot.generated.obj > tests/fixtures/interpret/echo-boot.expected.txt`

## Naming conventions

- Use lowercase kebab-case fixture stems (for example: `echo-load`, `chat-boot`, `int_literal`).
- Keep fixture names aligned across classes when they represent the same program.
- Use suffixes by class:
  - source: `.src`
  - bytecode snapshot: `.hex`
  - object snapshot: `.obj`
  - interpreter expectation: `.expected.txt`

## Required comments for case tables

When adding a fixture entry to any golden case table in `tests/*.c`, include a short inline comment that documents:

- `SOT:` source-of-truth artifact (file or builder function)
- `regen:` exact regeneration command (or `manual` for hand-authored source fixtures)

Example:

```c
{"echo_load", "examples/echo-load.src", "examples/echo-load.obj"}, /* SOT: examples/echo-load.src | regen: ./scomp -i examples/echo-load.src -o examples/echo-load.obj */
```

The enforcement test (`tests/test_fixture_policy.c`) validates fixture-file existence and validates comment metadata format for the fixture policy table.
