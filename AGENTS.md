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

Use a small, stable team for non-trivial code changes:

- The root orchestrator owns requirements, architecture, task decomposition,
  acceptance criteria, diff review, integration decisions, and the final
  answer. It audits the work directly rather than delegating routine final
  review.
- Use one implementation agent to write code and make corrections. Reuse that
  agent for the bounded task rather than starting fresh implementation agents.
- Use one low-capability, read-only agent for repository searches and test
  execution. It may report findings and exact failures, but it never edits
  files, diagnoses failures, or proposes fixes.

When using OpenAI models, use the Model-specific in `OPENAI.md` to select the
models and reasoning levels for these roles. The root remains the final
reviewer and must inspect the diff and test evidence rather than accepting an
agent's summary alone.

### Isolated handoffs

For every implementation, search, or test handoff:

- Never pass the root conversation history: provide the minimum context
  necessary to implement the requested task.
- Send a focused, self-contained handoff rather than transcripts, raw context
  dumps, or unrelated findings.
- Include only what the recipient needs: the bounded task or question, relevant
  acceptance criteria, decisions and files, necessary tree state, pertinent
  failures or review findings, and the expected deliverable.
- Summarize prior work. Include raw output only when a short excerpt is needed
  to diagnose a specific failure.
- Remind implementation agents that the workspace is shared and that they must
  preserve unrelated changes. Never run two implementation agents concurrently
  against the same working tree.

Keep discovery handoffs narrow: ask concrete questions and name likely paths
when known. Return only the findings needed for the orchestrator to make the
next decision. The orchestrator should use those findings without repeating
the same searches unless the tree changed or the result is incomplete.

### Test ownership and reuse

The read-only test agent owns test execution. The orchestrator defines the
proportionate test plan, evaluates the evidence, and decides whether failures
require another implementation iteration.

- Run narrow tests while iterating only when their result can affect the next
  code change. Run the planned final gates once after the candidate is stable.
- A successful test result remains valid while the tested code, build inputs,
  fixtures, and test configuration remain unchanged. Auditing the diff,
  reviewing output, or integrating the result does not invalidate it.
- Do not hand off for "final testing" and then rerun the same coverage as an
  "integration test" when no relevant changes occurred. The orchestrator's
  integration responsibility is review and evaluation, not duplicate execution.
- After a correction, rerun only tests whose result could be affected, then run
  any remaining planned gates that have not yet completed against that revision.
- Repeat a successful test only when relevant inputs changed, the earlier run
  was incomplete or unreliable, or a higher-priority instruction explicitly
  requires an independent run. State the reason when repeating it.
- Return concise results to the orchestrator, with exact command lines and
  errors verbatim. Do not transfer bulky successful logs unless requested.

### Implementation and correction

Use the following pattern for non-trivial code changes:

1. Inspect enough of the repository to define a bounded task, explicit
   acceptance criteria, affected subsystems, constraints, and a proportionate
   test plan. Delegate repository discovery to the read-only agent where useful.
2. Give the implementation to the designated implementation agent.
3. Review its diff directly, then have the read-only test agent run the checks
   that are useful at this revision.
4. If the result fails a check, is incomplete, violates an acceptance
   criterion, requires substantial correction, or leaves an unresolved
   certainty, refer back to the implementation agent for correction, with
   detailed and bounded instructions on what is wrong and what needs to be
   fixed.

Keep corrections with the same implementation agent. If repeated corrections
do not converge, the root must reassess the requirements, scope, and acceptance
criteria before deciding how to proceed; do not silently add implementation or
review agents.

The root may make tiny mechanical edits. Substantive implementation follows
the pattern of an implementation agent as code-monkey, irrespective of what
the orchestration model is.

### Agent count and review scope

Use the smallest workflow that satisfies the task:

- One bounded task normally needs the root orchestrator, one implementation
  agent, and one read-only search/test agent. Do not add routine task or
  whole-branch reviewers; the root owns audit and integration.
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

Use `docs/internals/architecture.md` for module ownership, dependency direction,
and key entry points. Update it whenever source files are added, deleted, or
relocated.

- `src/common/`: shared diagnostics, allocation, CLI, formatting, and utilities.
- `src/bytecode/`: bytecode verification and disassembly support.
- `src/compiler/`: parser, AST, semantics, IR, lowering, diagnostics, and emission.
- `src/runtime/`: VM, values, stack, tasks, decoding, and opcode execution.
- `src/itemstore/`: persistence, registries, caches, and structured error items.
- `src/libcall/`: host library calls and registry plumbing.
- `src/net/`: libuv networking and Telnet support.
- `tests/`: core, compiler, interpreter, network, fixture, benchmark, and fuzz tests.
- `docs/`, `docs/guide/examples/`, and `ci/`: documentation, sample programs, and local/CI gates.
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

1. `make test`: deterministic framework, contract, and network tests.
2. `make test-sanitize`: ASan/UBSan with leak detection. Run this outside a
   ptrace-restricted sandbox on the first attempt; LeakSanitizer cannot run
   correctly under ptrace.
3. `BUILD=release make test`: release behavior, especially compiler/interpreter work.
4. `./ci/gate_sanitizers_fuzz.sh`: compiler, runtime, parser, bytecode,
   itemstore loading, or fuzz-harness changes.

Use `make bench` when benchmark measurements are relevant; `make test` is the
normal deterministic gate.

Fuzzing is `make test-fuzz`, which builds and runs all three harnesses. Tune
campaigns with `FUZZ_RUNS` and `FUZZ_TIME`; do not edit scripts for local
iteration.

Validation by change type:

- Documentation only: inspect Markdown; no build unless examples are generated.
- Build system: `make clean`, `make`, and `make test` when feasible.
- Core C logic: `make test`.
- Compiler/parser/language: `make test`, `BUILD=release make test`, targeted golden
  tests, and the sanitizer/fuzz gate.
- Runtime/bytecode/itemstore: `make test`, applicable sanitizers, and relevant
  fuzz smoke tests.
- Fuzz harness: `make test-fuzz` with a seeded run.

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
- Document user-visible behavior in `docs/reference/libcalls.md`.

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
