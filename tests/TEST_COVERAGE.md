# Test coverage and completeness

The C17 framework is authoritative. `make test` discovers the framework
self-tests, conformance executable, and every migrated adapter under the
active `obj/<build>-<compiler>/` path. It also runs the executable CLI,
localhost network/chat, inventory, fixture, and integration contracts.

Completeness is enforced by checked-in catalogs rather than a brittle claim of
100% branch coverage:

- `tests/inventory/language.csv` maps every language token, production,
  operator, literal, statement, expression, and item-syntax construct to
  positive and negative source witnesses.
- `tests/inventory/libcalls.csv`, `contracts.csv`, `api.csv`, and
  `archive_symbols.csv` account for registered libcalls, module contracts,
  maintained library symbols, and ownership/provenance.
- `tests/inventory/tests.csv` reconciles framework descriptors and their
  contract edges.
- `tests/fixtures/conformance/conformance.manifest` provides executable
  source-to-bytecode-to-runtime witnesses and explicit exclusions for
  nondeterministic facilities.
- `tests/baseline/legacy_test_ledger.csv` records the migrated baseline and
  parity mapping; no entry is silently dropped.

The inventory auditor fails closed on missing, duplicate, stale, or unmapped
rows. The framework self-tests cover descriptor validation, crash and timeout
isolation, diagnostics, filtering, aggregation, and nonzero failure exits.
Coverage reports are generated under the active object directory by
`make test-full` and checked against `tests/baseline/coverage_floors.csv` and
the reviewed Clang floor file. Reports are generated artifacts, not checked-in
source data.

## Test groups

The adapters under `tests/rewrite/` preserve native test bodies while giving
each group an isolated framework executable:

- Group 1: AST, parser, CLI I/O, libcalls, fixture policy, values, and memory.
- Group 2: semantic analysis, IR/lowering, compiler diagnostics, and golden
  pipelines.
- Group 3: bytecode ABI, wire formats, conversion, emission, verification, and
  disassembly.
- Group 4: stack, lists, interpreter semantics, stress cleanup, and opt-in
  benchmark behavior.
- Group 5: itemstore cache, persistence, durability, and `sin` policy.
- Group 6: task and library-call contracts.
- Group 7: network/Telnet stubs and real localhost chat integration.
- Group 8: end-to-end CLI contract matrices for all four executables.

Conformance fixtures exercise positive and negative language behavior through
the real `scomp`, `sdiss`, and `sin` programs. Fuzz harnesses cover compiler
source input, disassembler object input, and runtime/itemstore loading. Their
generated corpora and campaign logs stay under `obj/`; checked-in fixture
directories are read-only during tests.

## Validation commands

```sh
make test
BUILD=release make test
make test-sanitize
make test-fuzz
make test-full
```

The sanitizer target enables ASan, UBSan, and leak detection and must run
outside ptrace-restricted environments. `make bench` is separate and never
changes ordinary test semantics or coverage accounting.
