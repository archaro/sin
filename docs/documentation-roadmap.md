# Documentation Improvement Roadmap

This roadmap lists remaining documentation work for making Sinistra easier to
learn. The canonical 0.7.3 language reference is now present; compatibility is
explicitly not frozen until 0.8.0. The documentation index, tools reference,
and source-of-truth maintenance guidance are also present. Items below are
follow-up work, not descriptions of missing current syntax documentation.

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

## Medium-term tasks

### Define stability labels

Use simple stability labels in documentation so readers know whether a page is
introductory, normative, implementation-derived, or provisional. This is
useful for distinguishing the canonical language reference from introductory
and implementation-derived pages.

### Cross-link implementation-derived references

For implementation-derived documents such as the bytecode and libcall references,
add explicit links to their source-of-truth files and tests. This makes drift
easier to spot during review.

### Expand the test coverage map into a documentation coverage map

Track not only what code is tested, but also what language/runtime behaviours are
documented. This can highlight features that are implemented and tested but not
yet explained to users.

## Long-term tasks

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
2. Add examples and coverage notes for the canonical language reference as
   later 0.7.3 stages document values, calls, ownership, and limits.
