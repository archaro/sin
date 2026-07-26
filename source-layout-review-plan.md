# Source Layout Review Plan

## Objective

Review the first-party C source layout and make only a small,
behavior-preserving rearrangement when it reduces the amount of code and
dependency context needed to reason about one responsibility.

This is not a general cleanup. A large file is not sufficient reason to split
it, and a small file is not sufficient reason to merge it. A change must
produce a clearer ownership boundary without introducing semantic edits,
duplicated helpers, or unnecessary private interfaces.

Baseline: `8b5c6056d6fd516c068ea4b7a1ab56ecc8a38f6a`.

## Scope and constraints

- Review tracked, first-party sources under `src/`, their headers, the
  `Makefile`, and `docs/architecture.md`.
- Exclude generated parser/lexer output and vendored
  `src/net/libtelnet.[ch]`.
- Preserve all public symbols, ownership rules, test hooks, on-disk formats,
  diagnostics, and behavior.
- Do not combine a move with formatting, renaming, API, or logic changes.
- Update `docs/architecture.md` and build object lists for every relocated or
  added source file.
- Use the smallest agent workflow: one isolated Luna audit in Phase 1 and one
  isolated Luna implementation agent in Phase 2. The root agent performs all
  reviews and validation; the low-risk, single-boundary change does not
  justify a separate reviewer.
- Commit the completed work in each phase and stop for approval before the next
  phase.
- Because the selected change is layout-only, validation is compilation plus
  static diff checks. If review finds any functional change, stop and either
  remove it or revise the validation scope before proceeding.

## Audit decision

Implement one change: move source-sidecar persistence out of
`src/itemstore/item_persist.c` into a focused itemstore source file.

The movable unit consists of:

- the source write/close test hooks and
  `itemstore_set_source_io_hooks_for_tests()`;
- `save_itemsource_in_srcroot()` and `save_itemsource()`; and
- `read_itemsource_in_srcroot()`.

These functions share source-file path and text I/O concerns, but not the
binary itemstore format, untrusted binary decoding, or atomic itemstore
publication state. The split lets a reader inspect binary persistence without
also loading source-sidecar behavior. Existing declarations remain in
`item.h` and `item_internal.h`; no new header is expected unless compilation
proves a genuinely shared private declaration is required.

Defer splitting `bytecode_verify.c`, `interpret.c`, and `network.c`. Each has a
conceptual seam, but its halves currently share enough private schema,
decoder, callback, or runtime state that a split would add interfaces without
clearly reducing reasoning cost. Reject splitting `libcall_sys.c`: it is a
cohesive host-service namespace and the apparent groups still share its ABI
and error/return conventions. Do not merge the thin runtime files; their
ownership boundaries are distinct.

## Phase 1 — Baseline, audit, and plan

1. Confirm the tracked baseline and working-tree state.
2. Map source sizes, module ownership, includes, and explicit Makefile object
   membership.
3. Have one isolated, read-only Luna agent audit the strongest split/merge
   candidates.
4. Independently inspect the proposed boundaries and accept only the
   source-sidecar split above.
5. Commit this complete plan and stop.

Acceptance:

- The plan records the complete task, the one accepted change, rejected or
  deferred alternatives, agent limits, phase gates, and validation.
- No production or test source changes occur in this phase.

## Phase 2 — Extract source-sidecar persistence

1. Give one isolated Luna implementation agent the exact movable unit,
   constraints, current clean baseline, affected files, and compilation gate.
2. Move the source-sidecar functions and their private hook state verbatim
   into a clearly named file under `src/itemstore/`.
3. Remove only the now-unused includes from `item_persist.c`; add only the
   includes required by the new translation unit.
4. Add the new object to `LIB_OBJECTS` and update the itemstore file map and
   ownership text in `docs/architecture.md`.
5. Root-review the complete diff for symbol preservation, single-instance hook
   state, unchanged function bodies, dependency direction, and absence of
   unrelated edits.
6. Run:
   - `make clean`
   - `make`
   - `git diff --check`
7. Commit the focused layout change and stop.

Acceptance:

- Source-sidecar I/O and its test hooks have one implementation owner.
- Binary itemstore encoding, loading, durability, and publication remain in
  `item_persist.c`.
- Public and internal declarations are unchanged unless compilation requires
  a minimal, justified include adjustment.
- All three executables compile successfully.
- No generated or built artifact is committed.

## Phase 3 — Final layout review

1. Inspect the committed diff from the Phase 1 baseline through Phase 2.
2. Confirm that the new file boundary improves the itemstore map, that
   `docs/architecture.md` and `Makefile` agree with the tree, and that no
   semantic, fixture, test, format, or public API change slipped into the move.
3. Check the final working tree and commit only a narrowly necessary
   documentation/build-map correction, if one is found.
4. Report the final commits and compilation evidence. Do not reopen deferred
   candidates without a new task and concrete coupling pressure.

Acceptance:

- The final tracked tree is clean.
- The only source change is the reviewed layout extraction and necessary
  include/build/documentation wiring.
- Compilation evidence from Phase 2 remains the sole behavioral gate because
  functionality was not altered.
