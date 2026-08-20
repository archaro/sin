# Compiler rewrite adapters

The `group2_adapter_*.c` files own explicit framework descriptor arrays for
the compiler front-end and lowering migration. Each adapter is linked with
the corresponding native test translation unit retained at cutover, so the
test body runs in an isolated framework process without the removed legacy
harness.
The Group 1 adapters under `group1/` remain the owners of their overlapping
AST/parser/float-format descriptors.

The `group3_adapter_*.c` files cover bytecode ABI/schema, wire encoding,
conversion, emission, verification, and `sdiss`, with one descriptor-owning
executable per native bytecode test translation unit. Group 3 binaries are
aggregated by `make test`.

The runtime migration adapters are `group4_adapter_*.c`. The value adapter
under `group1/` is intentionally extended for the overlapping value/diagnostic
translation unit; stack/frame, list, interpreter semantics, stress, and
benchmark each have their own descriptor-owning executable. Runtime binaries
use the active build variant's `obj/` directory. Stress cleanup is explicit on
the success path because framework isolation uses `_exit`; the benchmark
descriptor is tagged `benchmark,exclusive` and remains opt-in.
