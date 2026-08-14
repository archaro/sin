# Contract inventories

These CSV catalogs are the checked-in completeness inventory for Sinistra's
language, compiler/bytecode pipeline, module APIs, libcalls, executables, and
legacy behavioral tests. Canonical identifiers are reconciled by `audit.py`
against the parser, AST/IR ABI, opcode schema, libcall list, baseline ledger,
and the built shared archive.

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
fresh audit.
