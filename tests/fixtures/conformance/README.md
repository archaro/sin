# Conformance fixtures

`positive-core.src` and its expected output exercise the canonical language
reference's expression, call, truth/equality, list, item-reference, and
`FOREACH` contracts, including all short-circuit forms, evaluation order,
dangling reference re-resolution, nil elements, nested iteration, snapshot
iteration under source rebinding, and the nil iterator result after zero
iterations. The executable `positive-structures.src` witness covers loop
control, increment/decrement, branch alternatives, relative layers, and item
syntax. The executable `positive-mixed-case.src` witness locks down ASCII
case-insensitivity for compound control-flow keywords, local bindings, and item
layer names (including lowercase canonical item references). Persistence is
covered by the existing `interpret/list-itemref-persist` fixture.

The negative fixtures each contain one independent rejection and are consumed
by `test_pipeline_negative_matrix`:

| Reference area | Fixture |
|---|---|
| Unknown character | `negative/parser-unknown-character.src` |
| NUL octal escape | `negative/parser-nul-escape.src` |
| Trailing list comma | `negative/parser-trailing-comma.src` |
| Integer range | `negative/parser-integer-overflow.src` |
| Non-local `FOREACH` iterator | `negative/parser-foreach-nonlocal-iterator.src` |
| `BREAK` outside loop | `negative/semantic-break-outside-loop.src` |
| Local before definition | `negative/semantic-local-before-definition.src` |
| Control and syntax bundle | `negative-language-bundle.src` |
| Unknown libcall | `negative-unknown-libcall.src` |

Large generated limits, malformed bytecode/itemstore streams, and allocation
failure paths remain unit- or I/O-test coverage rather than conformance
fixtures.

## Declarative conformance manifest

`conformance.manifest` is the checked-in case catalog consumed by
`tests/conformance/test_conformance.c`; case contract tags must exist in
`tests/inventory/contracts.csv`. Its first line is the schema marker
`SINISTRA-CONFORMANCE|1|case-v1|coverage-v1|exclude-v1`. A `case` row has the
following pipe-delimited fields:

```text
case|id|positive-or-negative|source|contract-tags|compile-status|compile-stdout|compile-stderr|compile-match|disassembly-status|disassembly-stdout|disassembly-stderr|disassembly-match|runtime-mode|runtime-runs|runtime-status|runtime-expectation|runtime-match|notes
```

Statuses are decimal exit statuses or `skip`; compile/runtime matches are
`exact`, `contains`, or `skip`; disassembly uses the ordered-signature match
mode `signature` (or `skip`); and runtime mode is `loadonly` or `skip`. `-` means an
empty or inapplicable checked-in expectation. Runtime expectation files use
`===stdout===`, optional `===stdout_runN===`, `===stderr===`, and `===exit===`
blocks. Positive cases must declare compile, disassembly, and strict runtime
phases. Negative cases must declare a nonzero compiler status and skip all
later phases. The validator rejects duplicate IDs or source paths, missing
expectation files, unsafe paths, malformed rows, and unreferenced `.src` or
`.txt` fixture drift.

Disassembly expectation files contain an ordered, case-specific list of opcode
signatures. The `signature` mode checks those signatures in order and requires
the emitted summary, while allowing operands and offsets to vary with the
fixture's bytecode details.

Runtime `contains` expectations intentionally store only the application
payload. Sinistra's lifecycle messages and `sys.log` records share stdout, and
`sys.log` does not append newlines; the validator still requires an empty
checked-in stderr block to match exactly.

The libcall conformance witnesses are deliberately split by `sys`, `list`,
`str`, and `task`. This keeps every invoked, load-only-compatible result
assertion in a small, independently compiled frame. Each vector contains one
observable assertion for every listed invocation. `sys.compile` is explicitly
excluded: source invoking its nested compilation currently fails mandatory
strict verifier stack-flow validation, while the native runtime and interpreter
source-integration tests remain its coverage.

`coverage` rows have the form
`coverage|contract|positive-case|positive-source|negative-case|negative-source|runtime`.
Executable language kinds (tokens, productions, operators, literals,
statements, expressions, and item syntax) require a positive source-to-runtime
witness, a negative source witness, and `runtime=yes`. Diagnostic-only entries,
unknown-character token rejection, and semantic rules represented only by a
rejection case use that negative case in both witness columns with
`runtime=no`; this records what the source actually exercises rather than
inventing a successful diagnostic case. Libcalls require an executable positive
witness and use `native|-` when a negative invocation is covered by native tests.
For executable language contracts, a negative witness means a rejected source
that also contains or observes the construct; it does not claim that the
construct itself is invalid. The bundle's final rejection is therefore a
targeted context/diagnostic witness, while dedicated parser fixtures cover
malformed forms.
`exclude` rows are required for deterministic source-invocable facilities that
are intentionally not run (network transport, shutdown, persistence side
effects, clocks, or task scheduling). The validator reads both inventories and
fails closed when a newly added entry has neither a witness nor an explicit
exclusion.

Run the read-only suite with `make test`. Tests compile each positive source with `scomp`, require
`sdiss` to accept the emitted object, then invoke `sin --loadonly
--strict-validation` in an isolated temporary
fixture. Repeated runs share the fixture itemstore when `runtime-runs` is
greater than one. Negative cases stop after compiler diagnostics. No normal
test target rewrites fixtures or goldens.

To deliberately regenerate an expectation, compile a named source into a
temporary object, run `sdiss` or `sin` with the same options, normalize only
the documented temporary paths, and edit the checked-in expectation by hand.
For example, the existing persistence contract can be regenerated with two
`sin --loadonly` invocations against one temporary `items.dat` and `srcroot`.
