# AGENTS.md

These instructions apply to the repository rooted here. A deeper `AGENTS.md`
overrides this file for its subtree. User, system, and developer instructions
take precedence.

## Project

Sinistra is a C17 MUD engine with four executables:

- `scomp` compiles Sinistra source to custom object/bytecode data.
- `sin` loads object data, runs tasks, handles networking, and persists state.
- `sdiss` disassembles object data and bytecode fixtures.
- `sconv` converts itemstores from earlier on-disk versions to the latest
  version, migrating the code items they contain.

The codebase intentionally mixes older and newer C styles. Make small,
idiomatic changes that fit nearby code; avoid broad cleanup during feature work.

## Multi-agent implementation policy

The root agent owns requirements, architecture, task decomposition, acceptance
criteria, final validation, and the final answer. It is always the final
reviewer and must not accept a worker's summary without inspecting the diff and
test evidence.

### Isolated handoffs

For every native implementation, correction, or review agent:

- Never pass the root conversation history: provide the minimum context
  necessary to implement the requested task.
- Run tests with a low-capability, read-only agent. Return the results to the
  orchestrator; include errors verbatim. That agent executes and reports only;
  it never edits files or diagnoses failures.

For every implementation, correction, or review handoff:

- Send a focused, self-contained handoff rather than transcripts, raw context
  dumps, or unrelated findings.
- Include the bounded task, acceptance criteria, relevant decisions and files,
  current tree state when relevant, tests to run, pertinent failures or review
  findings, and the expected deliverable.
- Summarize prior work. Include raw output only when a short excerpt is needed
  to diagnose a specific failure.
- Remind implementation agents that the workspace is shared and that they must
  preserve unrelated changes. Never run two implementation agents concurrently
  against the same working tree.

### Implementation and escalation

For non-trivial code changes:

1. Inspect enough of the repository to define a bounded task, explicit
   acceptance criteria, affected subsystems, constraints, and tests.
2. Give the first implementation attempt to a medium-capability agent.
3. Review its diff and test evidence yourself.
4. If the result fails a check, is incomplete, violates an acceptance
   criterion, requires substantial correction, or leaves an unresolved
   certainty, refer back to the implementation agent for correction, with
   detailed and bounded instructions on what is wrong and what needs to be
   fixed.

Escalation is bounded. Count the initial implementation as attempt 1:

- Attempts 1 through 5 belong to the medium-capability implementation agent.
  Repeat step 4 for each correction.
- If attempt 5 still fails root review, hand the task to a high-capability
  review agent with a fresh, self-contained handoff stating what the
  implementation agent produced, which acceptance criteria remain unmet, and
  the specific failures. Do not forward the implementation agent's transcript.
- The high-capability review agent gets at most two attempts, reviewed against
  the same criteria.
- If the review agent's second attempt still fails, the root implements the
  fix itself.

Never restart the ladder for the same bounded task; re-scoping a failed task
does not reset the count. A task that reaches the root stays with the root.

The root may make tiny mechanical edits. Substantive implementation follows
the pattern of an implementation agent as code-monkey, irrespective of what
the orchestration model is.

### Agent count and review scope

Use the smallest workflow that satisfies the task:

- One bounded task normally needs one implementation process plus root review
  and validation. Do not automatically add both task and whole-branch reviewers.
- Add one independent reviewer only for unusual risk, cross-subsystem work,
  multiple independently implemented tasks, specialist needs, or an explicit
  user request. For a single-task branch, that reviewer should cover both
  specification compliance and integration quality.
- Use multiple reviewers only when their scopes are materially different and
  the plan records why the added cost is justified.
- Reviewer agents are strictly read-only: prohibit file edits, commits, ref
  changes, and destructive Git commands. Record `HEAD` before review and verify
  `HEAD` and the working tree afterward.
- Match reviewer model capability and reasoning effort to the concrete risk;
  do not default to the strongest model for a small diff.
- Higher-priority instructions or an explicitly requested skill may require a
  different workflow; otherwise this policy governs.

