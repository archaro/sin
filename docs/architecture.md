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
relevant checks documented in [`CONTRIBUTING.md`](../CONTRIBUTING.md). The
hosted workflow keeps the warning, release, leak-sanitizer, and fuzz checks in
separate parallel jobs; the combined local entry point is:

```sh
make test-warnings
make test-release
make test-lsan
FUZZ_SEED=1 FUZZ_ARTIFACT_DIR="$PWD/tests/fuzz/artifacts" make fuzz-smoke
```
The sin-object fuzz harness exercises v2-only runtime loading, strict raw bytecode verification, and shared `itemstore_convert` version dispatch. Source-level list/item-reference persistence integration is owned by interpreter golden fixtures under `tests/fixtures/interpret/`.
