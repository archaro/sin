# CLAUDE.md

Stable orientation for Sinistra. Read this once; it should remove the need to
re-derive repository structure, invariants, and gates from source on every task.

## Authority and precedence

1. User, system, and developer instructions.
2. `AGENTS.md` (repo root) — **authoritative** for process: multi-agent policy,
   delegation, review scope, Git hygiene, and reporting format.
3. `src/net/AGENTS.md` — overrides the root file for the `src/net/` subtree.
4. This file — repository *knowledge*: architecture, invariants, ABI/wire
   contracts, limits, and change recipes.

This file deliberately does **not** restate `AGENTS.md` process rules. When
process and knowledge appear to conflict, `AGENTS.md` wins and this file is the
thing to correct.

## What Sinistra is

A C17 MUD engine: Sinistra source → custom bytecode → stack VM, with a
persistent item tree and libuv/Telnet networking. The runtime can invoke the
compiler at runtime (`sys.compile`), so a running world can rewrite itself.

Four executables (`Makefile: PROGRAMS`):

| Binary  | Entry point    | Role |
|---|---|---|
| `scomp` | `src/scomp.c`  | compiles bootstrap source → bootstrap object |
| `sin`   | `src/sin.c`    | runtime: load itemstore, run bootstrap object, event loop |
| `sdiss` | `src/sdiss.c`  | disassemble bootstrap object |
| `sconv` | `src/sconv.c`  | migrate itemstores from earlier on-disk versions to the latest |

Shared implementation links as `lib/<build-tag>/libsinshared.a`.

`sconv`'s remit is general: convert any earlier itemstore version to the current
one, migrating the code items inside. Today that means v1 → v2, and it grows as
the format evolves. Write about it in those terms — not as a v1-to-v2 tool.

## Source of truth map

Never hand-maintain a fact that lives in one of these. Read the source file;
update it and its dependents together.

| Domain | Source of truth | Human reference |
|---|---|---|
| IR opcode metadata | `src/compiler/ir/opcode_schema.def` | `docs/bytecode.md` |
| Libcall registry/ABI | `src/libcall/libcall_list.h` | `docs/libcalls.md` |
| Bytecode v1 header | `src/bytecode/bytecode_format.h` | `docs/bytecode.md` |
| Itemstore wire format | `src/itemstore/item_persist_v2.c` | `docs/itemstore-format.md` |
| String limit | `src/common/string_limits.h` | — |
| List limits | `src/runtime/list.h` | `docs/lists.md` |
| Item path limits | `src/itemstore/item.h` | `docs/concepts.md` |
| Stack/callstack depth | `src/runtime/stack.h`, `src/runtime/vm.h` | `docs/runtime.md` |
| Error numbers | `src/common/error.h` | `docs/troubleshooting.md` |
| Grammar/lexis | `src/compiler/parser.y`, `src/compiler/lexer.l` | `docs/language-reference.md` |
| Engine version | `src/version.h` (`SINVERSION`) | — |
| Module ownership | `docs/architecture.md` | — |
| Build/test interface | `Makefile` (`make help`) | `CONTRIBUTING.md` |
| Fixture policy | `tests/shared/test_fixture_policy.c` | `tests/fixtures/README.md` |

`docs/language-reference.md` is **normative** where prose documents disagree.

## Repository map

```
src/common/     log memory error util cli_io floatconv strbuilder string_limits
src/compiler/   parser.y lexer.l absyn semant ir lower emitbc compdiag
                compiler_context compiler_pipeline  ir/opcode_schema.def
src/bytecode/   bytecode_format verify convert wire  sdiss_core
src/runtime/    interpret vm stack value list itemref task
                runtime_{context,decode,value,item_ops,opcode}
src/itemstore/  item.h (public API) item_{hash,tree,registry,error}
                item_persist{,_v1,_v2,_internal} item_source_persist
src/libcall/    libcall_list.h (ABI) libcall_table libcall_registry
                libcall_{sys,task,net,str,list} libcall_common.h
src/net/        network.{c,h}  libtelnet.{c,h} (vendored libtelnet 0.23)
tests/          core/ compiler/ interpreter/ network/ shared/ fixtures/ fuzz/
docs/ examples/ ci/
```

There is **no `item.c`**: the itemstore public API is declared in `item.h` and
implemented across the `item_*.c` files, split by concern.