## Repository map

Use `docs/architecture.md` for module ownership, dependency direction, and key
entry points. Update it whenever source files are added, deleted, or relocated.

- `src/common/`: shared diagnostics, allocation, CLI, formatting, and utilities.
- `src/bytecode/`: bytecode verification and disassembly support.
- `src/compiler/`: parser, AST, semantics, IR, lowering, diagnostics, and emission.
- `src/runtime/`: VM, values, stack, tasks, decoding, and opcode execution.
- `src/itemstore/`: persistence, registries, caches, and structured error items.
- `src/libcall/`: host library calls and registry plumbing.
- `src/net/`: libuv networking and Telnet support.
- `tests/`: core, compiler, interpreter, network, fixture, benchmark, and fuzz tests.
- `docs/`, `examples/`, and `ci/`: documentation, sample programs, and local/CI gates.
- `Makefile`: authoritative build, test, sanitizer, and fuzz interface.

Parser sources are `src/compiler/parser.y` and `src/compiler/lexer.l`; generated
files belong under `obj/generated/`, not in source control.

## Build and toolchain

Use the Makefile rather than ad hoc compiler commands except for narrow
diagnosis. The Unix-like build requires `make`, a C compiler, `pkg-config`,
libuv development files, Bison, Flex, `ar`, and `xxd`. Do not vendor missing
dependencies; report the exact failing command and error.

- `make`: debug builds of `scomp`, `sdiss`, `sin`, and `sconv`.
- `make BUILD=release`: optimized build.
- `make BUILD=sanitize`: ASan/UBSan build.
- `make clean`: remove generated and built artifacts.
- `make help`: list targets and tunable variables.

Keep C code compatible with C17. Useful variables include `CC`, `CSTD`,
`STRICT_WARNINGS`, `BUILD`, `PKG_CONFIG`, and `LIBUV_PC`.

## Validation

Run the narrowest meaningful checks while iterating, then broaden according to
risk. Preferred gates are:

1. `make test`: standard harness and network tests.
2. `make test-warnings`: strict-warning regressions.
3. `make test-asan`: ASan/UBSan with leak checks disabled.
4. `make test-lsan`: ASan/UBSan with leak checks enabled. Run this outside a
   ptrace-restricted sandbox on the first attempt; LeakSanitizer cannot run
   correctly under ptrace.
5. `make test-release`: release behavior, especially compiler/interpreter work.
6. `./ci/gate_sanitizers_fuzz.sh`: compiler, runtime, parser, bytecode,
   itemstore loading, or fuzz-harness changes.

Use `make test-strict` only when benchmark-budget enforcement is relevant;
`make test` is the normal deterministic gate.

Fuzz targets are `make fuzz-smoke`, `make fuzz-build`, `make fuzz-smoke-run`,
and the individual `make fuzz-scomp`, `make fuzz-sdiss`, and
`make fuzz-sin-object` targets. Tune smoke runs with `FUZZ_RUNS` and
`FUZZ_TIME`; do not edit scripts for local iteration.

Validation by change type:

- Documentation only: inspect Markdown; no build unless examples are generated.
- Build system: `make clean`, `make`, and `make test` when feasible.
- Core C logic: `make test`; consider `make test-warnings`.
- Compiler/parser/language: `make test`, `make test-release`, targeted golden
  tests, and the sanitizer/fuzz gate.
- Runtime/bytecode/itemstore: `make test`, applicable sanitizers, and relevant
  fuzz smoke tests.
- Fuzz harness: the specific harness plus a seeded run, or `make fuzz-smoke`.

Every behavior change needs corresponding unit, integration, golden,
benchmark, or fuzz coverage as appropriate. Never remove a failing test merely
to make a suite pass.

## C conventions

- Follow nearby naming, indentation, error handling, and ownership patterns.
  For new or substantially rewritten C, use indentation options
  `-br -ce -slc -nut -i2 -brf -npcs -npsl`.
