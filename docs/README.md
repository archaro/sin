# Sinistra Documentation Index

This directory contains the current human-maintained documentation for Sinistra.
The 0.7.3 language reference is canonical for documented source behaviour;
compatibility is not frozen until 0.8.0. Implementation-derived references
remain linked to their source files.

## Guides and References

- [`architecture.md`](architecture.md): module boundaries, dependency direction,
  and source ownership rules.
- [`concepts.md`](concepts.md): practical language and runtime concepts for
  writing Sinistra code.
- [`language-reference.md`](language-reference.md): canonical 0.7.3 syntax,
  grammar, precedence, and evaluation-order reference.
- [`libcalls.md`](libcalls.md): library-call reference derived from
  `src/libcall/libcall_list.h`.
- [`tools.md`](tools.md): command-line reference for `sin`, `scomp`, `sdiss`,
  and `sconv`.
- [`troubleshooting.md`](troubleshooting.md): compiler diagnostics, runtime
  error items, logging, and safe local-state recovery.
- [`bytecode.md`](bytecode.md): bytecode encoding reference derived from
  `src/bytecode/opcode_schema.def`.
- [`runtime.md`](runtime.md): runtime ownership, interpreter, and libcall API
  boundaries.
- [`lists.md`](lists.md): planned list and item-reference contracts (in development).
- [`itemstore-format.md`](itemstore-format.md): persisted itemstore wire format.
- [`history.md`](history.md): project background.
- [`documentation-roadmap.md`](documentation-roadmap.md): remaining
  documentation improvements and longer-term reference work.

## Maintenance

When changing syntax, bytecode, libcalls, itemstore persistence, runtime error
contracts, or command-line flags, update the relevant document in the same
change. Module ownership and dependency direction are maintained in
[`architecture.md`](architecture.md). Prefer linking to source-of-truth files such as
`src/libcall/libcall_list.h`, `src/bytecode/opcode_schema.def`, and
`src/common/string_limits.h` rather than duplicating implementation details
without context.
