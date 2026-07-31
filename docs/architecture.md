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

The shared implementation is built into `lib/libsinshared.a` and linked into
those tools and the test harnesses.

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
compiler may depend on common support, bytecode definitions/verification, and
item/value data structures where needed. It should not depend on networking or
runtime task scheduling.

Key entry points:

- `compile_source_to_bytecode_diag()` in `src/compiler/compiler_pipeline.h`.
- `emitbc_*` routines for bytecode emission.

### Bytecode and Disassembly

Files: `src/bytecode/bytecode_verify.*`, `src/bytecode/sdiss_core.*`.

Ownership: bytecode validation and bytecode-to-text disassembly. These modules
may depend on common support and shared opcode/runtime metadata, but should not
invoke runtime execution.

Key entry points:

- `bc_verify_bytecode()` from `src/bytecode/bytecode_verify.h`.
- `sdiss_*` APIs from `src/bytecode/sdiss_core.h`.

### Runtime VM

Files: `src/runtime/interpret.*`, `src/runtime/runtime_*`,
`src/runtime/itemref.*`, `src/runtime/list.*`,
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

- `libcall_lookup_token()` for compiler lowering.
- `libcall_func_token()` for runtime dispatch.
- Handler functions named `lc_<library>_<name>`.

### Networking

Files: `src/net/network.*`, `src/net/libtelnet.*`.

Ownership: libuv/libtelnet integration, connection state, input queues, and
output buffering. The `input_processor()` callback also runs the configured
input code item and flushes network output, so this boundary depends on the
runtime interpreter but not on compiler internals.

### Application Entry Points

Files: `src/scomp.c`, `src/sdiss.c`, `src/sin.c`, `src/config.h`,
`src/version.h`.

Ownership: CLI argument handling, program startup/shutdown, version constants,
process-wide configuration wiring, and the serialized runtime input scheduler.
`sin` stages the input timer after listener setup and closes it through the
centralized startup cleanup path. Entry points are allowed to depend on the
library modules they orchestrate. Library modules should not depend on CLI entry
points.

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
  integration coverage for `sys.compile`); compiler and disassembler tests under `tests/compiler/`; and
  interpreter semantic/stress/benchmark tests under `tests/interpreter/`.
- The dedicated network harnesses live under `tests/network/`: one uses local
  libuv/libtelnet stubs and the other runs the chat example over localhost.
  Shared harness and fixture-policy code lives under `tests/shared/`; the
  private libcall runtime fixture support is `test_libcall_support.[ch]`. Golden
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