Generated parser/lexer output goes to `obj/<build-tag>/generated/` and is never
committed. `obj/`, `lib/`, the four binaries, test/fuzz binaries, and temporary
fixtures are all build output.

### Dependency direction

`common` → `bytecode`/`itemstore` → `compiler` and `runtime`. Compiler and
runtime are **peers** sharing bytecode, values, diagnostics, and items.
Runtime, libcalls, and networking form a service boundary, not a chain:

- Runtime owns execution and hands `RuntimeContext` to libcall handlers.
- Libcalls may reach runtime, itemstore, network, and the compiler pipeline.
- Networking calls back into the runtime input path from the event-loop thread.
- CLI entry points orchestrate everything; nothing library-side depends on them.

Deliberate, documented crossings only. `sys.compile` (libcall → compiler
pipeline) is the canonical example.

Keep leaf modules (`common`, bytecode helpers, itemstore primitives) free of
tool and network dependencies.

## Build and validation

Use the Makefile, not ad hoc compiler invocations. Toolchain: `make`, C17
compiler, `pkg-config`, libuv dev, Bison, Flex, `ar`, `xxd`. Never vendor a
missing dependency — report the failing command and error.

```
make                      # debug build of all four binaries (default)
make BUILD=release        # -O2 -DNDEBUG
make BUILD=sanitize       # ASan/UBSan
make clean ; make help
```

Variables: `CC` `CSTD` `BUILD` `STRICT_WARNINGS` `PKG_CONFIG` `LIBUV_PC`
`FUZZ_CC` `FUZZ_RUNS` `FUZZ_TIME` `FUZZ_SEED` `FUZZ_ARTIFACT_DIR` `XXD`.

Gates, narrowest first — broaden with risk:

| Command | Covers |
|---|---|
| `make help` | Lists the available build targets |
| `make test` | **normal deterministic gate**: core + compiler + runtime + network + chat smoke |
| `make test-warnings` | clean rebuild, `STRICT_WARNINGS=1` |
| `make test-release` | `BUILD=release` + strict warnings |
| `make test-asan` | sanitize build, leaks **off** (ptrace-restricted fallback) |
| `make test-lsan` | sanitize build, leaks **on** — run outside a ptrace-restricted sandbox |
| `./ci/gate_sanitizers_fuzz.sh` | warnings → release → lsan → seeded fuzz smoke |
| `make test-network` / `make test-chat-smoke` | networking only / end-to-end localhost chat |
| `make fuzz-smoke` | build + run `scomp`, `sdiss`, `sin-object` harnesses |

`make test-strict` (`SIN_STRICT_BENCH=1`) only adds benchmark-budget assertions;
it is not the normal gate. `make test-benchmark` runs the opt-in extended matrix
(`SIN_EXTENDED_BENCH=1 SIN_BENCH_REPORT=1`) in an optimized build.

Validation by change type:

- Docs only → inspect Markdown; no build.
- Build system → `make clean && make && make test`.
- Core C logic → `make test`, consider `make test-warnings`.
- Compiler/parser/language → `make test`, `make test-release`, targeted golden
  tests, plus the sanitizer/fuzz gate.
- Runtime/bytecode/itemstore → `make test`, applicable sanitizers, relevant fuzz
  smoke.
- Fuzz harness → that harness plus a seeded run, or `make fuzz-smoke`.

Every behavior change needs matching unit, integration, golden, benchmark, or
fuzz coverage. Never delete a failing test to make a suite pass.

## Frozen contracts — treat as immutable

### Bytecode v1 opcode ABI is frozen

Encoded bytes, operand layouts, context validity, stack effects, and
control/termination classes are permanent. Unassigned bytes are reserved;
retiring an operation leaves its byte reserved forever. **Any new or changed
instruction requires a new bytecode version, not an edit to v1.**

Code block header (8 bytes, `BC_V1_HEADER_SIZE`), execution starts at offset 8:

```
0: 0x00  1: 0xff        reserved; invalid as legacy locals/params
2..3: 'S''B' magic      4..5: u16 LE version (1)
6: u8 locals (incl. params)   7: u8 params
```

Encoding is portable: fixed-width **little-endian**, two's complement,
IEEE-754 binary64 payload bits copied verbatim (signed zero, infinity, NaN
payloads preserved — never numerically converted during I/O). Jump offsets are
`i16` measured from the start of the two-byte offset field.

