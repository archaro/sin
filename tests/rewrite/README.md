# Compiler rewrite adapters

The `group2_adapter_*.c` files own explicit framework descriptor arrays for
the compiler front-end and lowering migration. Each adapter is linked with
the corresponding legacy native test translation unit, so the same test body
continues to run in both the legacy suite and the isolated framework process.
The Group 1 adapters under `group1/` remain the owners of their overlapping
AST/parser/float-format descriptors.
