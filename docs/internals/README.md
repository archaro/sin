# Internals Guide

This is the conceptual starting point for maintainers. It maps ownership,
boundaries, data flow, and invariants; it is not a second user guide.

## Implementation and Architecture

- [Architecture and module map](architecture.md): dependency direction,
  module ownership, concurrency boundaries, and change rules.
- [Compiler pipeline](compiler.md): source through verified bytecode,
  diagnostics, context cleanup, and embedded compilation.
- [Itemstore operations](itemstore.md): in-memory tree, mutation/cache
  contracts, persistence lifecycle, and structured errors.
- [Event loop and process lifecycle](event-loop.md): `sin` startup,
  task/network ownership, callback confinement, and shutdown.
- [Runtime ownership and API boundaries](runtime.md): VM/frame/value
  ownership and the runtime-facing contracts.
- [Networking](network.md): runtime network connection handling, including
  Telnet

## Wire and Format References

- [Bytecode reference](bytecode.md): bytecode ABI, instruction encoding, and
  verifier-facing wire rules.
- [Itemstore file format](itemstore-format.md): canonical v2 on-disk records,
  limits, and durability format details.

These format pages are the wire references. The conceptual pages above should
explain how those formats are produced and consumed without duplicating their
tables.

## Testing and Evidence

- [Testing internals](testing/README.md): framework architecture, APIs, and
  workflows. Its detailed pages are [framework](testing/framework.md),
  [APIs and ownership](testing/apis.md), and [workflow](testing/workflow.md);
  its linked authoritative test and fixture documents complete the test map.
- [Performance and benchmarking](performance.md): supported benchmark entry
  points and evidence policy.

Use the testing pages for test mechanics and the dated performance page for
measurements. User-visible language and library behavior belong in the Guide
and Reference documentation.
