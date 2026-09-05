# AGENTS.md

These instructions apply to the repository rooted here. A deeper `AGENTS.md`
overrides this file for its subtree. User, system, and developer instructions
take precedence.

When running on Codex, review OPENAI.md for multi-agent instructions.

## Project

Sinistra is a C17 MUD engine with four executables:

- `scomp` compiles Sinistra source to custom object/bytecode data.
- `sin` loads object data, runs tasks, handles networking, and persists state.
- `sdiss` disassembles object data and bytecode fixtures.
- `sconv` converts itemstores from earlier on-disk versions to the latest
  version, migrating the code items they contain.

The codebase intentionally mixes older and newer C styles. Make small,
idiomatic changes that fit nearby code; avoid broad cleanup during feature work.

## Task framing and decisions

- Establish the requested outcome, observable acceptance criteria, affected
  subsystems, and compatibility constraints before editing. Scale planning to
  the task; a small change does not need a formal plan.
- Use nearby code, tests, and the authoritative build files to check assumptions.
  Read enough to resolve the next decision; avoid repository-wide exploration
  when a targeted inspection will answer it.
- Resolve routine implementation choices from repository conventions. Ask when
  missing information would materially change public behavior, compatibility,
  or scope; continue independent work while awaiting an answer.
- Prefer the smallest coherent change that satisfies the outcome. Carry a
  behavior change through its consumers, validation, tests, and documentation.
- Explain consequential decisions with evidence and tradeoffs. Distinguish
  observed facts from hypotheses and identify uncertainty that affects the
  result.

## Investigation and review

- For a failure, capture the exact command, input, and diagnostic. Reproduce it
  when feasible, trace the failing path, and test a specific hypothesis before
  changing code. Separate environment failures from product failures.
- Add a regression case that exercises the failed contract. Check relevant
  boundaries and error paths; a test should detect incorrect behavior rather
  than merely repeat the implementation.
- Review the actual diff against the acceptance criteria, including ownership,
  cleanup, compatibility, and affected callers. Passing tests support review
  but do not establish that every requirement was implemented.
- Finish when the requested outcome is implemented, the diff is reviewed, and
  the applicable checks have passed. If a check cannot run, report the exact
  limitation and its effect on confidence; do not claim it passed.
- Keep unrelated findings separate from the requested change. Do not extend a
  completed task into speculative cleanup or repeated validation.

Every handoff must be isolated: disable conversation-history forking and supply
only the bounded task, relevant paths and decisions, acceptance criteria,
necessary tree state, and expected deliverable. Remind the writer that the
workspace is shared and unrelated edits must be preserved. Reuse discovery
findings unless the tree changed or the answer was incomplete.

Add an independent reviewer only for unusual risk, cross-subsystem work,
specialist needs, or an explicit request. Reviewers are read-only: no edits,
commits, ref changes, or destructive Git commands. Record `HEAD` and tree state
before review and verify them afterward. The root still audits the diff and
test evidence directly. Additional reviewers need distinct, justified scopes.

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
- `tests/inventory/`: checked-in contract catalogs audited by `make test`; follow
  `tests/inventory/README.md` when changing canonical definitions or test mappings.
- `docs/`, `docs/guide/examples/`, and `ci/`: documentation, sample programs, and local/CI gates.
- `Makefile`: public build and test interface; implementation lives in
  `mk/build.mk`, `mk/tests.mk`, and `mk/fuzz.mk`.

Parser sources are `src/compiler/parser.y` and `src/compiler/lexer.l`; generated
files belong under `obj/<build>-<compiler>/generated/`, not in source control.

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
`BUILD`, `PKG_CONFIG`, `LIBUV_PC`, and `TEST_JOBS`. Strict warnings are enabled
by the Makefile.

## Validation

Run the narrowest meaningful checks while iterating, then broaden according to
risk. Preferred component gates are:

1. `make test`: deterministic framework, contract, and network tests.
2. `make test-sanitize`: ASan/UBSan with leak detection. Run any command that
   includes this target outside a ptrace-restricted sandbox on the first
   attempt; LeakSanitizer cannot run correctly under ptrace.
3. `BUILD=release make test`: release behavior, especially compiler/interpreter work.
4. `./ci/gate_sanitizers_fuzz.sh`: compiler, runtime, parser, bytecode,
   itemstore loading, or fuzz-harness changes.

The combined sanitizer/fuzz gate already runs `make test`,
`BUILD=release make test`, `make test-sanitize`, and a seeded `make test-fuzz`.
When that combined gate is required, run the whole script outside a
ptrace-restricted sandbox and treat it as the final broad gate. Do not run its
component gates separately at the same unchanged revision unless a component
result is needed earlier to guide implementation or the combined run was
incomplete or unreliable.

Successful checks remain valid while the tested code, build inputs, fixtures,
and configuration are unchanged. Diff review and handoffs do not invalidate
them. After a correction, rerun affected checks and any remaining planned
gates. State the reason for repeating a successful check.

Use `make test-full` when coverage-floor implications are part of the change;
see `docs/internals/testing/workflow.md`. It includes debug, release, coverage,
sanitizer, and fuzz gates, so the same sandbox and check-reuse rules apply.
Coverage snapshots, floors, and contract catalogs are reviewed source data;
do not weaken or regenerate them merely to clear a failure.

Use `make bench` when benchmark measurements are relevant; `make test` is the
normal deterministic gate.

Fuzzing is `make test-fuzz`, which builds and runs all three harnesses. Tune
campaigns with `FUZZ_RUNS`, `FUZZ_TIME`, and `FUZZ_SEED`; do not edit scripts for
local iteration.

Validation by change type:

- Documentation only: inspect Markdown; no build unless examples are generated.
- Build system: `make clean`, `make`, and `make test` when feasible.
- Core C logic: `make test`.
- Compiler/parser/language: targeted golden tests and the combined
  sanitizer/fuzz gate, which includes debug and release tests.
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
- Document user-visible behavior in the relevant library-specific page under
  `docs/reference/`; update `docs/reference/libcalls.md` only when the shared
  libcall policy or index changes.

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

For a PR or completed code change, report:

- a concise summary and repository-relative citations for changed files;
- every test/check command run, prefixed with `✅`, `⚠️`, or `❌`;
- tests not run and why, including sandbox or dependency limitations;
- documentation and fixture changes; and
- commits or PRs only after they actually exist.
