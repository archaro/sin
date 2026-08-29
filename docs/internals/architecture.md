# Architecture and Module Map

This document describes the current module boundaries in Sinistra and the
intended dependency direction. It is intended to explain why things have been
arranged as they have.  It shouldn't be considered a TODO list.

For the broader internals map, start at the [internals index](README.md).
Conceptual boundaries are also covered by the [compiler pipeline](compiler.md),
[itemstore operations](itemstore.md), and [event-loop lifecycle](event-loop.md);
this page is the module/dependency map rather than the sole navigation point.

## Source Organization

Top-level `src/` holds the CLI entry points and integration headers, with
reusable implementation grouped under `src/common/`, `src/compiler/`,
`src/runtime/`, `src/itemstore/`, `src/bytecode/`, `src/libcall/`, and
`src/net/`. Generated parser and lexer output is written to the active
`obj/<build>-<compiler>/generated/` directory by the build.

The main command entry points are:

- `src/scomp.c`: compiler CLI.
- `src/sdiss.c`: bytecode disassembler CLI.
- `src/sin.c`: runtime/game executable CLI.
- `src/sconv.c`: itemstore conversion CLI.

The shared implementation is built into
`lib/<build>-<compiler>/libsinshared.a` and linked into those tools and the
test harnesses.

## Make implementation

The root `Makefile` is the public build and test interface. Production object,
parser, lexer, and executable rules live in `mk/build.mk`; framework binaries
and deterministic composition live in `mk/tests.mk`; fuzz compilation,
corpus seeding, and campaigns live in `mk/fuzz.mk`. These fragments derive all
outputs from the active build/compiler tag, use normal dependencies, and keep
generated test data out of `tests/`.

## Contract Inventory

The checked-in catalogues under `tests/inventory/` map the language, bytecode,
module APIs, libcalls and executable interfaces to their implementation and
test coverage. `make test` audits them against the source and built archive so
additions, removals and unmapped changes fail explicitly rather than drifting
silently.

See [the test inventory documentation](../../tests/inventory/README.md) for
the catalogue schema and reconciliation rules.

## Test Architecture

Tests use the C17/POSIX framework under `tests/framework/`. Native white-box
test bodies remain grouped by subsystem, while adapters under `tests/rewrite/`
supply framework descriptors and executable ownership. Fixture-driven language
conformance tests live under `tests/conformance/`.

The framework, adapter layout, test metadata, process isolation, and
contributor workflow are documented in the
[testing internals guide](testing/README.md). Contract-to-test relationships
are checked by the inventories under `tests/inventory/`.

## Coverage Gate

`make test-full` collects compiler-native coverage for authored C modules and
checks it against manually reviewed per-module floors. Coverage policy,
compiler-specific baselines, and exceptional cases are documented in the
[testing workflow](testing/workflow.md).

## Module Boundaries

### Common Support

Files: `src/common/log.*`, `src/common/memory.*`, `src/common/error.*`,
`src/common/util.*`, `src/common/cli_io.*`, `src/common/floatconv.*`,
`src/common/string_limits.h`, `src/common/strbuilder.h`

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

Frame transitions and execution-pin ownership are centralised in
`runtime_frame.[ch]`; interpreter and nested-call paths use that boundary
rather than manipulating VM stack/call-stack state independently.

### Itemstore

Files: `src/itemstore/*`.

Ownership: the live item tree, lookup/cache behaviour, structured errors,
persistence, conversion, and source sidecars. The itemstore owns its tree and
payloads; callers borrow item pointers. It may depend on common support, value
representation, and bytecode verification, but not on the compiler pipeline or
networking.

The public boundary is `item.h`; implementation is split internally by
tree/hash operations, persistence/version codecs, errors, and source-sidecar
handling. See [Itemstore operations](itemstore.md) for ownership, mutation,
revision, and durability invariants and
[Itemstore file format](itemstore-format.md) for the wire format.

### Libcalls

Files: `src/libcall/libcall*.c`, `src/libcall/libcall*.h`, including the
dedicated immutable list handlers in `libcall_list.c`.

Ownership: Sinistra standard library primitives exposed to bytecode. Libcalls
bridge runtime values to host services such as tasks, networking, system
operations, and string helpers. They may depend on runtime context, itemstore,
networking, and compiler pipeline only when the primitive requires it.

The libcall registry owns the permanent `(library index, call index)` ABI
shared by compiler lowering and runtime dispatch. `libcall_lookup_pair()`
resolves compiler-side names, while `libcall_func_pair()` resolves runtime
dispatch. Handlers are named `lc_<library>_<name>`.

### Networking

Files: `src/net/network.*`, `src/net/libtelnet.*`.

`NetworkRuntime` owns connection slots, Telnet state, polling state, and
network buffers while borrowing the `libuv` loop/listener handles from
application startup. Runtime/libcall code interacts through the opaque network
API; transport internals do not cross that boundary. Network callbacks execute
on the runtime event-loop thread, so networking intentionally depends on
runtime execution but not compiler internals.

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
  concurrently.
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
- Add new libcalls through `src/libcall/libcall_list.h`; keep compiler lowering
  and runtime dispatch aligned with the permanent pair ABI, and update the
  corresponding inventory and tests.
- If moving files later, do it as behavior-preserving path/build/include updates
  with no semantic edits in the same change.

## Verification

For normal changes, run:

```sh
make test
```

Architectural, compiler, runtime, or build-system changes should also run the
applicable extended checks described in
[`CONTRIBUTING.md`](../../CONTRIBUTING.md) and the
[testing internals guide](testing/README.md).
