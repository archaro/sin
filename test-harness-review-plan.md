# Test Harness Review Plan

## Objective

Review Sinistra's test harness so that every retained test protects a distinct,
meaningful contract. Remove tests or private cases that are strictly subsumed
by broader coverage, strengthen tests whose names overstate what they prove,
and keep superficially similar tests when they exercise different layers,
failure modes, or lifecycle boundaries.

This is test maintenance only. It must not change production behaviour,
language semantics, bytecode or persistence formats, or public interfaces.
Each implementation phase uses at most one isolated Luna worker, followed by
root review and validation. Work stops after every phase for user review.

## Baseline

- Starting commit: `857d5184abfaef5ed9621e85dbbe5b7a67a69b1d`
- Starting tree: clean
- Standard harness: 226 tests
  - core: 111
  - compiler: 31
  - runtime: 84
- `make test`: passes all 226 unified-harness tests and the network tests

## Review standard

A test earns its place when it has at least one assertion, input class,
integration boundary, failure mode, ownership rule, or regression condition
not already proved by a broader test at the same layer.

Tests are not duplicates merely because they share inputs. In particular, keep:

- itemstore-load rejection separate from interpreter pre-execution rejection;
- value-helper edge cases separate from encoded-opcode/VM integration cases;
- ordinary interpreter golden runs separate from repeated stress/determinism
  runs;
- strict validation separate from strict runtime-contract option behaviour;
- task ID/type validation separate from active-task lifecycle behaviour; and
- registry initialization failure, teardown idempotence, reinitialization, and
  lazy initialization as distinct lifecycle contracts.

Do not split tests solely to make failures more granular, merge unrelated
contracts into large catch-all tests, add assertions for implementation details,
or replace useful boundary coverage with lower test counts.

## Phase 1: Baseline, audit, and plan

1. Record the clean-tree and test-count baseline.
2. Run the standard test gate.
3. Use one read-only Luna audit to compare registrations and implementations
   across the core, compiler, and runtime suites.
4. Root-review every proposed overlap against the actual assertions.
5. Commit this complete plan and stop.

Acceptance:

- The baseline is green.
- Candidate removals name the retaining test and the unique contract, if any.
- Similar-but-distinct tests are explicitly protected from accidental removal.
- No test or production code changes are made in this phase.

## Phase 2: Remove strictly subsumed coverage

Use one Luna implementation worker for the following bounded consolidations:

1. Remove
   `test_task_introspection_count_returns_zero_with_no_tasks`; the initial and
   final assertions in
   `test_task_introspection_count_and_exists_with_lifecycle` already prove the
   zero-task result and error preservation, while also covering active and
   closing task states.
2. Remove
   `test_itemstore_verifier_rejects_malformed_code_item_bytecode`; its truncated
   string-operand fixture and strict-load rejection are already one case in the
   six-case `test_load_itemstore_rejects_malformed_code_bytecode` matrix.
   Remove the now-unused test source from the build and reconcile repository
   documentation if its file inventory changes.
3. Remove the private
   `test_emitbc_invalid_post_emit_bytecode_fails` case from
   `test_emitbc_invariants`; registered
   `test_emitbc_post_emission_verification` proves the same parameter/local
   failure and diagnostic, plus five other post-emission verifier failures.
4. Reconcile harness declarations/registrations, Makefile inputs, documented
   coverage, and suite counts without hand-editing generated files.

Acceptance:

- Only strictly subsumed assertions are removed.
- The retaining tests still execute in the expected suites.
- Harness counts and `tests/TEST_COVERAGE.md` match the executable registry.
- `make test` and `make test-warnings` pass.
- Commit the focused consolidation and stop.

## Phase 3: Make the large-local pipeline test truthful

Use one Luna implementation worker to repair
`test_pipeline_large_local_lookup_duplicate`. It currently proves semantic
indexing for 120 locals and successful lowering, but its name and pipeline
placement imply a stronger end-to-end contract.

Prefer strengthening it through emission with assertions that the high local
index and the original index of the duplicate survive lowering and bytecode
encoding. If the existing fixture cannot support a stable emitted-byte
assertion without coupling to implementation details, rename and relocate the
test to describe the semantic/lowering boundary precisely. Preserve the
120-local boundary and duplicate-name regression either way.

Acceptance:

- The test name, suite placement, and assertions describe the same contract.
- The 120-local and duplicate-index boundaries remain covered.
- Assertions target public pipeline output or stable bytecode schema, not IR
  allocation or incidental instruction ordering.
- Run the narrow compiler tests while iterating, then `make test`,
  `make test-release`, and the sanitizer/fuzz gate required for compiler changes.
- Commit the focused strengthening and stop.

## Phase 4: Remaining contract-quality pass

Use one read-only Luna audit of the post-Phase-3 tree, with root making only
tiny mechanical edits or assigning one bounded Luna implementation if a
high-confidence defect remains. Review the remaining registry entries for:

- assertions weaker than their test names;
- repeated valid/invalid helper sweeps that prove no additional detail;
- tests registered in a suite that does not match the layer exercised;
- coverage-map claims that do not match executable assertions; and
- pedantic checks of private representation with no regression value.

The string invalid-argument families are a review target, not a presumed
deletion: generic invalid-argument behaviour and alternate context-itemroot
provenance are distinct. Strengthen diagnostic detail only if the current
assertions could pass with the wrong public error contract.

Acceptance:

- Every change is tied to a named, demonstrable coverage defect.
- No speculative cleanup, harness refactor, or test-count target is introduced.
- Similar tests with distinct layer or lifecycle contracts remain.
- Run checks proportional to any files changed, then `make test`.
- Commit any focused correction; if no correction is justified, record that
  result without an empty commit. Stop.

## Phase 5: Final validation and independent review

1. Run the complete standard and strict-warning gates.
2. Run release and sanitizer checks required by the compiler/runtime test
   changes, including LeakSanitizer outside a ptrace-restricted sandbox.
3. Use one read-only Luna reviewer for the complete plan-to-HEAD diff. The
   reviewer checks that removals are genuinely subsumed, retained boundaries
   remain meaningful, counts and documentation agree, and production behaviour
   is untouched.
4. Root-review the diff and test evidence, correct only confirmed issues, and
   commit any correction separately.
5. Stop with a final report; do not tag or publish unless separately requested.

Acceptance:

- All required gates pass, or an exact environmental limitation is reported.
- The final review finds no lost contract or misleading test.
- The working tree is clean and commits are focused.
- The final report lists changed tests, retained lookalikes, documentation or
  fixture impact, every command run, and every commit created.

## Commit policy

Commit once per completed phase that changes tracked files. Before every
commit, inspect `git status --short` and the staged diff. Do not commit build
artifacts, generated parser/lexer files, executables, or temporary fixtures.
