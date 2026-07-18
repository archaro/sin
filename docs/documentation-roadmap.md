# Documentation Improvement Roadmap

This roadmap lists remaining documentation work for making Sinistra easier to
learn while leaving space for a formal language reference once the language has
stabilised. The documentation index, tools reference, and source-of-truth
maintenance guidance are already present; the items below are follow-up work,
not descriptions of missing current documentation.

## Near-term tasks

### Split the concepts guide into reader-focused pages

Break `docs/concepts.md` into smaller pages with stable responsibilities:

- items and item hierarchy;
- code items, evaluation, parameters, and local variables;
- expressions, operators, and truthiness;
- control flow;
- comments and source-layout rules;
- runtime tasks and event flow.

This would make the existing material easier to review and create natural homes
for future examples and formal reference links.

### Add an examples guide

Create a guide that walks through the files in `examples/` and explains the
runtime behaviour they demonstrate. Start with the existing echo and chat server
examples, then add small single-purpose examples for language features as they
stabilise.

### Add a diagnostics and troubleshooting page

Document common compiler and runtime failure modes, including how `error` and
`error.msg` are set, where log output appears, how invalid libcall arguments are
reported, and how to reset generated itemstore/source-root state during local
experiments.

## Medium-term tasks

### Establish a language-reference skeleton

Create a placeholder language reference with intentionally marked unstable
sections. Suggested top-level sections are lexical structure, item names,
literals, expressions, statements, code items, parameters, local variables,
libcall syntax, evaluation order, errors, and implementation limits.

### Define stability labels

Use simple stability labels in documentation so readers know whether a page is
introductory, normative, implementation-derived, or provisional. This is
especially useful while the formal language documentation is being prepared but
not yet authoritative.

### Add grammar-oriented documentation

Once the parser rules settle, add a grammar appendix derived from the parser and
lexer. Until then, document only the practical syntax that examples and tests
exercise, and mark gaps explicitly.

### Cross-link implementation-derived references

For implementation-derived documents such as the bytecode and libcall references,
add explicit links to their source-of-truth files and tests. This makes drift
easier to spot during review.

### Expand the test coverage map into a documentation coverage map

Track not only what code is tested, but also what language/runtime behaviours are
documented. This can highlight features that are implemented and tested but not
yet explained to users.

## Long-term tasks

### Promote the formal language reference to canonical status

When the language stabilises, promote the language reference from provisional to
canonical and make other guides link back to it for normative definitions.

### Generate reference tables where possible

Consider generating or checking selected reference tables from implementation
metadata, especially opcode and libcall inventories. Generated checks can reduce
manual drift without requiring every document to be generated.

### Add versioned documentation notes

If Sinistra starts preserving compatibility across releases, add versioned notes
for syntax, bytecode, library calls, and itemstore/runtime behaviour.

## Suggested next issues

1. Split `docs/concepts.md` into focused concept pages without changing content
   semantics.
2. Add a troubleshooting page for diagnostics, logs, and generated state.
3. Add a provisional language-reference skeleton with stability notes.
