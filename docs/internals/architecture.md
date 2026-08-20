# Architecture and Module Map

This document describes the current module boundaries in Sinistra and the
intended dependency direction. It is a reasoning aid, not a file-move plan.

## Source Organization

Top-level `src/` holds the CLI entry points and integration headers, with
reusable implementation grouped under `src/common/`, `src/compiler/`,
`src/runtime/`, `src/itemstore/`, `src/bytecode/`, `src/libcall/`, and
`src/net/`. Generated parser and lexer output is written to `obj/generated/`
by the build.

The main command entry points are:

- `src/scomp.c`: compiler CLI.
- `src/sdiss.c`: bytecode disassembler CLI.
- `src/sin.c`: runtime/game executable CLI.
- `src/sconv.c`: itemstore conversion CLI.

The shared implementation is built into `lib/libsinshared.a` and linked into
those tools and the test harnesses.

## Contract Inventory

The checked-in catalogs under `tests/inventory/` are the completeness
inventory for the language, compiler/bytecode pipeline, runtime APIs,
libcalls, executable interfaces, and legacy test contracts.  Their canonical
identifiers are reconciled against `parser.y`, `absyn.h`, `bytecode_abi.h`,
`opcode_schema.def`, `libcall_list.h`, and the built shared archive.  Run
`make inventory-audit` after `make lib` (or `make test`, which includes the
audit) to detect missing, stale, duplicate, or unmapped entries.  The focused
positive/negative checks are available with `make inventory-audit-self-test`;
they mutate temporary copies only.  `tests/inventory/archive_symbols.csv`
accounts for every maintained archive global with its object, architectural
module, provenance, and grouped `api_contract_id`; `api.csv` contains the
reviewed observable module contracts (including application entry points),
not fabricated per-symbol prose for private implementation helpers.  The
auditor fails closed on unknown archive objects or contract mappings.
Grammar tokens include `%token` and precedence declarations; opcode inventory
rows reconcile all ten `OP(...)` fields, and libcall rows reconcile the exact
handler symbol in addition to their numeric ABI metadata.

## Test Framework

The self-contained C17/POSIX framework lives under `tests/framework/` and is
kept separate from the legacy unified harness during migration. Each test
translation unit supplies a standard-C `TF_TestDescriptor` array to
`tf_main()`. The framework validates metadata, lists descriptors as `TF|...`
records, and runs one selected descriptor in a fresh process with captured
output, timeout/process-group cleanup, fixture cleanup, and resettable
allocation/itemstore hooks. `test_runner.c` discovers descriptors from each
executable's `--list` output and invokes them through `--run ID`; it runs
serially by default and uses positive `TEST_JOBS` values for non-exclusive
batches. Build artifacts for `make test-framework` remain in the active
variant's `obj/<build>-<compiler>/tests/framework/` directory.
Successful captured output is suppressed by default; setting `TF_VERBOSE=1`
replays it for diagnostics.

Framework records use pipe-delimited UTF-8-safe tokens with this fixed schema:
`TF|LIST|id|tags|timeout_ms|contracts` describes one validated descriptor;
`TF|RESULT|id|status|duration_or_reason|tags` reports one selected test; and
`TF|TOTAL|scope|selected_or_ran|passed|failed` reports an aggregate. IDs, tags,
and contract IDs contain only ASCII letters, digits, `_`, `-`, `.`, and `:`;
comma-separated tags/contracts may not contain empty components. Empty tags
are valid, while every descriptor has at least one contract. Executables return
0 only when all selected tests pass, 1 for test failures (including crashes,
timeouts, and assertion failures), and 2 for usage, discovery, or metadata
errors. The runner similarly returns 0 for an all-pass aggregate, 1 for any
selected failure, and 2 for invalid options, invalid `TEST_JOBS`, discovery,
or record errors. Captured output is replayed only for failures unless
`TF_VERBOSE=1` is set.
The normal self-test translation unit contains only expected-to-pass
descriptors. Deliberately failing/crashing/hanging fixtures live in the
separate `framework-negative-fixture` translation unit and are addressed by
ID through `TF_FRAMEWORK_NEGATIVE`; it is listed and run directly for focused
checks, but is not included in the ordinary all-pass aggregate.

