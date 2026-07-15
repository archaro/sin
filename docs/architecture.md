# Architecture and Module Map

This document describes the current module boundaries in Sinistra and the
intended dependency direction. It is a reasoning aid, not a file-move plan.

## Current Source Layout

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

- `verify_bytecode()` from `src/bytecode/bytecode_verify.h`.
- `sdiss_*` APIs from `src/bytecode/sdiss_core.h`.

### Runtime VM

Files: `src/runtime/interpret.*`, `src/runtime/runtime_*`,
`src/runtime/vm.*`, `src/runtime/stack.*`, `src/runtime/value.*`,
`src/runtime/task.*`.

Ownership: bytecode decoding/execution, VM stacks, runtime values, task
scheduling, and interpreter context. Runtime code may depend on common support,
bytecode verification, itemstore, libcalls, and networking. Direct compiler
dependencies should be deliberate and documented; `sys.compile` is one such
crossing.

Key entry points:

- `interpret()` in `src/runtime/interpret.h`.
- `runtime_context_init()`, `runtime_init()`, and `runtime_destroy()` via
  `src/runtime/runtime_context.h`.

### Itemstore

Files: `src/itemstore/item.*`, `src/itemstore/item_internal.h`,
`src/itemstore/item_hash.c`, `src/itemstore/item_tree.c`,
`src/itemstore/item_registry.c`, `src/itemstore/item_persist.c`,
`src/itemstore/item_error.c`.

Ownership: item tree data model, persistence, lookup/cache behavior, and
structured error items. Itemstore code may depend on common support, bytecode
verification for persisted code items, and value representation. It should not
depend on compiler pipeline or networking.

Key entry points:

- `make_root_item()`, `find_item()`, `insert_code_item()` in
  `src/itemstore/item.h`.
- `save_itemstore()` / `load_itemstore()` in `src/itemstore/item.h`.

### Libcalls

Files: `src/libcall/libcall*.c`, `src/libcall/libcall*.h`.

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
output buffering. Networking may depend on common support and runtime config
types, but should not depend on compiler internals.

### Application Entry Points

Files: `src/scomp.c`, `src/sdiss.c`, `src/sin.c`, `src/config.h`,
`src/version.h`.

Ownership: CLI argument handling, program startup/shutdown, version constants,
and process-wide configuration wiring. Entry points are allowed to depend on the
library modules they orchestrate. Library modules should not depend on CLI entry
points.

## Intended Dependency Direction

Prefer dependencies to flow upward through this stack:

```text
common
  ↑
bytecode, itemstore
  ↑
compiler      runtime VM
                 ↑
              libcall / networking
                 ↑
          application entry points
```

The compiler and runtime are peers that share bytecode, values, diagnostics, and
item data. Crossings between them should be explicit. Examples:

- `sys.compile` intentionally calls the compiler pipeline from a runtime libcall.
- Runtime bytecode execution relies on libcall registry metadata generated from
  `src/libcall/libcall_list.h`.

## Boundary Rules for Changes

- Keep leaf modules (`common`, bytecode helpers, itemstore primitives) free of
  tool and network dependencies.
- Add new libcalls through `src/libcall/libcall_list.h`, implement the
  handler, and add runtime coverage in `tests/core/test_libcall_registry.c`.
- Keep compiler behavior tests under `tests/compiler/` and interpreter behavior
  under `tests/interpreter/`.
- If moving files later, do it as behavior-preserving path/build/include updates
  with no semantic edits in the same change.

## Verification

For normal changes, run:

```sh
make test
```

For module-boundary, compiler, runtime, or build-system changes, also run:

```sh
make test-warnings
./ci/gate_ir_absyn_emitbc.sh
```