`F` is shared by `IR_OP_ITEM_DEREF` (emitter writes arity 0) and `IR_OP_CALL`
(u16 arity). Both run the same fetch-or-call primitive. Keep schema, emitter,
interpreter, verifier, and `docs/bytecode.md` aligned when touching it.

### Libcall pair ABI is permanent

`(library index, call index)` from `src/libcall/libcall_list.h`, emitted after
opcode `M`. Library indices: **sys=1, task=2, net=3, str=4, list=5**. Never
renumber, never reuse a retired pair; unknown pairs are invalid bytecode.
Handlers are named `lc_<library>_<name>`.

### Itemstore v2 is the on-disk format

Header: magic `53 49 4e 49 54 45 4d 00` (`SINITEM\0`) + `u16` LE version `2`,
then exactly one recursive root record; trailing bytes make the file invalid.
The runtime reader accepts **only** v2; v1 decoding survives solely as the
`sconv` migration path and never auto-converts. Item-kind tags: 1 value, 2 code.
Value tags: 0 int, 1 float, 2 string, 3 nil, 4 bool, 5 list, 6 itemref.

Preserve on-disk compatibility unless the task explicitly changes the format.
A format change requires documentation plus encoding, header, and verifier tests.

## Limits (all normative)

| Limit | Value | Constant / enforcement |
|---|---:|---|
| String / code-body payload | 65,535 | `SIN_MAX_STRING_BYTES` |
| Item layer name | 32 bytes | `ITEM_MAX_LAYER_NAME_LENGTH` |
| Item depth (non-root layers) | 8 | `ITEM_MAX_DEPTH` |
| Full non-root path | 263 bytes | `ITEM_MAX_FULL_NAME_LENGTH` |
| Locals (incl. params) / params | 255 / 255 | semantic analysis + `u8` header |
| Item-call arity | 65,535 | `u16` |
| Literal item-layer length | 255 | `u8` |
| Branch displacement | ±32,767 | `i16` |
| Item-expression nesting | 8 | `BC_MAX_ITEM_EXPRESSION_DEPTH` |
| Emitted block length | 4,294,967,295 | `u32` |
| Verifier operand stack | 1024 | static verification |
| Runtime value stack / call stack | 1024 / 1024 | `STACK_SIZE`, `CALLSTACK_SIZE` |
| List elements / nesting | 1,048,576 / 64 | `SIN_LIST_MAX_ELEMENTS`, `SIN_LIST_MAX_DEPTH` |
| V2 aggregate list budget | 1,048,576 | save/load stream check |
| Persisted children per item | 250 | itemstore save/load |
| Code payload | 64 MiB | itemstore save/load |
| Default listener port / max conns | 4001 / 50 | `LISTENER_PORT`, `MAXCONNS` |

**Atomicity rule:** compilation and loading either produce a complete result or
fail with no partial result exposed. Mutation APIs check limits *before*
changing topology or payload. Functional list operations never mutate inputs.

## Semantics that bite

Get these wrong and tests fail in non-obvious ways.

- **Seven value types**: nil, bool, int64, binary64 float, byte string, item
  reference, list.
- **Truthiness**: `0`, `±0.0`, `""`, empty list, `nil` are false. NaN and
  infinities are **true**. Every item reference is true, even dangling.
- **`+` treats `nil` as integer 0** — and only `+`. `nil + float` is invalid.
- **Invalid `/` splits**: no float operand → integer `0`; with a float operand →
  `nil`. Integer `x/0` is `0`; integer `x%0` is `nil`.
- **Checked overflow** on int `+ - * /` and unary `-` pushes `nil` (so
  `INT64_MIN / -1` and `-INT64_MIN` are `nil`). `INT64_MIN % -1` is `0`.
- **`and`/`or` short-circuit and return canonical booleans**, not either operand.
- **Only `return expression;` produces a code-item value.** `return;`,
  fallthrough, and `HALT` yield `nil`. Every expression statement compiles to
  `DISCARD`; a residual stack value never becomes an implicit result.
- **Tolerant call contract by design**: excess arguments are dropped, missing
  trailing parameters are `nil`, value-item targets are cloned rather than
  executed (arguments evaluated then discarded), missing/invalid targets return
  `nil`. This is what lets a live world be updated. `--strict-runtime-contracts`
  reports `ERR_RUNTIME_INVALIDARGS` for these **without changing stack effects
  or return values**.
- **Duplicate parameter names share one slot**, ordered by first occurrence:
  `code {@a, @a, @b}` has two slots.