The first migration group is under `tests/rewrite/group1/`. Its adapters keep
legacy native test bodies in their original translation units while each
adapter owns an explicit descriptor array and is linked as a separate binary.
The `SIN_TEST_FRAMEWORK_COMPAT` assertion shim maps legacy assertion names to
framework failures only for these binaries. `make test-framework` discovers
the group alongside framework and conformance binaries; the legacy `make test`
target and its unified harness remain unchanged.

The compiler front-end migration adapters are kept under `tests/rewrite/` and
use the same compatibility boundary. They cover the compiler-owned AST,
semantic, parser, IR/lowering, and pipeline translation units with one
descriptor-owning executable per native source file; the six Group 1 overlap
descriptors remain in their original adapters. Their binaries are aggregated
by `make test-framework` without changing the legacy test target.

The bytecode/disassembly migration adapters are also kept under
`tests/rewrite/`, in `group3_adapter_*.c`. They cover each bytecode-owned
native test translation unit with a separate descriptor-owning executable for
ABI/schema, wire encoding, conversion, emission, verification, and `sdiss`.
The Group 3 binaries are included in `make test-framework` and use only the
active variant's `obj/` output directory; the legacy bytecode gate remains
authoritative during migration.

The runtime migration adapters are kept under `tests/rewrite/` in
`group4_adapter_*.c`. They cover stack/frame transitions, immutable lists,
interpreter execution, stress cleanup, and the opt-in runtime benchmark. The
existing `group1/adapter_value_behavior.c` owns all value-behavior descriptors
for its native translation unit, including the two diagnostic descriptors
reused from Group 1. Each adapter has one active-variant binary and is
aggregated by `make test-framework`; runtime tests preserve fresh-process
isolation and mark global-hook, process, stress, and benchmark cases
exclusive.

The itemstore/persistence migration adapters are kept under
`tests/rewrite/` as `group5_adapter_item_cache.c`,
`group5_adapter_itemstore_io.c`, and
`group5_adapter_sin_itemstore_policy.c`. They own one active-variant binary
per native translation unit for cache topology, persistence versions and
budgets, source sidecars, durability, and the two in-scope `sin` policy
descriptors. The `group1/adapter_sconv.c` binary is extended with the other
seven `sconv` descriptors, so conversion coverage has one executable. Fixed
paths and process/global hooks are tagged `exclusive`; the cache benchmark is
tagged `benchmark,exclusive`. All four binaries are aggregated by
`make test-framework` while the legacy itemstore gate remains authoritative.

The libcall/task migration adapters are kept under `tests/rewrite/` as
`group6_adapter_*.c`; the Group 1 registry and `sys` adapters are extended so
each native translation unit still has exactly one descriptor owner. Their
active-variant binaries cover the complete registry plus `task`, `net`, `str`,
`list`, and `sys.compile` contracts. The `sys_compile` binary links the
framework runner without `framework_config.c` because its native test owns the
intentional `CONFIG_t config` definition. Libcall, task, network, and registry
state is process-local and all such descriptors are tagged `exclusive`.
The conformance manifest explicitly excludes real-language `net.*` cases as
transport-dependent; native stubs are not treated as localhost chat
integration, which belongs to the network migration group.

The network/Telnet/chat migration adapters are
`tests/rewrite/group7_adapter_network.c` and
`tests/rewrite/group7_adapter_chat_smoke.c`. The network adapter directly
includes `tests/network/test_network.c`, preserving its white-box
`CONFIG_t`, allocation/libuv/Telnet stubs, and embedded implementation
sources; its framework rule links only `test_framework.c` plus the archive and
therefore does not introduce `framework_config.c` or duplicate normal network
objects. The chat adapter invokes the unchanged localhost orchestration in an
isolated framework child. Both binaries own one explicit descriptor array,
tag all cases `exclusive,network`, and are aggregated by `make test-framework`
alongside the unchanged dedicated `test-network` and `test-chat-smoke` gates.
Chat uses an ephemeral loopback port, bounded waits, a dedicated server
process group, and teardown assertions for early disconnect, startup failure,
and complete process-group cleanup. Network cases explicitly drain write and
close callbacks and release Telnet/input/output state before destroying their
fixture runtime.

