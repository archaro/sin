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

`seed_catalogs.py` is a maintainer utility for regenerating catalogs after an
intentional canonical-definition change. Normal test commands never rewrite
catalog files.