- Avoid unrelated formatting and mechanical rewrites. Prefer focused functions
  and explicit control flow over clever macros.
- Treat warnings as defects. Avoid unchecked narrowing; the default build uses
  `-Wconversion` and `-Wsign-conversion`.
- Prefer `size_t` for object sizes and indices while respecting existing APIs.
- Initialize structs deliberately and release resources on every error path.
  Make ownership clear where allocations are introduced.
- Do not commit generated parser/lexer files or build artifacts.

## Change-specific checks

### Compiler, parser, and bytecode

Keep language and bytecode changes coherent across the applicable stages:

- grammar and lexer: `src/compiler/parser.y`, `src/compiler/lexer.l`
- AST and semantics: `src/compiler/absyn.*`, `src/compiler/semant.*`
- IR and lowering: `src/compiler/ir.*`, `src/compiler/ir/`, `src/compiler/lower.*`
- emission and verification: `src/compiler/emitbc.*`,
  `src/bytecode/bytecode_verify.*`
- runtime and disassembly: `src/runtime/runtime_decode.*`,
  `src/runtime/runtime_opcode.*`, `src/runtime/interpret.*`,
  `src/bytecode/sdiss_core.*`
- relevant documentation, positive/negative tests, fixtures, and golden output

New or changed language components need validator coverage, emitter/schema
updates where applicable, and positive and negative tests.

### Runtime, itemstore, and persistence

- Preserve on-disk compatibility unless the task explicitly changes a format.
- Format changes require documentation plus encoding, header, and verifier tests.
- Validate untrusted object data before runtime use and add malformed/boundary
  regression fixtures when changing decode or load paths.
- Keep cache invalidation, registry ownership, and cleanup rules explicit.

### Library calls

- Use existing registry patterns.
- Test argument validation, truthiness and value conversion, ownership, returns,
  side effects, and error behavior as applicable.
- Document user-visible behavior in `docs/libcalls.md`.

### Networking

- Avoid blocking event-loop paths unless an established API requires it.
- Treat libuv handle, callback, buffer, and close ownership as explicit design
  concerns.
- Add or update network tests for behavior changes.

### Fixtures and documentation

- Keep fixtures minimal, deterministic, and inspectable; follow
  `tests/fixtures/README.md` for generation and update both input and expected
  output when behavior intentionally changes.
- Update documentation when public behavior, commands, formats, architecture,
  or contributor workflow changes. Common targets are `README.md`,
  `QUICKSTART.md`, `CONTRIBUTING.md`, and the relevant file under `docs/`.
- Keep documentation concise and add examples only when they improve clarity.

## Git hygiene and reporting

- Run `git status --short` before editing and before committing. Preserve
  unrelated user changes and work around a dirty tree rather than reverting it.
- Keep commits focused and descriptive. Use a body when motivation,
  implementation details, or validation are not clear from the headline.
- Never commit `obj/`, `lib/`, executables, test/fuzz binaries, generated
  parser/lexer output, or temporary fixture files.
- Use `rg`, `rg --files`, `find`, or targeted commands instead of `ls -R` or
  `grep -R`.
- Avoid broad cleanup unless cleanup is the task.

## Session-efficiency guidance

Keep coding work bounded to a single feature, bug, or review gate. Prefer a
fresh agent session after implementation and validation rather than carrying a
conversation across unrelated tasks. Batch independent repository inspection
and test commands, keep file and terminal output targeted, and avoid polling
loops when a completion notification or foreground command is sufficient.

Do not repeat repository searches or skill loads already performed in the
current task. Delegate only when isolation or genuine parallelism materially
helps; do not duplicate the same inspection in both parent and worker sessions.

For a PR or completed code change, report:

- a concise summary and repository-relative citations for changed files;
- every test/check command run, prefixed with `✅`, `⚠️`, or `❌`;
- tests not run and why, including sandbox or dependency limitations;
- documentation and fixture changes; and
- commits or PRs only after they actually exist.
