# Sinistra Documentation Index

This directory contains the current human-maintained documentation for Sinistra.
The project is still pre-alpha, so implementation-derived references should be
kept in sync with the source files named in each document.

## Guides and References

- [`architecture.md`](architecture.md): module boundaries, dependency direction,
  and source ownership rules.
- [`concepts.md`](concepts.md): practical language and runtime concepts for
  writing Sinistra code.
- [`libcalls.md`](libcalls.md): library-call reference derived from
  `src/libcall/libcall_list.h`.
- [`tools.md`](tools.md): command-line reference for `sin`, `scomp`, and
  `sdiss`.
- [`bytecode.md`](bytecode.md): bytecode encoding reference derived from
  `src/compiler/ir/opcode_schema.def`.
- [`runtime.md`](runtime.md): runtime ownership, interpreter, and libcall API
  boundaries.
- [`itemstore-format.md`](itemstore-format.md): persisted itemstore wire format.
- [`history.md`](history.md): project background.
- [`documentation-roadmap.md`](documentation-roadmap.md): remaining
  documentation improvements and longer-term reference work.

## Maintenance

When changing syntax, bytecode, libcalls, itemstore persistence, runtime error
contracts, or command-line flags, update the relevant document in the same
change. Module ownership and dependency direction are maintained in
[`architecture.md`](architecture.md). Prefer linking to source-of-truth files such as
`src/libcall/libcall_list.h`, `src/compiler/ir/opcode_schema.def`, and
`src/common/string_limits.h` rather than duplicating implementation details
without context.
