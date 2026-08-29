# Internals guide

This is the conceptual starting point for maintainers. It maps ownership,
boundaries, data flow, and invariants; it is not a second user guide.

## Implementation and architecture

- [Architecture and module map](architecture.md) — dependency direction,
  module ownership, concurrency boundaries, and change rules.
- [Compiler pipeline](compiler.md) — source through verified bytecode,
  diagnostics, context cleanup, and embedded compilation.
- [Itemstore operations](itemstore.md) — in-memory tree, mutation/cache
  contracts, persistence lifecycle, and structured errors.
- [Event loop and process lifecycle](event-loop.md) — `sin` startup,
  task/network ownership, callback confinement, and shutdown.
- [Runtime ownership and API boundaries](runtime.md) — VM/frame/value
  ownership and the runtime-facing contracts.

## Wire and format references

- [Bytecode reference](bytecode.md) — bytecode ABI, instruction encoding, and
  verifier-facing wire rules.
- [Itemstore file format](itemstore-format.md) — canonical v2 on-disk records,
  limits, and durability format details.

These format pages are the wire references. The conceptual pages above should
explain how those formats are produced and consumed without duplicating their
tables.

## Testing and evidence

- [Testing internals](testing/README.md) — framework architecture, APIs, and
  workflows. Its detailed pages are [framework](testing/framework.md),
  [APIs and ownership](testing/apis.md), and [workflow](testing/workflow.md);
  its linked authoritative test and fixture documents complete the test map.
- [Performance and benchmarking](performance.md) — supported benchmark entry
  points and evidence policy.
- [Pre-0.8.0 performance evidence](performance-pre-0.8.0.md) — dated historical
  measurements, retained as evidence rather than a guarantee.

Use the testing pages for test mechanics and the dated performance page for
measurements. User-visible language and library behavior belong in the Guide
and Reference documentation.
