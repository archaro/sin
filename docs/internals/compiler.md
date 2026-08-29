# Compiler pipeline

The compiler is a single bounded pipeline owned by `CompilerContext`. The
public entry points are `compile_source_to_bytecode*()` in
`src/compiler/compiler_pipeline.h`; all variants use the same stage order.

## Data flow

```text
source + ParseInput
    -> lexer/parser -> AS_NODE AST
    -> AST budget check
    -> parameter seeding + semantic locals/params
    -> AST lowering -> IR_Unit
    -> IR validation
    -> byte-count/local/parameter checks -> bytecode emission
    -> emitter post-verification -> OUTPUT_t
```

The lexer and Bison parser construct AST nodes with source spans. The pipeline
then rejects an AST that exceeds its node or traversal-depth budget before
semantic work. `sem_seed_params()` seeds leading parameter slots; semantic
analysis discovers locals in first-seen order, checks local use and loop
context, and creates a separate semantic context for each embedded code body.
The resulting local indices are part of the lowering/emission contract.

Lowering translates expressions, statements, control-flow labels, item
assemblies, libcalls, and embedded-code operations into an `IR_Unit`. IR owns
its instruction and label tables, and owns the embedded-payload table plus its
parameter/local metadata. An embedded source pointer remains borrowed from the
AST/source storage for the IR lifetime; it is not copied or freed by IR.
`ir_validate_diag()` is a structural boundary: it checks opcode operands,
labels/control flow, local indices, pointer-backed payload shape, and embedded
payload references before emission. The emitter performs its own
size/representability checks, writes the versioned bytecode header and stream,
and post-verifies the completed output before returning it.

## Context and destruction

`CompilerContext` borrows the source bytes and owns the AST root, semantic
context, IR unit, diagnostic string, and temporary `OUTPUT_t`. Its reset path
destroys those in their owning APIs (`as_delete`, `sem_delete_ctx`,
`ir_destroy_unit`, and output buffers); the successful output is detached from
the context before cleanup. Every error path goes through the same cleanup, so
callers receive no partial output.

## Provenance and diagnostics

`CompilerSourceSpan` is non-owning and is copied from AST nodes into lowered IR
instructions and embedded payloads. Parse state supplies the scanner location;
semantic, lowering, IR-validation, and emission errors attach the current or
offending span. The pipeline adds source name and a source-line excerpt at its
final diagnostic boundary. Keep spans available when adding nodes or IR
instructions: emitted bytecode does not contain source provenance, but
diagnostics and compiler error items depend on it.

## Embedded code and the runtime crossing

The `code (...)` AST form is semantically checked in its own scope, seeded only
with its declared parameters. Lowering copies the source pointer and builds
owned parameter/local metadata in an IR embedded payload; IR destruction frees
that metadata. Emission writes the embedded parameter marker, terminated
parameter list, and source block into the `ITEM_SAVE_CODE` payload. The wire
layout and limits are specified in [the bytecode reference](bytecode.md).

At runtime, `sys.compile` is the intentional crossing: `libcall_sys.c` invokes
the same diagnostic pipeline on a source string, inserts the resulting bytecode
as a temporary code item, interprets it synchronously, discards only values
produced by that nested run, and deletes the temporary item. Compilation errors
are published through the runtime error item. This path must preserve both
compiler cleanup and runtime stack/frame ownership.

## Durable invariants and change map

Keep stage order and fail-closed behavior intact: no lowering without a valid
AST, no emission without valid IR, no returned output after a failed stage, and
no leaked context-owned allocation. Local and parameter counts must remain
representable in the bytecode header; embedded source/parameter payloads must
remain bounded and unambiguous. Changes to syntax start in `parser.y` or
`lexer.l`, AST/span shape in `absyn.*`, semantic rules in `semant.*`, lowering
in `lower.*`, IR contracts in `ir.*`, and encoding/post-verification in
`emitbc.*` plus the bytecode verifier.

The focused coverage is in `tests/core/test_semant.c` and
`tests/compiler/test_pipeline_*`, `test_compiler_context_failures.c`,
`test_compiler_diag_pipeline.c`, `test_ir_validate.c`, and
`test_emitbc_*`; embedded and runtime crossing cases also live in
`tests/compiler/test_emitbc_invariants.c` and
`tests/core/test_libcall_sys_compile.c`. Update positive and negative coverage
at the boundary that changes, then use the compiler gates described in
[`CONTRIBUTING.md`](../../CONTRIBUTING.md).
