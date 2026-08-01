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
   - Ownership: cases are declared in `tests/shared/test_pipeline_cases.c` and checked by `tests/compiler/test_pipeline_golden.c` and `tests/compiler/test_pipeline_source_golden.c`.
   - Regeneration: there is no generic Make target; update the paired case/source and expected bytes deliberately, then run `make test`.

3. **Itemstore hex seeds** (`itemstore/*.hex`)
   - Purpose: minimal v1, valid v2 nested-list/reference, and malformed recursive-list inputs for the `sin` object/itemstore fuzzer.
   - Source of truth: commented, hand-authored hex files.
   - Regeneration: remove comment lines and decode with `sed '/^[[:space:]]*#/d' <seed>.hex | xxd -r -p > tests/fuzz/corpus/sin-object/<seed>.itemstore`; `make seed-fuzz-sin-object-corpus` performs this workflow alongside existing source seeds.

4. **Generated object artifacts** (`*.generated.obj` / `*.reference.obj`)
   - Purpose: test-owned temporary outputs used by compiler golden comparisons.
   - Source of truth: `./scomp` output from the paired `.src` source at test time.
   - Ownership: `tests/compiler/test_parser_examples_obj_golden.c` creates both files and removes them after each case; `make clean` also removes them.
   - Manual reproduction examples:
     - `./scomp examples/chat-boot.src tests/fixtures/chat-boot.reference.obj`
     - `./scomp examples/echo-boot.src tests/fixtures/echo-boot.generated.obj`

5. **Interpreter output fixtures** (`*.expected.txt`)
   - Purpose: expected stdout/stderr/exit contracts for runtime behavior.
   - Source of truth: `./sin` output for a freshly-compiled object, normalized to test format.
   - Ownership: `tests/interpreter/test_interpret_semantics_golden.c` and `tests/interpreter/test_interpret_stress.c` compile temporary objects, run them, and compare the checked-in contracts.
   - Update workflow: run `./scomp examples/echo-boot.src tests/fixtures/interpret/echo-boot.generated.obj && ./sin -o tests/fixtures/interpret/echo-boot.generated.obj`, then manually update the fixture's `===stdout===`, `===stderr===`, and `===exit===` sections and run `make test`.
   - The sdiss expectation is checked by `tests/compiler/test_sdiss_fixtures.c`; `make test` creates the temporary input from `basic.hex`, runs `./sdiss --no-header -o tests/fixtures/sdiss/basic.bin`, and removes it. Update `basic.expected.txt` deliberately when that output contract changes; `make clean` removes any leftover temporary `.bin` files.
   - `list-itemref-persist.expected.txt` is a two-run contract: compile its source once, create one temporary itemstore and source root, invoke `sin --loadonly -i <same-store> -s <same-srcroot> -o <object>` twice, normalize paths, and update the expected sections manually. The second run must observe persisted aggregates.

## Naming conventions

- Use lowercase fixture stems (for example: `echo-load`, `chat-boot`, `int_literal`).
- Keep fixture names aligned across classes when they represent the same program.
- Use suffixes by class:
  - source: `.src`
  - bytecode snapshot: `.hex`
  - object snapshot: `.obj`
  - interpreter expectation: `.expected.txt`

## Required comments for case tables

When adding a declared fixture to the policy table in `tests/shared/test_fixture_policy.c`, include metadata that documents:

- `SOT:` source-of-truth artifact (file or builder function)
- `regen:` exact regeneration command (or `manual` for hand-authored source fixtures)

Example:

```c
{"echo_load_expected", "tests/fixtures/interpret/echo-load.expected.txt", "SOT: runtime output contract for echo-load | regen: ./scomp examples/echo-load.src tests/fixtures/interpret/echo-load.generated.obj && ./sin -o tests/fixtures/interpret/echo-load.generated.obj > tests/fixtures/interpret/echo-load.expected.txt"},
```

The enforcement test (`tests/shared/test_fixture_policy.c`) is compiled into `tests/test-suite` by the Makefile and validates declared fixture existence, metadata format, duplicate declarations, and pipeline golden paths. Run `make test` for the standard fixture checks; use `make fuzz-corpora` to seed fuzz inputs from checked-in `.hex` fixtures and `examples/*.src` files.