- **Evaluation order**: arguments and list elements left-to-right, arguments
  before the call target, item path before the assigned value.
- **Item references are weak**: they store a canonical root-relative path,
  resolve afresh on every use, compare by path (never pointer), and stay truthy
  when dangling.
- **Lists are immutable values**; internal structural sharing is unobservable.
- **Integer layer names canonicalize**: `foo.1` and `foo.[@i]` with `@i = 1` are
  the same layer. Floats are never valid layer values.
- **Child ordering is not stable across runtime restarts.**
- `--strict-validation` (bytecode structure) and `--strict-runtime-contracts`
  (argument contracts) are independent; neither implies the other.

### Ownership rules

- `interpret()` returns `VALUE_t` by value; the **caller** owns any string
  payload. It borrows and mutates the shared VM stack/callstack — nested calls
  save/restore decoder and current-item state only.
- Libcall handlers use the opcode-handler ABI
  `uint8_t *(RuntimeContext *, uint8_t *nextop, ITEM_t *)`. Each must consume
  exactly its registered arity and leave **exactly one** value on the stack.
  Popped arguments become handler-owned — free string payloads unless ownership
  transfers to the itemstore or back to Sinistra code. Return `nextop` for
  normal completion *including* Sinistra-visible failure (`nil`/`false`);
  `NULL` is reserved for fatal aborts.
- Use `lc_invalid_args_return` / `lc_invalid_args_detail_return` from
  `libcall_common.h` so `error` and the stack result stay consistent.
- `ITEM_t *` pointers are **borrowed** and die with their store (or with
  deletion of the item/an ancestor). Mutation transfers payload ownership only
  on `CREATED`/`REPLACED`; every failure leaves input caller-owned.
- Each active interpreter frame holds one transient execution **pin**. Any
  nonzero pin blocks replacement; deletion checks the whole target subtree.
  Pins are not serialized and are not an ownership claim.
- Topology revisions invalidate lookup-cache entries; payload revisions do not.

### Error item contract

`error`, `error.msg`, `error.item` for runtime failures; compiler diagnostics
additionally populate `error.code/stage/file/line/column/excerpt` and clear
`error.item`. Stable compiler codes are `SIN-<PHASE>-<4 digits>` with phases
`PARSE SEMANT LOWER IR_VALIDATE EMITBC COMPILE IO`.

Numeric codes live in `src/common/error.h` (note the intentional gaps at 2, 6,
7): compiler `1,3,4,5,8,9,10,11`; runtime `20`–`29`
(`SIGUSR1 INVALIDARGS NOSUCHITEM TRUNCATED INVLIB BYTECODE INVALIDITEM INTERNAL
PERSISTENCE SOURCE`); network `30`.

**Success does not universally clear a prior error** — the per-libcall contract
in `docs/libcalls.md` decides.

## Change recipes

Keep every stage in the set coherent, in one change.

**New/changed opcode**
`opcode_schema.def` (one `OP(...)` row) → `ir.c` materialization → `emitbc.c`
encoding/size/validator → `bytecode_verify.c` → `runtime_decode.c` /
`runtime_opcode.c` / `interpret.c` → `sdiss_core.c` → `docs/bytecode.md` →
positive **and** negative tests + golden fixtures.
Custom payload writing belongs in `emitbc.c` only when the schema policy needs
variable-length handling. Remember: v1 is frozen — a semantic change needs a new
version.

**New libcall**
Add one row to `libcall_list.h` with a fresh permanent pair → implement
`lc_<lib>_<name>` in the matching `libcall_*.c` → cover it in
`tests/core/test_libcall_<lib>.c` (generic registry contracts stay in
`test_libcall_registry.c`) → document in `docs/libcalls.md`. Test argument
validation, truthiness/conversion, ownership, returns, side effects, and error
behavior.

**Language/grammar change**
`lexer.l` / `parser.y` → `absyn.*` → `semant.*` → `ir.*`, `lower.*` →
`emitbc.*` → `bytecode_verify.*` → runtime/disassembly → `docs/language-reference.md`
(normative) and `docs/concepts.md` → conformance fixtures under
`tests/fixtures/conformance/` (negative fixtures hold exactly one rejection
each and are driven by `test_pipeline_negative_matrix`).

**Persistence change**
`item_persist*.c` → `docs/itemstore-format.md` → encoding, header, and verifier
tests → malformed/boundary fixtures under `tests/fixtures/itemstore/` → consider
`sconv` migration and `make fuzz-sin-object`. Validate untrusted object data
before runtime use.

