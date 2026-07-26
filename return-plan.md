# Explicit RETURN implementation plan

## Goal and language contract

Replace implicit code-item results with explicit return statements:

```sinistra
RETURN expression;
RETURN;
```

`RETURN expression;` evaluates its expression exactly once, immediately stops
the current code item, and returns that value to its caller. `RETURN;`
immediately stops the current code item and returns `nil`.

Falling off the end of a code item also returns `nil`. Expression statements
are always evaluated for their side effects and discarded, including the final
top-level expression statement. A VM `HALT` must never infer a result from a
residual stack value. Stored value items continue to return their stored value;
this change applies to code-item execution.

To make that distinction enforceable in bytecode, add a separate
`IR_OP_RETURN` instruction. `IR_OP_RETURN` consumes exactly one value and
terminates the current frame with it. `IR_OP_HALT` consumes no value and
terminates the current frame with `nil`. Append `IR_OP_RETURN` to the IR enum
so existing numeric IR values do not move, and encode it with the currently
unused byte `Q`.

The bytecode verifier must decode the complete bounded bytecode buffer.
`HALT` and `RETURN` are control-flow terminators, not physical end-of-buffer
sentinels. Intermediate terminators are valid, so later branch targets and
unreachable instructions must still be decoded and checked structurally. The
compiler continues to append a final structural `HALT`, and compiler-produced
bytecode must end with it.

## Phase 1 — discovery and complete plan

- Trace current results through grammar, AST ownership and walking, semantic
  analysis, lowering, IR/schema/emission, verification, disassembly, runtime
  frame unwinding, fixtures, tests, and documentation.
- Record the new source and bytecode contracts, affected subsystems,
  acceptance criteria, validation, agent budget, and commit boundaries.
- Create an isolated feature branch.
- Commit this plan and stop for review.

## Phase 2 — complete bounded implementation

Use one isolated Luna implementation agent for the coherent cross-subsystem
change. The root agent reviews its complete diff and test evidence before
accepting or committing any work.

### Compiler front end and lowering

- Parse both `RETURN;` and `RETURN expression;` without introducing grammar
  conflicts. Store the optional expression in `N_RETURN::lhs`.
- Preserve AST destruction, allocation-failure cleanup, semantic traversal,
  embedded-code handling, and debug walking for the optional expression.
- Lower `RETURN expression;` as expression evaluation followed by
  `IR_OP_RETURN`.
- Lower bare `RETURN;` as `IR_OP_HALT`.
- Remove final-expression result preservation. Every expression statement,
  regardless of position or nesting, must emit `IR_OP_DISCARD`.
- Continue appending a final `IR_OP_HALT` for ordinary fallthrough and as the
  structural end of compiler-produced bytecode.

### IR, bytecode, verifier, and disassembler

- Append `IR_OP_RETURN` to the IR enum and add it to the canonical opcode
  schema as byte `Q`, with no operand and no ordinary runtime handler.
- Teach every exhaustive IR/schema/emitter/decoder/disassembler mapping about
  the new opcode.
- Give `RETURN` stack effect `pop 1, push 0`; keep `HALT` at `pop 0, push 0`.
  Both instructions terminate control-flow and have no successor. At every
  reachable `RETURN`, require exactly one operand above the frame's parameter
  baseline; a parameter or local slot must not be mistaken for a return value,
  and additional residual operands indicate invalid bytecode.
- Decode the complete bytecode buffer instead of returning at the first
  `HALT`. Record every instruction boundary before validating jumps and stack
  flow.
- Permit valid instructions, labels, and jump targets after an intermediate
  `HALT` or `RETURN`, including unreachable instructions.
- Reject malformed bytes anywhere in the bounded buffer, jumps into operands,
  `RETURN` stack underflow, stack-depth conflicts on reachable joins, and
  compiler output whose final physical instruction is not `HALT`.
- Remove or narrowly replace the obsolete “trailing bytes after HALT” policy
  and tests: a halt is no longer evidence that following bytes are trailing.

### Runtime frame results

- On `HALT`, unwind the current frame and supply `VALUE_NIL`, discarding every
  residual operand above the frame as part of cleanup.
- On `RETURN`, pop exactly the explicit expression value before unwinding, then
  pass that owned value to the caller or return it from the top-level
  interpreter invocation.
- Preserve string ownership, item-use pins, nested interpreter state, caller
  continuation, locals/parameters, and error cleanup on both termination
  paths.
- Ensure handcrafted or stale bytecode equivalent to `PUSH value; HALT`
  returns `nil`, never the pushed value.

### Focused coverage

