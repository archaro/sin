# Contract inventories

These CSV catalogs are the checked-in completeness inventory for Sinistra's
language, compiler/bytecode pipeline, module APIs, libcalls, executables, and
legacy behavioral tests. Canonical identifiers are reconciled by `audit.py`
against the parser, AST/IR ABI, opcode schema, libcall list, baseline ledger,
and the built shared archive. `archive_symbols.csv` is the exhaustive
symbol/object/module accountability map; `api.csv` contains grouped,
observable module contracts that those symbols resolve to, rather than one
prose row per private helper.

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
fresh audit. After cutover, the audit discovers checked-in
`rewrite.*` descriptors under `tests/rewrite/` and requires their IDs to match
the catalog rewrite rows exactly; ledger rows marked with verified parallel
coverage must be a subset. Replacement test rows retain the same reciprocal
contract edges as their legacy identities, while focused expansion rows may
have no legacy ledger identity. Unknown test namespaces remain rejected.
