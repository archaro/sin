# AGENTS.md

Applies to this repository; deeper `AGENTS.md` files override it for their
subtrees. System, developer, and user instructions take precedence.

## Astra workflow

Astra owns task framing, technical decisions, and final review. Establish the
outcome, acceptance criteria, affected subsystems, and compatibility constraints
before editing. Inspect nearby code and authoritative build files; resolve
routine choices independently and ask when missing information changes scope or
public behavior. Prefer the smallest coherent change through callers, tests, and
documentation.

Delegate code-writing to Luna (High) and test running to Luna (Low). Astra
provides bounded work packages, reviews results against acceptance criteria,
and directs corrections. Every handoff uses `fork_turns="none"` and includes
only the task, relevant paths and decisions, acceptance criteria, necessary
tree state, and expected deliverable. Remind agents that the workspace is shared
and unrelated edits must be preserved. Reuse discovery and successful checks.

Add an independent reviewer only for unusual risk, cross-subsystem work,
specialist needs, or an explicit request. Reviewers are read-only: no edits,
commits, ref changes, or destructive Git commands. Record HEAD and tree state
before review and verify both afterward. Astra still audits the final diff and
test evidence; additional reviewers need distinct scopes.

Batch independent tool calls in one `functions.exec` using
`await Promise.allSettled([...])` and inspect every result. Keep dependencies,
mutations, approvals, waits, and adaptive investigations sequential.

For failures, capture the exact command, input, and diagnostic; reproduce and
trace the cause before fixing it. Separate environment failures from product
failures. Add regression coverage of the failed contract and relevant boundaries.
Finish after implementation, diff review, and applicable checks; report blocked
checks precisely and keep unrelated findings separate.

## Project and conventions

Sinistra is a C17 MUD engine: `scomp` compiles source to object/bytecode data,
`sin` runs the VM, networking, and persistence, `sdiss` disassembles objects,
and `sconv` migrates older itemstores and their code items.

See `docs/internals/architecture.md` for module ownership and entry points;
update it when source files are added, removed, or relocated. Main modules are
`src/{common,bytecode,compiler,runtime,itemstore,libcall,net}/`. The public build
interface is `Makefile`, implemented in `mk/{build,tests,fuzz}.mk`.

- Follow nearby C style, naming, error handling, and ownership. For new or
  substantially rewritten C, use `-br -ce -slc -nut -i2 -brf -npcs -npsl`.
- Avoid unrelated cleanup or formatting. Treat warnings as defects, check
  narrowing, prefer `size_t` for sizes where APIs permit, initialize structs
  deliberately, and release resources on every error path.
- Carry language changes through lexer/parser, AST/semantics, IR/lowering,
  emission, verification, runtime, and disassembly as applicable. Include
  validator/schema updates, positive and negative tests, and golden output.
- Preserve on-disk compatibility unless explicitly changing formats. Format
  changes need documentation and encoding/header/verifier tests. Validate
  untrusted objects; test malformed and boundary decode/load cases. Make cache
  invalidation, registry ownership, and cleanup explicit.
- Follow libcall registry patterns; test arguments, conversions, truthiness,
  ownership, returns, side effects, and errors. Document behavior in the relevant
  `docs/reference/` library page; change `libcalls.md` for shared policy/index only.
- Keep event-loop paths nonblocking unless an established API requires otherwise.
  Make libuv handle, callback, buffer, and close ownership explicit; test network
  changes and follow `src/net/AGENTS.md`.
- Follow `tests/inventory/README.md` for contract/catalog changes and
  `tests/fixtures/README.md` for minimal, deterministic fixtures. Update inputs
  and expected output together. Update documentation for public behavior,
  commands, formats, architecture, and contributor workflow changes.

## Build and validation

Use Make rather than ad hoc compilation except for narrow diagnosis. Dependencies
are a C17 compiler, make, pkg-config, libuv development files, Bison, Flex, ar,
and xxd; report missing dependencies instead of vendoring them. `make` builds
debug executables; `BUILD=release` and `BUILD=sanitize` select other profiles.
Use `make help` for targets and variables (`CC`, `CSTD`, `BUILD`, `PKG_CONFIG`,
`LIBUV_PC`, `TEST_JOBS`).

Every behavior change needs meaningful coverage. Start narrow, then use:

| Change | Final validation |
| --- | --- |
| Documentation only | Inspect Markdown; build only for generated examples |
| Build system | `make clean`, `make`, `make test` |
| Core C logic or networking | `make test`, including relevant network tests |
| Compiler/parser/language | Targeted golden tests and `./ci/gate_sanitizers_fuzz.sh` |
| Runtime/bytecode/itemstore loading | `./ci/gate_sanitizers_fuzz.sh` |
| Other itemstore logic | `make test`, applicable sanitizers and fuzz smoke tests |
| Fuzz harness | Seeded `make test-fuzz` |
| Coverage-floor implications | `make test-full`; see `docs/internals/testing/workflow.md` |

The combined sanitizer/fuzz gate includes debug, release, ASan/UBSan with leak
detection, and seeded fuzz tests. `make test-full` also includes coverage. Run
commands containing `make test-sanitize`, either combined gate, or equivalent
leak checks outside a ptrace-restricted sandbox on the first attempt.

Successful checks remain valid while code, build inputs, fixtures, and
configuration are unchanged; review and handoffs do not invalidate them.
Do not duplicate combined-gate components unless needed for diagnosis or an
incomplete/unreliable run. After corrections, rerun affected checks and remaining
gates; explain repeats. Never weaken floors/catalogs, regenerate reviewed
snapshots, or remove failing tests merely to pass. Tune fuzzing with `FUZZ_RUNS`,
`FUZZ_TIME`, and `FUZZ_SEED`; use `make bench` when measurements matter.

## Git and reporting

Check `git status --short` before editing and committing; preserve user changes.
Use `rg` for targeted searches. Keep commits focused. Never commit `obj/`, `lib/`,
executables, test/fuzz binaries, temporary fixtures, or generated parser/lexer
files. Canonical parser sources are `src/compiler/parser.y` and `lexer.l`;
generated output belongs in `obj/<build>-<compiler>/generated/`.

Report a concise summary with changed-file references, documentation/fixture
changes, every validation command marked ✅/⚠️/❌, and omitted checks with reasons.
Claim commits or PRs only after they exist.