The fixture-driven conformance executable is
`tests/conformance/test_conformance.c`, built under the active variant's
`obj/<build>-<compiler>/tests/conformance/` directory. It consumes the strict,
pipe-delimited `tests/fixtures/conformance/conformance.manifest`, validates
source and expectation references against the language/libcall inventories,
and drives the real `scomp`, `sdiss`, and `sin` executables through
`tf_process_run`. Positive cases therefore cover parse, semantic analysis,
lowering, emission, bytecode verification, persistence/loading, and execution;
negative cases assert compiler rejection and stop before later phases. Runtime
cases use isolated framework fixtures, strict bytecode validation, and explicit
repeat counts for persistence checks. `make test-conformance` is the focused
target; `make test-framework` discovers it alongside the framework self-tests,
while the legacy `make test` harness remains authoritative during migration.

Manifest coverage rows must classify every language and libcall inventory entry
with a checked-in source witness. Explicit exclusion rows document network,
shutdown/abort, clock, persistence, nested compilation, and task-scheduling
facilities that are unsafe or nondeterministic in load-only mode. The validator
also rejects undeclared conformance fixture drift. Normal test execution is
read-only; expectation regeneration is a deliberate manual workflow documented
in `tests/fixtures/conformance/README.md`.

## Module Boundaries

### Common Support

Files: `src/common/log.*`, `src/common/memory.*`, `src/common/error.*`,
`src/common/util.*`, `src/common/cli_io.*`, `src/common/floatconv.*`,
`src/common/string_limits.h`, `src/common/strbuilder.h`, `src/config.h`,
`src/version.h`.

Ownership: process-wide diagnostics, allocation wrappers, CLI helpers, numeric
formatting, and small utilities. These modules should not depend on compiler,
runtime, networking, itemstore, or libcall internals.

### Compiler

Files: `src/compiler/*`, including `src/compiler/parser.y` and
`src/compiler/lexer.l`.

Ownership: parsing, semantic analysis, IR construction/lowering, bytecode
emission, compiler diagnostics, and compiler pipeline orchestration. The
`compiler/source_span.h` value carries non-owning source provenance from AST
through IR and diagnostics without entering emitted bytecode.
The compiler may depend on common support, bytecode definitions/verification, and
item/value data structures where needed. It should not depend on networking or
runtime task scheduling.

Key entry points:

- `compile_source_to_bytecode_diag()` in `src/compiler/compiler_pipeline.h`.
- `emitbc_*` routines for bytecode emission.

### Bytecode and Disassembly

Files: `src/bytecode/bytecode_abi.*`, `src/bytecode/opcode_schema.def`,
`src/bytecode/bytecode_format.*`, `src/bytecode/bytecode_verify.*`,
`src/bytecode/bytecode_convert.*`,
`src/bytecode/bytecode_wire.*`, `src/bytecode/sdiss_core.*`.

Ownership: bytecode validation and bytecode-to-text disassembly. These modules
may depend on common support and shared opcode/runtime metadata, but should not
invoke runtime execution.

Key entry points:

- `bc_verify_executable_bytecode()` from `src/bytecode/bytecode_verify.h`
  performs the mandatory checks for bytecode that will execute.
- `bc_verify_bytecode()` from `src/bytecode/bytecode_verify.h`.
- `bc_convert_latest()` from `src/bytecode/bytecode_convert.h`.
- `sdiss_*` APIs from `src/bytecode/sdiss_core.h`.

### Runtime VM

Files: `src/runtime/interpret.*`, `src/runtime/runtime_*`,
`src/runtime/itemref.*`, `src/runtime/list.*`,
`src/runtime/list_internal.h`,
`src/runtime/vm.*`, `src/runtime/stack.*`, `src/runtime/value.*`,
`src/runtime/task.*`.

Ownership: bytecode decoding/execution, VM stacks, runtime values, persistent
immutable lists, task scheduling, and interpreter context. Runtime code may depend on common support,
bytecode verification, itemstore, libcalls, and networking. Direct compiler
dependencies should be deliberate and documented; `sys.compile` is one such
crossing.

Key entry points:

- `interpret()` in `src/runtime/interpret.h`.
- `runtime_context_init()`, `runtime_init()`, and `runtime_destroy()` via
  `src/runtime/runtime_context.h`.

Frame transitions are owned by `src/runtime/runtime_frame.[ch]`. Its checked
boundary captures invocation checkpoints, reserves and normalizes call
arguments, publishes continuations, acquires and releases one execution pin
per active code frame, places return values, and unwinds failed or interrupted
invocations. `interpret.c` and `libcall_sys.c` use this API rather than
composing raw VM stack/call-stack transitions.

