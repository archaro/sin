# Conformance fixtures

`positive-core.src` and its expected output exercise the canonical language
reference's expression, call, truth/equality, list, and item-reference
contracts, including all short-circuit forms, evaluation order, and dangling
reference re-resolution. Loop control and persistence are intentionally covered by the
existing `interpret/break-log`, `interpret/continue-log`, and
`interpret/list-itemref-persist` fixtures.

The negative fixtures each contain one independent rejection and are consumed
by `test_pipeline_negative_matrix`:

| Reference area | Fixture |
|---|---|
| Unknown character | `negative/parser-unknown-character.src` |
| NUL octal escape | `negative/parser-nul-escape.src` |
| Trailing list comma | `negative/parser-trailing-comma.src` |
| Integer range | `negative/parser-integer-overflow.src` |
| `BREAK` outside loop | `negative/semantic-break-outside-loop.src` |
| Local before definition | `negative/semantic-local-before-definition.src` |

Large generated limits, malformed bytecode/itemstore streams, and allocation
failure paths remain unit- or I/O-test coverage rather than conformance
fixtures.