**Networking change**
Policy and libuv integration in `network.c`; public `net.*` behavior in
`libcall_net.c`. Do not block event-loop paths. Telnet negotiation is stateful
(RFC 1143) — test transitions, not just emitted bytes. `libtelnet.[ch]` is
vendored: preserve its style, change it only for genuine protocol/API/portability
needs. Gate with `make test-network`, `tests/core/test_libcall_net.c`, and
`make test-chat-smoke`.

**Fixtures**
Keep them minimal, deterministic, inspectable. Follow `tests/fixtures/README.md`
and update input *and* expected output together. Declared fixtures need `SOT:`
and `regen:` metadata in `tests/shared/test_fixture_policy.c`.

**Test placement**
`tests/core/` low-level, itemstore, task, stack, value, libcall (incl.
`test_libcall_sys_compile.c`) · `tests/compiler/` compiler and disassembler
(incl. `test_bytecode_v1_abi.c`, the independent frozen-ABI oracle) ·
`tests/interpreter/` semantics, stress, benchmark · `tests/network/` the two
network harnesses · `tests/shared/` harness and fixture policy.

## C conventions

- C17. Match nearby naming, indentation, error handling, and ownership style —
  the codebase intentionally mixes older and newer styles. Do not do broad
  cleanup during feature work.
- New or substantially rewritten C: `indent -br -ce -slc -nut -i2 -brf -npcs -npsl`.
- Warnings are defects. The default build carries `-Wall -Wextra -Wpedantic
  -Wconversion -Wsign-conversion`; avoid unchecked narrowing.
- Prefer `size_t` for sizes/indices while respecting existing APIs.
- Initialize structs deliberately; release resources on **every** error path;
  make ownership explicit where allocations are introduced.
- Use the overflow-checked helpers in `src/common/memory.h`
  (`alloc_mul_overflow`, `alloc_grow_array_capacity`, …) rather than open-coded
  arithmetic. `alloc_test_fail_after()` exists for allocation-failure tests.
- Prefer focused functions and explicit control flow over clever macros.
- Search with `rg` / `rg --files` / `find`; not `ls -R` or `grep -R`.

## Release state

Tags: `0.6.1 … 0.6.3 → 0.7.0 → 0.7.1 → 0.7.2`. **0.7.2 is the current tag and
the current GitHub release** (`archaro/sin`), and `src/version.h` matches it.
Work already committed to `main` past that tag becomes **0.7.3**; a **0.7.4** may
follow before the freeze.

**0.8.0 is the language and bytecode stabilisation release.** Compatibility is
not frozen before it — yet the v1 opcode ABI, the libcall pair ABI, and itemstore
v2 are *already* frozen ahead of it (see *Frozen contracts*). Treat those three
as immutable today regardless of the 0.8.0 wording in `docs/`.

The docs label themselves 0.7.3 (`docs/language-reference.md`, `docs/README.md`,
`docs/concepts.md`, `docs/documentation-roadmap.md`). That is deliberate
forward-labelling for the in-progress release, **not** drift — `src/version.h`
tracks the *released* version and bumps at release time. `sys.version` returns
`SINVERSION`. Do not "reconcile" the two.

`pre-0.8.0-plan.md` (untracked, repo root) is the pre-freeze review. Its items
1–5 — versioned bytecode header, portable wire encoding, stable libcall IDs,
frozen opcode ABI, `sconv` bytecode migration — are **complete**; the git history
shows the commits. What remains is the language freeze itself and conformance
breadth, and work moves past this plan shortly. Do not re-implement items 1–5.

## Anti-patterns

- Editing `docs/bytecode.md` or `docs/libcalls.md` without changing their source
  of truth, or the reverse.
- Adding a libcall by position rather than by permanent pair.
- Indexing the first two bytes of a code block instead of using
  `bc_decode_header()`.
- Assuming host byte order or `sizeof` in any wire path.
- Letting a libcall handler leave zero or two values on the stack.
- Returning `NULL` from a handler for an ordinary Sinistra-visible failure.
- Committing `obj/`, `lib/`, binaries, generated parser/lexer output, or
  `tests/fuzz/artifacts/`.
- Running `make test-lsan` first inside a ptrace-restricted sandbox — LSan
  cannot work there; that is what `make test-asan` is for.
- Reverting or discarding unrelated working-tree changes; work around a dirty
  tree instead.
