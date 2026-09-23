# Contract inventories

These CSV catalogs are the checked-in completeness inventory for Sinistra's
language, compiler/bytecode pipeline, module APIs, libcalls, executables, and
behavioral tests. Canonical identifiers are reconciled by `audit.py`
against the parser, AST/IR ABI, opcode schema, and libcall list,
and the built shared archive. `archive_symbols.csv` is the exhaustive
symbol/object/module accountability map; `api.csv` contains grouped,
observable module contracts that those symbols resolve to, rather than one
prose row per private helper.

The archive audit omits narrowly identified compiler/runtime instrumentation
symbols, including Clang's `___asan_globals_registered` and
`__odr_asan_gen_` markers, because they are generated bookkeeping rather than
authored APIs.

Grammar token reconciliation includes both `%token` and Bison precedence
directives. Opcode rows store an exact fingerprint of all ten canonical
`OP(...)` fields, including nested stack metadata, and non-opcode bytecode rows
mark that field `not-applicable`. Libcall rows store and reconcile the exact
handler symbol as well as library/call indices and arity.

The positive catalog gate runs as part of the deterministic framework suite:

```sh
make test
```

Focused drift checks mutate temporary copies only and are included in `make test`.

Catalogs are reviewed source data. Normal test commands never rewrite catalog
files; canonical-definition changes require an intentional catalog edit and a
fresh audit. The audit discovers checked-in `test.*` descriptors under
`tests/adapters/` and requires their IDs to match the catalog test rows exactly.
The `test.<namespace>.<id>` namespace identifies native adapter descriptors;
conformance descriptors retain their separate `conformance.*` namespace and
their catalog rows are checked the same way. `tests.csv` is a reciprocal index
generated from the forward catalog edges, and unknown test namespaces remain
rejected.