### Itemstore

Files: `src/itemstore/item.h`, `src/itemstore/item_internal.h`,
`src/itemstore/item_persist_internal.h`,
`src/itemstore/item_hash.c`, `src/itemstore/item_tree.c`,
`src/itemstore/item_registry.c`, `src/itemstore/item_persist.c`,
`src/itemstore/item_persist_v1.c`,
`src/itemstore/item_persist_v2.c`,
`src/itemstore/item_source_persist.c`, and `src/itemstore/item_error.c`. There
is no `item.c`; the public API lives in `item.h`, while the implementation is
intentionally split by concern. Shared binary itemstore I/O, header/version
dispatch, loading lifecycle, durability, and publication live in
`item_persist.c`; the narrow persistence interfaces are in
`item_persist_internal.h`; the frozen v1 record/value codec is implemented in
`item_persist_v1.c`; the runtime v2 recursive value/record codec (including
list and item-reference validation) is implemented in `item_persist_v2.c`;
source-sidecar text I/O and its test
hooks live in `item_source_persist.c`.
Child lookup and insertion-order storage are encapsulated by the opaque
container implemented in `item_hash.c`; other itemstore modules use its
internal operations rather than accessing representation fields.

Ownership: item tree data model, persistence, lookup/cache behavior, and
structured error items. Itemstore code may depend on common support, bytecode
verification for persisted code items, and value representation. It should not
depend on compiler pipeline or networking.

Key entry points are `itemstore_create()`, `itemstore_root()`,
`itemstore_destroy()`, `find_item()`, and `item_set_code()` for in-memory
ownership and lookup. Persistence uses `itemstore_save()` and
`itemstore_load()`; roots are borrowed and remain valid only until their
owning store is destroyed. Topology and payload revisions are store-local
transient metadata: topology revisions invalidate lookup-cache entries when
names or tree shape change, while payload revisions track successful
value/code replacements without invalidating cached pointers. Hit/miss
statistics are cumulative per store.

### Libcalls

Files: `src/libcall/libcall*.c`, `src/libcall/libcall*.h`, including the
dedicated immutable list handlers in `libcall_list.c`.

Ownership: Sinistra standard library primitives exposed to bytecode. Libcalls
bridge runtime values to host services such as tasks, networking, system
operations, and string helpers. They may depend on runtime context, itemstore,
networking, and compiler pipeline only when the primitive requires it.

Key entry points:

- `libcall_lookup_pair()` for compiler lowering.

Compiler lowering and runtime dispatch use the permanent `(library index,
call index)` pair ABI; registry APIs are pair-based and do not expose
positional tokens. `libcall_func_pair()` resolves runtime dispatch directly.
- Handler functions named `lc_<library>_<name>`.

### Networking

Files: `src/net/network.*`, `src/net/libtelnet.*`.

Ownership: `NetworkRuntime` is the sole owner of libuv/libtelnet integration,
connection slots, fair-poll state, input queues, and output buffering. Its
loop and listener handle storage are borrowed from application startup and
must outlive the runtime until close callbacks have drained. Destruction
preflights listener and slot transport state; if live state remains, it fails
without mutating the runtime. Runtime and libcall code borrow only the opaque
runtime pointer and use polling, write, flush, echo, disconnect, connected, and
copied-address operations; transport records and telnet state never cross that
boundary. The `input_processor()` callback also runs the configured input code
item and flushes network output, so this boundary depends on the runtime
interpreter but not on compiler internals.

### Application Entry Points

Files: `src/scomp.c`, `src/sdiss.c`, `src/sin.c`, `src/sconv.c`, `src/config.h`,
`src/version.h`.

Ownership: CLI argument handling, program startup/shutdown, version constants,
process-wide configuration wiring, and the serialized runtime input scheduler.
`sin` stages the input timer after listener setup and closes it through the
centralized startup cleanup path. Entry points are allowed to depend on the
library modules they orchestrate. Library modules should not depend on CLI entry
points.

## Concurrency and Thread Confinement

The executables and shared APIs currently assume single-threaded use. They do
not provide synchronization for their mutable state: each process must run its
compiler calls and shared-library calls in one serialized flow. Runtime,
itemstore, task, and network mutation belongs on the owning libuv event-loop
thread; callbacks that enter those services must remain on that thread.