- Add parser/AST coverage for bare and valued returns, nested returns, malformed
  return expressions, and allocation cleanup without duplicating broader
  parser tests.
- Add lowering and opcode-schema coverage proving all expression statements
  discard, valued returns emit `RETURN`, bare returns emit `HALT`, and the
  compiler still appends its final structural `HALT`.
- Replace the old verifier trailing-byte assertions with meaningful cases for
  multiple/intermediate terminators, jumps to instructions after a terminator,
  malformed later instructions, and `RETURN` underflow.
- Update interpreter result-semantics coverage to prove:
  - a final expression statement returns `nil`;
  - several residual expression values followed by `HALT` return `nil`;
  - bare `RETURN;` exits early with `nil`;
  - `RETURN expression;` returns each supported runtime value;
  - returned owned strings survive callee-frame cleanup;
  - returns from `IF`, `WHILE`, `DO..WHILE`, embedded code, and nested calls
    terminate only the current code item;
  - code after a taken return is not executed; and
  - stored value items still return their stored values.
- Migrate test sources and checked-in example/fixture sources that require a
  code-item result to use explicit `RETURN`. Do not add returns to callback
  code that intentionally returns `nil`.
- Keep fixtures minimal and update expected bytecode/disassembly output only
  where the new opcode or mandatory discards change it.

### Documentation

- Replace implicit final-expression descriptions in `docs/concepts.md`,
  `docs/runtime.md`, and `docs/bytecode.md`.
- Document both source forms, early exit, fallthrough `nil`, the distinct
  `RETURN`/`HALT` bytecodes, and explicit-only code-item values.
- Update `tests/TEST_COVERAGE.md` and fixture metadata where coverage or
  fixture content changes.
- Update `docs/architecture.md` only if files are added, removed, or relocated.

### Phase acceptance

- Bison reports no new conflicts.
- `RETURN expression;` evaluates once and returns that value.
- `RETURN;`, ordinary fallthrough, and `PUSH value; HALT` return `nil`.
- No expression statement implicitly becomes a code-item result.
- Every reachable `RETURN` has exactly one verifier-proven operand above its
  frame baseline.
- Multiple control-flow terminators and valid later branch targets verify.
- Invalid bytes after an intermediate terminator are still diagnosed.
- Nested returns restore caller frames and preserve owned return values.
- Value items retain their existing return behaviour.
- Focused compiler, verifier, disassembler, runtime, fixture, and example tests
  pass.
- `make test` passes.
- Commit the language/runtime change as one coherent feature commit. Any
  independently discovered bugfix or refactor must use a separate commit.
- Stop after root review, targeted validation, and commit.

## Phase 3 — independent integration review

Use one read-only Luna reviewer covering specification compliance and
integration quality. Record `HEAD` and worktree state before review and verify
both afterward. Review the feature commit against this plan, especially:

- grammar ambiguity and AST ownership for optional expressions;
- removal of every implicit source-level result path;
- opcode schema completeness and bytecode compatibility diagnostics;
- full-buffer decoding, terminator CFG edges, jump boundaries, and stack flow;
- frame unwinding, string ownership, nested calls, and residual-value cleanup;
- meaningful, non-duplicated positive and negative tests;
- fixture/example migration; and
- documentation matching implemented source, bytecode, and runtime behaviour.

Resolve substantive findings with one focused Luna correction turn and a
separate correction commit where appropriate, then re-review. Run only the
narrow tests covering a correction. Stop when the review is clean.

## Phase 4 — final validation and release readiness

- Run `make test`.
- Run `make test-release`.
- Run `./ci/gate_sanitizers_fuzz.sh` outside the ptrace-restricted sandbox.
  This supplies strict-warning, leak-enabled ASan/UBSan, and seeded
  compiler/runtime/object-loader fuzz coverage without duplicating those
  individual gates.
- Inspect Bison conflict output, final commit boundaries, fixture/example and
  documentation changes, `git diff --check`, and worktree cleanliness.
- Report every command and environmental limitation.
- Mark the branch ready only when the worktree is clean, review has no open
  findings, and every required gate passes.

## Agent and token budget

- Phase 2: one isolated Luna implementation agent for the complete coherent
  change.
- Phase 3: one isolated, read-only Luna reviewer; reuse it for re-review if
  necessary.
- No other subagents unless a concrete blocker cannot be resolved through
  focused root inspection.
- Handoffs contain this plan plus only relevant file paths, current tree state,
  and concise observed failures; no conversation transcript or broad dump.

## Expected commit structure

1. `Plan explicit RETURN semantics`
2. `feat(language): require explicit return values`
3. Separate correction commits only for independent review or validation
   findings.
