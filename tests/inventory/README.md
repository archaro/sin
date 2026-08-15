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

Run the positive gate with:

```sh
make inventory-audit
```

Run focused drift checks, which mutate temporary copies only, with:

```sh
make inventory-audit-self-test
```

Catalogs are reviewed source data. Normal test commands never rewrite catalog
files; canonical-definition changes require an intentional catalog edit and a
fresh audit. During the conformance migration, the audit permits only
`conformance.*` descriptor test IDs in addition to the legacy ledger IDs; those
edges remain fully reciprocal and unknown namespaces are rejected.