This contract also covers the less visible state used by those APIs:

- `RuntimeContext` and its VM, itemstores, and tasks are thread-confined
  objects. Separate contexts or separate itemstores do not make concurrent
  calls supported.
- Libcall registries, including the process-default registry and registries
  initialized lazily, are unsynchronized. Registry initialization, lookup,
  destruction, and test reset must be serialized with users of the registry.
- Runtime strings and values, including the process-local string tracking
  metadata used by runtime string helpers, must not be mutated or reclaimed
  concurrently. The tracker is a private pointer-keyed open-addressing hash
  table with bounded load, tombstone cleanup, and an empty baseline after its
  last entry is released; recording metadata may fail without affecting string
  ownership. Test-only probes in `runtime_value.h` measure hash probe work
  without making that metadata part of the public `VALUE_t` API.
- Process-global allocation-failure hooks, itemstore persistence and
  source-sidecar test hooks, and the `sys.backup` test timestamp hook must be
  installed, reset, and observed only while the process is quiescent. Tests
  using them must run in a serial flow.

These are concurrency requirements, not optional caller guidance; no locks,
thread ownership checks, or context/store isolation are implied by the APIs.

## Intended Dependency Direction

Most low-level dependencies flow from common support into bytecode/itemstore,
then into the compiler and runtime. The runtime, libcalls, and networking form
an intentional service boundary rather than a strict dependency chain:

- The runtime owns execution and provides the `RuntimeContext` used by libcall
  handlers.
- Libcalls use runtime values and context, itemstore operations, networking, and
  the compiler pipeline when a primitive requires them.
- Networking invokes the runtime input callback from the event-loop thread.
- Application entry points orchestrate these services and own startup/shutdown.

The compiler and runtime are peers that share bytecode, values, diagnostics, and
item data. Crossings between them should be explicit. Examples:

- `sys.compile` intentionally calls the compiler pipeline from a runtime libcall.
- Runtime bytecode execution relies on libcall registry metadata generated from
  `src/libcall/libcall_list.h`.

## Boundary Rules for Changes

- Keep leaf modules (`common`, bytecode helpers, itemstore primitives) free of
  tool and network dependencies.
- Add new libcalls through `src/libcall/libcall_list.h`, implement the
  handler, and add runtime coverage in the matching `tests/core/test_libcall_*.c`
  file: registry/generic contracts remain in `test_libcall_registry.c`, with
  `sys`, `task`, `net`, `str`, and `list` handlers in their respective files.
- Keep low-level, itemstore, task, stack, value, and libcall tests under
  `tests/core/` (including `test_libcall_sys_compile.c`, the runtime
  integration coverage for `sys.compile`); compiler and disassembler tests under
  `tests/compiler/` (including `test_bytecode_v1_abi.c`, the independent frozen
  opcode ABI oracle); and
  interpreter semantic/stress/benchmark tests under `tests/interpreter/`.
- The dedicated network harnesses live under `tests/network/`: one uses local
  libuv/libtelnet stubs and the other runs the chat example over localhost.
  Shared harness and fixture-policy code lives under `tests/shared/`; the
  private libcall runtime fixture support is `test_libcall_support.[ch]`, with
  opaque network setup helpers declared in `test_network_fixture.h`. Golden
  inputs and outputs under `tests/fixtures/`, and fuzz harnesses/corpora under
  `tests/fuzz/`.
- If moving files later, do it as behavior-preserving path/build/include updates
  with no semantic edits in the same change.

## Verification

For normal changes, run:

```sh
make test
```

For module-boundary, compiler, runtime, or build-system changes, also run the
relevant checks documented in [`CONTRIBUTING.md`](../../CONTRIBUTING.md). The
hosted workflow keeps the warning, release, leak-sanitizer, and fuzz checks in
separate parallel jobs; the combined local entry point is:

```sh
make test-warnings
make test-release
make test-lsan
FUZZ_SEED=1 FUZZ_ARTIFACT_DIR="$PWD/tests/fuzz/artifacts" make fuzz-smoke
```
The sin-object fuzz harness exercises v2-only runtime loading, strict raw bytecode verification, and shared `itemstore_convert` version dispatch. Source-level list/item-reference persistence integration is owned by interpreter golden fixtures under `tests/fixtures/interpret/`.
